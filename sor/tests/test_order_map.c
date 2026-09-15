/*
 * T2-09 논리 주문 <-> 물리 주문 매핑.
 *
 * 완료 조건 넷을 그대로 옮긴다.
 *  1. 논리 하나가 물리 여럿으로 갈라지고, 양방향 조회가 O(1)
 *  2. 물리 주문번호가 **시장별로 겹치지 않는다**
 *  3. 물리의 체결·취소가 논리의 상태로 합산된다
 *  4. (불변조건) 논리 잔량 = 원 수량 - 체결 합 - 취소 합
 *
 * 2번은 손으로 고른 몇 개가 아니라 **논리 주문번호 1..2000 x 시장 전부**를 훑어서
 * 번호가 한 번이라도 겹치는지 본다. 산술 인코딩이 맞다면 겹칠 수가 없고, 틀렸다면
 * 반드시 겹친다 — 그 성질을 그대로 검사로 쓴다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"
#include "order_map.h"

#define CAP 64

static order_t make_order(order_id_t id, side_t side, price_t price, qty_t qty)
{
    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = id;
    o.side = side;
    o.price = price;
    o.qty = qty;
    o.type = ORDER_LIMIT;
    return o;
}

/* 시장별 수량을 그대로 다리로 만든 계획. 0주는 plan_add_leg가 걸러 낸다. */
static exec_plan_t make_plan(const order_t *req, qty_t krx, qty_t nxt)
{
    exec_plan_t plan;
    plan_init(&plan);
    assert(plan_add_leg(&plan, MARKET_KRX, krx, req->price, req->type) == ERR_OK);
    assert(plan_add_leg(&plan, MARKET_NXT, nxt, req->price, req->type) == ERR_OK);
    return plan;
}

/* --- 1. 물리 주문번호 산술 --- */

static void test_phys_id_roundtrip(void)
{
    for (order_id_t id = 1; id <= 100; id++) {
        for (int32_t m = 0; m < MARKET_COUNT; m++) {
            order_id_t phys = phys_id_make(id, (market_t)m);
            assert(phys != ORDER_ID_INVALID);
            assert(phys_id_logical(phys) == id);

            market_t got;
            assert(phys_id_market(phys, &got));
            assert(got == (market_t)m);
        }
    }
}

/*
 * 완료 조건: "물리 주문번호는 시장별로 겹치지 않는다."
 *
 * 번호를 전부 만들어 놓고 하나라도 같은 것이 있는지 본다. 인코딩이 틀어지면
 * (예: + 1을 빠뜨리거나 자리값이 모자라면) 서로 다른 (논리, 시장) 쌍이 같은
 * 번호로 접힌다.
 */
static void test_phys_id_never_collides(void)
{
#define N 2000
    static order_id_t ids[N * MARKET_COUNT];
    int32_t           n = 0;

    for (order_id_t id = 1; id <= N; id++) {
        for (int32_t m = 0; m < MARKET_COUNT; m++) {
            ids[n++] = phys_id_make(id, (market_t)m);
        }
    }

    for (int32_t i = 0; i < n; i++) {
        assert(ids[i] != ORDER_ID_INVALID);
        for (int32_t j = i + 1; j < n; j++) {
            assert(ids[i] != ids[j]);
        }
    }
#undef N
}

