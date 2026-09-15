#include "order_map.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"

/* --- 물리 주문번호 산술 --- */

order_id_t phys_id_make(order_id_t logical_id, market_t market)
{
    if (logical_id == ORDER_ID_INVALID || logical_id > LOGICAL_ID_MAX) {
        return ORDER_ID_INVALID;
    }
    if ((int32_t)market < 0 || (int32_t)market >= MARKET_COUNT) {
        return ORDER_ID_INVALID;
    }
    return logical_id * PHYS_ID_SLOTS + (order_id_t)market + 1;
}

bool phys_id_market(order_id_t phys_id, market_t *out_market)
{
    if (out_market == NULL || phys_id == ORDER_ID_INVALID) {
        return false;
    }

    order_id_t slot = phys_id % PHYS_ID_SLOTS;
    /* 자리값 0은 시장이 비었다는 뜻이고, 시장 수를 넘으면 없는 시장이다. */
    if (slot == 0 || slot > (order_id_t)MARKET_COUNT) {
        return false;
    }
    /* 논리 주문번호가 0이면 애초에 발급될 수 없는 번호다. */
    if (phys_id / PHYS_ID_SLOTS == 0) {
        return false;
    }

    *out_market = (market_t)(slot - 1);
    return true;
}

order_id_t phys_id_logical(order_id_t phys_id)
{
    market_t market;
    if (!phys_id_market(phys_id, &market)) {
        return ORDER_ID_INVALID;
    }
    return phys_id / PHYS_ID_SLOTS;
}

/* --- 매핑 --- */

/*
 * 논리 주문번호 -> 슬롯 번호를 오픈 어드레싱으로 찾는다.
 *
 * 삭제가 없다. 논리 주문은 집행 이력이라 체결이 끝나도 남아 있어야 나중에 평균 체결
 * 단가를 낼 수 있다. 그래서 T1-07의 역방향 시프트가 필요 없고 표가 훨씬 단순하다.
 */
#define OMAP_LOAD_DEN 2

struct order_map {
    logical_order_t *orders; /* 논리 주문 배열. 등록 순서대로 채운다 */
    int32_t         *slot;   /* 해시 표. -1이면 빈 칸 */
    int32_t          mask;
    int32_t          capacity;
    int32_t          count;
};

