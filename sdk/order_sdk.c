#include "order_sdk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"

struct sdk {
    char     account[MSG_ACCOUNT_LEN + 1];
    uint64_t next_cl_ord_id;

    sdk_order_t ord[SDK_ORDERS_MAX];
    int32_t     count; /* 쓰이고 있는 자리 수 */

    uint64_t orphans;
};

const char *sdk_state_str(sdk_ord_state_t s)
{
    switch (s) {
    case SDK_ORD_NONE:
        return "NONE";
    case SDK_ORD_PENDING:
        return "PENDING";
    case SDK_ORD_LIVE:
        return "LIVE";
    case SDK_ORD_DONE:
        return "DONE";
    default:
        return "?";
    }
}

sdk_t *sdk_create(const char *account, uint64_t first_cl_ord_id)
{
    if (account == NULL || account[0] == '\0') {
        return NULL;
    }
    /*
     * 0은 "없음"이다(types.h의 ORDER_ID_INVALID와 같은 약속). 첫 번호로
     * 0을 받으면 발급한 번호와 안 받은 번호를 구분할 수 없다.
     */
    if (first_cl_ord_id == 0) {
        return NULL;
    }

    sdk_t *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    snprintf(s->account, sizeof(s->account), "%.*s",
             (int)(sizeof(s->account) - 1), account);
    s->next_cl_ord_id = first_cl_ord_id;
    return s;
}

void sdk_destroy(sdk_t *s)
{
    free(s);
}

/* --- 찾기 --- */

/*
 * 선형 탐색이다.
 *
 * ponytail: 1024자리를 훑는다. 전략 하나가 동시에 띄우는 주문이 수천 건이
 * 되면 재이겠지만, 그때는 `SDK_ORDERS_MAX`부터 다시 정해야 한다. 재 본 적이
 * 없으므로 지금은 자료구조를 두지 않는다(CLAUDE.md).
 */
static sdk_order_t *find(sdk_t *s, uint64_t cl_ord_id)
{
    if (cl_ord_id == 0) {
        return NULL;
    }
    for (int32_t i = 0; i < SDK_ORDERS_MAX; i++) {
        if (s->ord[i].state != SDK_ORD_NONE &&
            s->ord[i].cl_ord_id == cl_ord_id) {
            return &s->ord[i];
        }
    }
    return NULL;
}

static sdk_order_t *find_by_order_id(sdk_t *s, order_id_t order_id)
{
    if (order_id == ORDER_ID_INVALID) {
        return NULL;
    }
    for (int32_t i = 0; i < SDK_ORDERS_MAX; i++) {
        if (s->ord[i].state != SDK_ORD_NONE &&
            s->ord[i].order_id == order_id) {
            return &s->ord[i];
        }
    }
    return NULL;
}

static sdk_order_t *take_slot(sdk_t *s)
{
    for (int32_t i = 0; i < SDK_ORDERS_MAX; i++) {
        if (s->ord[i].state == SDK_ORD_NONE) {
            return &s->ord[i];
        }
    }
    return NULL; /* **덮어쓰지 않는다** */
}

/* --- 내보내기 --- */

int sdk_new_order(sdk_t *s, const char *symbol, side_t side,
                  order_type_t type, market_t market, price_t price, qty_t qty,
                  ts_t now, uint8_t *buf, size_t cap, uint64_t *out_cl_ord_id)
{
    if (s == NULL || symbol == NULL || buf == NULL || out_cl_ord_id == NULL) {
        return ERR_NULL_PTR;
    }
    if (symbol[0] == '\0') {
        return ERR_INVALID_ARG;
    }
    if (side != SIDE_BUY && side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }
    if (market < 0 || market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }
    if (qty <= 0) {
        return ERR_INVALID_QTY;
    }

    sdk_order_t *o = take_slot(s);
    if (o == NULL) {
        return ERR_POOL_EXHAUSTED;
    }

    msg_order_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%.*s",
             (int)(sizeof(req.account) - 1), s->account);
    snprintf(req.symbol, sizeof(req.symbol), "%.*s",
             (int)(sizeof(req.symbol) - 1), symbol);
    req.cl_ord_id = s->next_cl_ord_id;
    req.side = (uint8_t)side;
    req.type = (uint8_t)type;
    req.market = (uint8_t)market;
    req.price = price;
    req.qty = qty;

    int n = msg_encode_order_req(&req, buf, cap);
    if (n < 0) {
        /*
         * **자리를 잡아 두고 전문을 못 만들었으면 자리를 그냥 둔다.**
         * `take_slot`은 아직 아무것도 쓰지 않았으므로 그 자리는 여전히
         * NONE이다 — 보내지도 않은 주문이 자리를 먹지 않는다.
         */
        return n;
    }

    memset(o, 0, sizeof(*o));
    o->cl_ord_id = req.cl_ord_id;
    o->order_id = ORDER_ID_INVALID;
    o->state = SDK_ORD_PENDING;
    snprintf(o->symbol, sizeof(o->symbol), "%.*s",
             (int)(sizeof(o->symbol) - 1), symbol);
    o->side = side;
    o->price = price;
    o->qty = qty;
    o->reject_reason = ERR_OK;
    o->sent_ts = now;

    s->count++;
    s->next_cl_ord_id++;
    *out_cl_ord_id = o->cl_ord_id;
    return n;
}