static void test_phys_id_rejects_bad_input(void)
{
    market_t market;

    /* 논리 주문번호 0은 '없음'이다. */
    assert(phys_id_make(ORDER_ID_INVALID, MARKET_KRX) == ORDER_ID_INVALID);
    /* 없는 시장. */
    assert(phys_id_make(1, (market_t)MARKET_COUNT) == ORDER_ID_INVALID);
    assert(phys_id_make(1, (market_t)-1) == ORDER_ID_INVALID);
    /* 넘치는 논리 주문번호. */
    assert(phys_id_make(LOGICAL_ID_MAX + 1, MARKET_KRX) == ORDER_ID_INVALID);
    /* 상한 자체는 써야 한다. */
    assert(phys_id_make(LOGICAL_ID_MAX, MARKET_KRX) != ORDER_ID_INVALID);

    /* 자리값이 비었거나(0) 없는 시장을 가리키는 번호는 물리 번호가 아니다. */
    assert(!phys_id_market(0, &market));
    assert(!phys_id_market(PHYS_ID_SLOTS, &market));      /* 자리값 0 */
    assert(!phys_id_market(PHYS_ID_SLOTS + 15, &market)); /* 없는 시장 */
    assert(!phys_id_market(MARKET_COUNT, &market));       /* 논리 번호 0 */
    assert(!phys_id_market(PHYS_ID_SLOTS + 1, NULL));

    assert(phys_id_logical(0) == ORDER_ID_INVALID);
    assert(phys_id_logical(PHYS_ID_SLOTS) == ORDER_ID_INVALID);
}

/* --- 2. 등록과 양방향 조회 --- */

static void test_register_splits_into_legs(void)
{
    order_map_t *map = omap_create(CAP);
    assert(map != NULL);

    order_t     req = make_order(7, SIDE_BUY, 10000, 100);
    exec_plan_t plan = make_plan(&req, 40, 60);
    order_id_t  phys[PLAN_LEGS_MAX] = {0};

    assert(omap_register(map, &req, &plan, phys) == ERR_OK);
    assert(omap_count(map) == 1);

    /* 물리 번호는 계획의 다리 순서 그대로 나온다. */
    assert(phys[0] == phys_id_make(7, MARKET_KRX));
    assert(phys[1] == phys_id_make(7, MARKET_NXT));

    /* 논리 -> 물리 */
    const logical_order_t *lo = omap_get(map, 7);
    assert(lo != NULL);
    assert(lo->leg_count == 2);
    assert(lo->order_qty == 100);
    assert(lo->side == SIDE_BUY);
    assert(lo->limit_price == 10000);
    assert(lo->legs[0].market == MARKET_KRX && lo->legs[0].sent_qty == 40);
    assert(lo->legs[1].market == MARKET_NXT && lo->legs[1].sent_qty == 60);
    assert(lo->legs[0].live && lo->legs[1].live);

    /* 물리 -> 논리. 어느 다리로 물어도 같은 논리 주문이 나온다. */
    assert(omap_get_by_phys(map, phys[0]) == lo);
    assert(omap_get_by_phys(map, phys[1]) == lo);

    /* 물리 -> 다리. 번호만 보고 시장을 안다. */
    const phys_leg_t *leg = omap_leg(map, phys[1]);
    assert(leg != NULL && leg->market == MARKET_NXT && leg->sent_qty == 60);

    /* 없는 것은 없다고 한다. */
    assert(omap_get(map, 8) == NULL);
    assert(omap_get(map, ORDER_ID_INVALID) == NULL);
    assert(omap_leg(map, phys_id_make(8, MARKET_KRX)) == NULL);
    assert(omap_get_by_phys(map, 0) == NULL);

    omap_destroy(map);
}

static void test_register_single_leg(void)
{
    order_map_t *map = omap_create(CAP);
    order_t      req = make_order(1, SIDE_SELL, 20000, 50);
    exec_plan_t  plan = make_plan(&req, 50, 0); /* NXT는 0주 -> 다리 없음 */

    assert(plan.leg_count == 1);
    assert(omap_register(map, &req, &plan, NULL) == ERR_OK);

    const logical_order_t *lo = omap_get(map, 1);
    assert(lo->leg_count == 1 && lo->legs[0].market == MARKET_KRX);
    /* 만들지 않은 다리는 조회도 안 된다. */
    assert(omap_leg(map, phys_id_make(1, MARKET_NXT)) == NULL);

    omap_destroy(map);
}