static uint64_t hash_id(order_id_t id)
{
    uint64_t x = (uint64_t)id;

    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

/* 있으면 슬롯 번호, 없으면 -1. */
static int32_t find_slot(const order_map_t *map, order_id_t logical_id)
{
    int32_t i = (int32_t)(hash_id(logical_id) & (uint64_t)map->mask);

    while (map->slot[i] >= 0) {
        if (map->orders[map->slot[i]].logical_id == logical_id) {
            return map->slot[i];
        }
        i = (i + 1) & map->mask;
    }
    return -1;
}

order_map_t *omap_create(int32_t capacity)
{
    if (capacity <= 0) {
        return NULL;
    }

    int64_t want = (int64_t)capacity * OMAP_LOAD_DEN;
    int64_t buckets = 1;
    while (buckets < want) {
        if (buckets > INT32_MAX / 4) {
            return NULL;
        }
        buckets *= 2;
    }

    order_map_t *map = calloc(1, sizeof(*map));
    if (map == NULL) {
        return NULL;
    }
    map->orders = calloc((size_t)capacity, sizeof(*map->orders));
    map->slot = malloc((size_t)buckets * sizeof(*map->slot));
    if (map->orders == NULL || map->slot == NULL) {
        omap_destroy(map);
        return NULL;
    }
    for (int64_t i = 0; i < buckets; i++) {
        map->slot[i] = -1;
    }
    map->mask = (int32_t)(buckets - 1);
    map->capacity = capacity;
    map->count = 0;

    return map;
}

void omap_destroy(order_map_t *map)
{
    if (map == NULL) {
        return;
    }
    free(map->orders);
    free(map->slot);
    free(map);
}

int omap_register(order_map_t *map, const order_t *req, const exec_plan_t *plan,
                  order_id_t *out_phys)
{
    if (map == NULL || req == NULL || plan == NULL) {
        return ERR_NULL_PTR;
    }
    if (req->id == ORDER_ID_INVALID || req->id > LOGICAL_ID_MAX) {
        return ERR_INVALID_ARG;
    }

    /*
     * 계획이 불변조건을 어기면 여기서 멈춘다. 다리 수량의 합이 주문 수량과 다른 채로
     * 등록하면 잔량이 영원히 안 맞고, 그 사실은 한참 뒤 체결 단계에서야 드러난다.
     */
    int rc = plan_validate(plan, req);
    if (rc != ERR_OK) {
        return rc;
    }
    if (plan->leg_count == 0) {
        return ERR_INVALID_ARG; /* 보낼 물리 주문이 없는 계획은 등록할 것이 없다 */
    }

    if (find_slot(map, req->id) >= 0) {
        return ERR_DUPLICATE;
    }
    if (map->count >= map->capacity) {
        return ERR_POOL_EXHAUSTED;
    }

    int32_t          idx = map->count;
    logical_order_t *lo = &map->orders[idx];

    memset(lo, 0, sizeof(*lo));
    lo->logical_id = req->id;
    lo->side = req->side;
    lo->limit_price = req->price;
    lo->order_qty = req->qty;
    lo->leg_count = plan->leg_count;

    for (int32_t i = 0; i < plan->leg_count; i++) {
        const plan_leg_t *leg = &plan->legs[i];
        phys_leg_t       *pl = &lo->legs[i];

        pl->phys_id = phys_id_make(req->id, leg->market);
        /* 위에서 논리 주문번호와 계획을 이미 검사했으므로 실패할 수 없다. */
        assert(pl->phys_id != ORDER_ID_INVALID);
        pl->market = leg->market;
        pl->sent_qty = leg->qty;
        pl->live = true;

        if (out_phys != NULL) {
            out_phys[i] = pl->phys_id;
        }
    }

    /* 부하율 0.5를 지키므로 빈 칸이 반드시 있다. */
    int32_t i = (int32_t)(hash_id(req->id) & (uint64_t)map->mask);
    while (map->slot[i] >= 0) {
        i = (i + 1) & map->mask;
    }
    map->slot[i] = idx;
    map->count++;

    return ERR_OK;
}

const logical_order_t *omap_get(const order_map_t *map, order_id_t logical_id)
{
    if (map == NULL || logical_id == ORDER_ID_INVALID) {
        return NULL;
    }
    int32_t idx = find_slot(map, logical_id);
    return (idx >= 0) ? &map->orders[idx] : NULL;
}

const logical_order_t *omap_get_by_phys(const order_map_t *map,
                                        order_id_t phys_id)
{
    return omap_get(map, phys_id_logical(phys_id));
}

/* 물리 다리를 찾는다. 없으면 NULL. */
static phys_leg_t *find_leg(order_map_t *map, order_id_t phys_id)
{
    if (map == NULL) {
        return NULL;
    }

    order_id_t logical_id = phys_id_logical(phys_id);
    if (logical_id == ORDER_ID_INVALID) {
        return NULL;
    }
    int32_t idx = find_slot(map, logical_id);
    if (idx < 0) {
        return NULL;
    }

    logical_order_t *lo = &map->orders[idx];
    for (int32_t i = 0; i < lo->leg_count; i++) {
        if (lo->legs[i].phys_id == phys_id) {
            return &lo->legs[i];
        }
    }
    return NULL;
}

const phys_leg_t *omap_leg(const order_map_t *map, order_id_t phys_id)
{
    return find_leg((order_map_t *)map, phys_id);
}

int omap_on_fill(order_map_t *map, order_id_t phys_id, qty_t qty, price_t price)
{
    if (map == NULL) {
        return ERR_NULL_PTR;
    }
    if (qty <= 0 || price <= 0) {
        return ERR_INVALID_QTY;
    }

    phys_leg_t *leg = find_leg(map, phys_id);
    if (leg == NULL) {
        return ERR_NOT_FOUND;
    }

    /*
     * 보낸 수량보다 많이 체결될 수 없다. 넘치면 아무것도 바꾸지 않고 거절한다 —
     * 절반만 반영해 두면 합계가 조용히 틀어져 나중에 원인을 못 찾는다.
     */
    if (leg->filled_qty + leg->canceled_qty + qty > leg->sent_qty) {
        return ERR_INVALID_QTY;
    }

    leg->filled_qty += qty;
    leg->notional += (int64_t)price * (int64_t)qty;
    if (leg->filled_qty + leg->canceled_qty == leg->sent_qty) {
        leg->live = false;
    }

    return ERR_OK;
}

int omap_on_cancel(order_map_t *map, order_id_t phys_id, qty_t qty)
{
    if (map == NULL) {
        return ERR_NULL_PTR;
    }
    if (qty <= 0) {
        return ERR_INVALID_QTY;
    }

    phys_leg_t *leg = find_leg(map, phys_id);
    if (leg == NULL) {
        return ERR_NOT_FOUND;
    }
    if (leg->filled_qty + leg->canceled_qty + qty > leg->sent_qty) {
        return ERR_INVALID_QTY;
    }

    leg->canceled_qty += qty;
    if (leg->filled_qty + leg->canceled_qty == leg->sent_qty) {
        leg->live = false;
    }

    return ERR_OK;
}

qty_t omap_filled_qty(const order_map_t *map, order_id_t logical_id)
{
    const logical_order_t *lo = omap_get(map, logical_id);
    if (lo == NULL) {
        return 0;
    }
    qty_t sum = 0;
    for (int32_t i = 0; i < lo->leg_count; i++) {
        sum += lo->legs[i].filled_qty;
    }
    return sum;
}

int64_t omap_notional(const order_map_t *map, order_id_t logical_id)
{
    const logical_order_t *lo = omap_get(map, logical_id);
    if (lo == NULL) {
        return 0;
    }
    int64_t sum = 0;
    for (int32_t i = 0; i < lo->leg_count; i++) {
        sum += lo->legs[i].notional;
    }
    return sum;
}

qty_t omap_canceled_qty(const order_map_t *map, order_id_t logical_id)
{
    const logical_order_t *lo = omap_get(map, logical_id);
    if (lo == NULL) {
        return 0;
    }
    qty_t sum = 0;
    for (int32_t i = 0; i < lo->leg_count; i++) {
        sum += lo->legs[i].canceled_qty;
    }
    return sum;
}

qty_t omap_remaining(const order_map_t *map, order_id_t logical_id)
{
    const logical_order_t *lo = omap_get(map, logical_id);
    if (lo == NULL) {
        return 0;
    }
    return lo->order_qty - omap_filled_qty(map, logical_id) -
           omap_canceled_qty(map, logical_id);
}

int32_t omap_count(const order_map_t *map)
{
    return (map != NULL) ? map->count : 0;
}