/* 취소·정정이 공유하는 검사. */
static int mutable_order(sdk_t *s, uint64_t cl_ord_id, sdk_order_t **out)
{
    sdk_order_t *o = find(s, cl_ord_id);
    if (o == NULL) {
        return ERR_NOT_FOUND;
    }
    if (o->state == SDK_ORD_DONE) {
        /*
         * **끝난 주문을 건드리려는 것은 전략이 상태를 잘못 알고 있다는
         * 뜻이다.** 조용히 성공시키면 전략이 그대로 틀린 채로 간다.
         */
        return ERR_NOT_SUPPORTED;
    }
    *out = o;
    return ERR_OK;
}

int sdk_cancel(sdk_t *s, uint64_t cl_ord_id, uint8_t *buf, size_t cap)
{
    if (s == NULL || buf == NULL) {
        return ERR_NULL_PTR;
    }

    sdk_order_t *o = NULL;
    int          rc = mutable_order(s, cl_ord_id, &o);
    if (rc != ERR_OK) {
        return rc;
    }

    msg_cancel_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%.*s",
             (int)(sizeof(req.account) - 1), s->account);
    /*
     * 상대의 번호를 아직 모를 수 있다(PENDING). 그때는 0을 보낸다 —
     * **우리 번호로 찾으라는 뜻**이고, 상대가 그것을 다룰 수 있어야 한다
     * (T3-13의 `ordmap_cancel_key`가 같은 약속을 쓴다).
     */
    req.order_id = o->order_id;
    req.cl_ord_id = o->cl_ord_id;

    return msg_encode_cancel_req(&req, buf, cap);
}

int sdk_modify(sdk_t *s, uint64_t cl_ord_id, price_t new_price, qty_t new_qty,
               uint8_t *buf, size_t cap)
{
    if (s == NULL || buf == NULL) {
        return ERR_NULL_PTR;
    }
    if (new_qty <= 0) {
        return ERR_INVALID_QTY;
    }

    sdk_order_t *o = NULL;
    int          rc = mutable_order(s, cl_ord_id, &o);
    if (rc != ERR_OK) {
        return rc;
    }

    msg_modify_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%.*s",
             (int)(sizeof(req.account) - 1), s->account);
    req.order_id = o->order_id;
    req.cl_ord_id = o->cl_ord_id;
    req.new_price = new_price;
    req.new_qty = new_qty;

    /*
     * **여기서 o->price/qty를 바꾸지 않는다.** 상대가 받아 줬는지 모른다.
     * 미리 바꿔 두면 거부됐을 때 전략이 있지도 않은 값을 보게 된다.
     */
    return msg_encode_modify_req(&req, buf, cap);
}

/* --- 받아들이기 --- */

/* 끝났는지 다시 따진다. 한 군데서만 정해야 상태가 갈라지지 않는다. */
static void settle(sdk_order_t *o)
{
    if (o->state == SDK_ORD_DONE) {
        return;
    }
    if (o->filled_qty + o->canceled_qty >= o->qty) {
        o->state = SDK_ORD_DONE;
    }
}

int sdk_on_order_ack(sdk_t *s, const msg_order_ack_t *ack)
{
    if (s == NULL || ack == NULL) {
        return ERR_NULL_PTR;
    }

    sdk_order_t *o = find(s, ack->cl_ord_id);
    if (o == NULL) {
        s->orphans++;
        return ERR_NOT_FOUND;
    }

    o->order_id = ack->order_id;

    if (ack->reason != ERR_OK) {
        o->reject_reason = ack->reason;
        o->state = SDK_ORD_DONE; /* 거부된 주문은 끝난 주문이다 */
        return ERR_OK;
    }

    if (o->state == SDK_ORD_PENDING) {
        o->state = SDK_ORD_LIVE;
    }

    /*
     * 응답이 들고 온 체결 수량은 **누적치**다. 체결 통보와 겹칠 수 있으므로
     * 더하지 않고 **큰 쪽을 취한다.** 더하면 같은 체결을 두 번 센다.
     */
    if (ack->filled_qty > o->filled_qty) {
        o->filled_qty = ack->filled_qty;
    }
    settle(o);
    return ERR_OK;
}