static void test_register_rejects(void)
{
    order_map_t *map = omap_create(2);
    order_t      req = make_order(1, SIDE_BUY, 10000, 100);
    exec_plan_t  plan = make_plan(&req, 40, 60);

    assert(omap_register(NULL, &req, &plan, NULL) == ERR_NULL_PTR);
    assert(omap_register(map, NULL, &plan, NULL) == ERR_NULL_PTR);
    assert(omap_register(map, &req, NULL, NULL) == ERR_NULL_PTR);

    /* 수량 합이 원 주문과 다른 계획은 받지 않는다. 받으면 잔량이 영원히 안 맞는다. */
    exec_plan_t bad = make_plan(&req, 40, 50);
    assert(omap_register(map, &req, &bad, NULL) == ERR_INVALID_QTY);
    assert(omap_count(map) == 0);

    /* 빈 계획은 등록할 물리 주문이 없다. */
    exec_plan_t empty;
    plan_init(&empty);
    assert(omap_register(map, &req, &empty, NULL) == ERR_INVALID_ARG);

    /* 논리 주문번호가 없다. */
    order_t     no_id = make_order(ORDER_ID_INVALID, SIDE_BUY, 10000, 100);
    exec_plan_t p2 = make_plan(&req, 40, 60);
    assert(omap_register(map, &no_id, &p2, NULL) == ERR_INVALID_ARG);

    assert(omap_register(map, &req, &plan, NULL) == ERR_OK);
    /* 같은 논리 주문번호는 두 번 못 넣는다. */
    assert(omap_register(map, &req, &plan, NULL) == ERR_DUPLICATE);

    order_t     req2 = make_order(2, SIDE_BUY, 10000, 100);
    exec_plan_t plan2 = make_plan(&req2, 100, 0);
    assert(omap_register(map, &req2, &plan2, NULL) == ERR_OK);

    /* 용량을 넘기면 거절한다. 크래시하지 않는다. */
    order_t     req3 = make_order(3, SIDE_BUY, 10000, 100);
    exec_plan_t plan3 = make_plan(&req3, 100, 0);
    assert(omap_register(map, &req3, &plan3, NULL) == ERR_POOL_EXHAUSTED);
    assert(omap_count(map) == 2);

    omap_destroy(map);
}

static void test_create_rejects(void)
{
    assert(omap_create(0) == NULL);
    assert(omap_create(-1) == NULL);
    omap_destroy(NULL); /* 널 해제는 무해해야 한다 */
    assert(omap_count(NULL) == 0);
}

/* --- 3. 체결·취소의 합산 --- */

static void test_fills_aggregate(void)
{
    order_map_t *map = omap_create(CAP);
    order_t      req = make_order(5, SIDE_BUY, 10000, 100);
    exec_plan_t  plan = make_plan(&req, 40, 60);
    order_id_t   phys[PLAN_LEGS_MAX];

    assert(omap_register(map, &req, &plan, phys) == ERR_OK);

    /* KRX 10주 @ 9990, NXT 25주 @ 10000 */
    assert(omap_on_fill(map, phys[0], 10, 9990) == ERR_OK);
    assert(omap_on_fill(map, phys[1], 25, 10000) == ERR_OK);

    assert(omap_filled_qty(map, 5) == 35);
    assert(omap_notional(map, 5) == 10 * 9990 + 25 * 10000);
    assert(omap_remaining(map, 5) == 65);

    /* 같은 다리에 또 체결이 와도 누적된다. */
    assert(omap_on_fill(map, phys[0], 30, 9980) == ERR_OK);
    assert(omap_filled_qty(map, 5) == 65);
    assert(omap_notional(map, 5) == 10 * 9990 + 25 * 10000 + 30 * 9980);

    /* KRX 다리는 보낸 40주를 다 채웠다 — 더 이상 살아 있지 않다. */
    assert(!omap_leg(map, phys[0])->live);
    assert(omap_leg(map, phys[1])->live);

    /* 나머지도 채우면 논리 주문 전체가 다 찬다. */
    assert(omap_on_fill(map, phys[1], 35, 10010) == ERR_OK);
    assert(omap_filled_qty(map, 5) == 100);
    assert(omap_remaining(map, 5) == 0);
    assert(!omap_leg(map, phys[1])->live);

    omap_destroy(map);
}

static void test_cancels_aggregate(void)
{
    order_map_t *map = omap_create(CAP);
    order_t      req = make_order(6, SIDE_SELL, 10000, 100);
    exec_plan_t  plan = make_plan(&req, 40, 60);
    order_id_t   phys[PLAN_LEGS_MAX];

    assert(omap_register(map, &req, &plan, phys) == ERR_OK);

    assert(omap_on_fill(map, phys[0], 15, 10000) == ERR_OK);
    assert(omap_on_cancel(map, phys[0], 25) == ERR_OK); /* 남은 25주 취소 */

    assert(omap_canceled_qty(map, 6) == 25);
    assert(omap_filled_qty(map, 6) == 15);
    assert(!omap_leg(map, phys[0])->live);

    /* NXT는 그대로 살아 있다 — 한쪽 취소가 다른 쪽을 건드리지 않는다. */
    assert(omap_leg(map, phys[1])->live);
    assert(omap_remaining(map, 6) == 60);

    assert(omap_on_cancel(map, phys[1], 60) == ERR_OK);
    assert(omap_canceled_qty(map, 6) == 85);
    assert(omap_remaining(map, 6) == 0);

    omap_destroy(map);
}

/*
 * 보낸 수량보다 많이 체결·취소될 수 없다. 넘치면 **아무것도 바꾸지 않고** 거절한다.
 * 절반만 반영하면 합계가 조용히 틀어져 한참 뒤에야 드러난다.
 */
static void test_overfill_rejected_atomically(void)
{
    order_map_t *map = omap_create(CAP);
    order_t      req = make_order(9, SIDE_BUY, 10000, 100);
    exec_plan_t  plan = make_plan(&req, 40, 60);
    order_id_t   phys[PLAN_LEGS_MAX];

    assert(omap_register(map, &req, &plan, phys) == ERR_OK);
    assert(omap_on_fill(map, phys[0], 30, 10000) == ERR_OK);

    phys_leg_t before = *omap_leg(map, phys[0]);

    assert(omap_on_fill(map, phys[0], 11, 10000) == ERR_INVALID_QTY);
    assert(omap_on_cancel(map, phys[0], 11) == ERR_INVALID_QTY);
    /* 체결과 취소를 합쳐서도 넘을 수 없다. */
    assert(omap_on_cancel(map, phys[0], 10) == ERR_OK);
    assert(omap_on_fill(map, phys[0], 1, 10000) == ERR_INVALID_QTY);

    assert(omap_filled_qty(map, 9) == 30);
    assert(omap_canceled_qty(map, 9) == 10);

    /* 거절된 호출이 금액을 건드리지 않았다. */
    assert(omap_leg(map, phys[0])->notional == before.notional);

    omap_destroy(map);
}

static void test_fill_cancel_reject_bad_input(void)
{
    order_map_t *map = omap_create(CAP);
    order_t      req = make_order(11, SIDE_BUY, 10000, 100);
    exec_plan_t  plan = make_plan(&req, 100, 0);
    order_id_t   phys[PLAN_LEGS_MAX];

    assert(omap_register(map, &req, &plan, phys) == ERR_OK);

    assert(omap_on_fill(NULL, phys[0], 1, 10000) == ERR_NULL_PTR);
    assert(omap_on_cancel(NULL, phys[0], 1) == ERR_NULL_PTR);
    assert(omap_on_fill(map, phys[0], 0, 10000) == ERR_INVALID_QTY);
    assert(omap_on_fill(map, phys[0], -1, 10000) == ERR_INVALID_QTY);
    assert(omap_on_fill(map, phys[0], 1, 0) == ERR_INVALID_QTY);
    assert(omap_on_cancel(map, phys[0], 0) == ERR_INVALID_QTY);

    /* 없는 물리 주문. */
    assert(omap_on_fill(map, phys_id_make(99, MARKET_KRX), 1, 10000) ==
           ERR_NOT_FOUND);
    assert(omap_on_fill(map, phys_id_make(11, MARKET_NXT), 1, 10000) ==
           ERR_NOT_FOUND);
    assert(omap_on_cancel(map, 0, 1) == ERR_NOT_FOUND);

    /* 없는 논리 주문의 합계는 0이다. */
    assert(omap_filled_qty(map, 99) == 0);
    assert(omap_notional(map, 99) == 0);
    assert(omap_canceled_qty(map, 99) == 0);
    assert(omap_remaining(map, 99) == 0);

    omap_destroy(map);
}