int sdk_on_cancel_ack(sdk_t *s, const msg_cancel_ack_t *ack)
{
    if (s == NULL || ack == NULL) {
        return ERR_NULL_PTR;
    }

    sdk_order_t *o = find(s, ack->cl_ord_id);
    if (o == NULL) {
        o = find_by_order_id(s, ack->order_id);
    }
    if (o == NULL) {
        s->orphans++;
        return ERR_NOT_FOUND;
    }

    if (ack->reason != ERR_OK) {
        /*
         * 취소가 거부됐다. **주문은 그대로 살아 있다** — 취소 실패를
         * 주문 실패로 읽으면 전략이 있지도 않은 잔량을 잃는다.
         */
        return ERR_OK;
    }

    if (ack->canceled_qty > o->canceled_qty) {
        o->canceled_qty = ack->canceled_qty;
    }
    settle(o);
    return ERR_OK;
}

int sdk_on_modify_ack(sdk_t *s, const msg_modify_ack_t *ack)
{
    if (s == NULL || ack == NULL) {
        return ERR_NULL_PTR;
    }

    sdk_order_t *o = find(s, ack->cl_ord_id);
    if (o == NULL) {
        o = find_by_order_id(s, ack->order_id);
    }
    if (o == NULL) {
        s->orphans++;
        return ERR_NOT_FOUND;
    }

    if (ack->reason != ERR_OK) {
        return ERR_OK; /* 정정이 거부됐다. 옛 값이 그대로다 */
    }

    /* **받아들여졌을 때에만 반영한다.** */
    o->price = ack->price;
    return ERR_OK;
}

int sdk_on_fill(sdk_t *s, const msg_fill_noti_t *fill)
{
    if (s == NULL || fill == NULL) {
        return ERR_NULL_PTR;
    }

    sdk_order_t *o = find(s, fill->cl_ord_id);
    if (o == NULL) {
        o = find_by_order_id(s, fill->order_id);
    }
    if (o == NULL) {
        /*
         * **체결이 사라진 것을 아무도 모르면 안 된다.** 조용히 버리지 않고
         * 세어서 알린다.
         */
        s->orphans++;
        return ERR_NOT_FOUND;
    }

    if (fill->qty <= 0) {
        return ERR_INVALID_QTY;
    }

    o->filled_qty += fill->qty;
    o->notional += (int64_t)fill->price * (int64_t)fill->qty;
    if (o->state == SDK_ORD_PENDING) {
        /* 응답보다 체결이 먼저 올 수 있다. 그래도 살아 있는 주문이다. */
        o->state = SDK_ORD_LIVE;
    }
    settle(o);
    return ERR_OK;
}

/* --- 보기 --- */

const sdk_order_t *sdk_get(const sdk_t *s, uint64_t cl_ord_id)
{
    if (s == NULL) {
        return NULL;
    }
    return find((sdk_t *)s, cl_ord_id);
}

const sdk_order_t *sdk_get_by_order_id(const sdk_t *s, order_id_t order_id)
{
    if (s == NULL) {
        return NULL;
    }
    return find_by_order_id((sdk_t *)s, order_id);
}

static int32_t count_state(const sdk_t *s, sdk_ord_state_t want)
{
    int32_t n = 0;
    for (int32_t i = 0; i < SDK_ORDERS_MAX; i++) {
        if (s->ord[i].state == want) {
            n++;
        }
    }
    return n;
}

int32_t sdk_pending_count(const sdk_t *s)
{
    return (s != NULL) ? count_state(s, SDK_ORD_PENDING) : 0;
}

int32_t sdk_live_count(const sdk_t *s)
{
    return (s != NULL) ? count_state(s, SDK_ORD_LIVE) : 0;
}

int32_t sdk_count(const sdk_t *s)
{
    return (s != NULL) ? s->count : 0;
}

qty_t sdk_remaining(const sdk_t *s, uint64_t cl_ord_id)
{
    const sdk_order_t *o = sdk_get(s, cl_ord_id);
    if (o == NULL) {
        return 0;
    }
    qty_t left = o->qty - o->filled_qty - o->canceled_qty;
    return (left > 0) ? left : 0;
}

price_t sdk_avg_price(const sdk_t *s, uint64_t cl_ord_id)
{
    const sdk_order_t *o = sdk_get(s, cl_ord_id);
    if (o == NULL || o->filled_qty <= 0) {
        return 0;
    }
    return (price_t)(o->notional / (int64_t)o->filled_qty);
}

uint64_t sdk_orphans(const sdk_t *s)
{
    return (s != NULL) ? s->orphans : 0;
}

int32_t sdk_reap_done(sdk_t *s)
{
    if (s == NULL) {
        return 0;
    }
    int32_t n = 0;
    for (int32_t i = 0; i < SDK_ORDERS_MAX; i++) {
        if (s->ord[i].state == SDK_ORD_DONE) {
            memset(&s->ord[i], 0, sizeof(s->ord[i]));
            n++;
        }
    }
    s->count -= n;
    return n;
}