/*
 * 4. 불변조건 — 논리 잔량 = 원 수량 - 체결 합 - 취소 합.
 *
 * 논리 주문 40개를 서로 다른 배분으로 넣고, 체결과 취소를 섞어 가며 매 단계마다
 * 불변조건과 양방향 조회를 확인한다. 해시 표가 충돌을 제대로 넘기는지도 여기서 걸린다.
 */
static void test_invariant_under_many_orders(void)
{
    order_map_t *map = omap_create(CAP);
    assert(map != NULL);

    static order_id_t phys[CAP][PLAN_LEGS_MAX];
    qty_t             krx_share[CAP];

    for (int32_t i = 0; i < 40; i++) {
        order_id_t  id = (order_id_t)(i * 37 + 1); /* 번호를 띄엄띄엄 준다 */
        qty_t       qty = 100 + i;
        qty_t       krx = (qty_t)(i % (qty - 1)) + 1;
        order_t req = make_order(id, (i % 2) ? SIDE_SELL : SIDE_BUY, 10000, qty);
        exec_plan_t plan = make_plan(&req, krx, qty - krx);

        krx_share[i] = krx;
        assert(omap_register(map, &req, &plan, phys[i]) == ERR_OK);
    }
    assert(omap_count(map) == 40);

    for (int32_t i = 0; i < 40; i++) {
        order_id_t id = (order_id_t)(i * 37 + 1);
        qty_t      qty = 100 + i;
        qty_t      krx = krx_share[i];

        /* 등록한 것이 그대로 조회된다. */
        const logical_order_t *lo = omap_get(map, id);
        assert(lo != NULL && lo->logical_id == id && lo->order_qty == qty);
        assert(omap_get_by_phys(map, phys[i][0]) == lo);

        /* KRX 다리는 절반 체결, 나머지 취소. NXT 다리는 전량 체결. */
        qty_t fill = krx / 2;
        if (fill > 0) {
            assert(omap_on_fill(map, phys[i][0], fill, 10000) == ERR_OK);
        }
        if (krx - fill > 0) {
            assert(omap_on_cancel(map, phys[i][0], krx - fill) == ERR_OK);
        }
        if (lo->leg_count > 1) {
            assert(omap_on_fill(map, phys[i][1], qty - krx, 10010) == ERR_OK);
        }

        assert(omap_filled_qty(map, id) == fill + (qty - krx));
        assert(omap_canceled_qty(map, id) == krx - fill);
        assert(omap_remaining(map, id) ==
               qty - omap_filled_qty(map, id) - omap_canceled_qty(map, id));
        assert(omap_remaining(map, id) == 0);
    }

    omap_destroy(map);
}

int main(void)
{
    test_phys_id_roundtrip();
    test_phys_id_never_collides();
    test_phys_id_rejects_bad_input();
    test_register_splits_into_legs();
    test_register_single_leg();
    test_register_rejects();
    test_create_rejects();
    test_fills_aggregate();
    test_cancels_aggregate();
    test_overfill_rejected_atomically();
    test_fill_cancel_reject_bad_input();
    test_invariant_under_many_orders();
    return 0;
}
