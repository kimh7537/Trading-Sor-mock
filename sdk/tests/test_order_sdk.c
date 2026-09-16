/*
 * T5-07 전략 엔진용 주문 SDK.
 *
 * 완료 조건을 그대로 옮긴다: 왕복(주문→응답→체결), 취소·정정, 부분 체결
 * 누적, 모르는 번호, 자리 부족, **미결 건수가 맞는가**, 결정성.
 *
 * 숫자는 맞아떨어지지 않는 값으로 고른다(T3-11에서 한 번 당했다).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "order_sdk.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

#define ACCOUNT "31940771"
#define SYMBOL "005930"
#define FIRST_ID 700000001ull
#define TS 1000

static sdk_t *fresh(void)
{
    sdk_t *s = sdk_create(ACCOUNT, FIRST_ID);
    assert(s != NULL);
    return s;
}

/* 주문 하나를 내고 번호를 돌려준다. */
static uint64_t send_order(sdk_t *s, price_t price, qty_t qty)
{
    uint8_t  buf[SDK_BODY_MAX];
    uint64_t id = 0;
    int      n = sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX,
                               price, qty, TS, buf, sizeof(buf), &id);
    assert(n == (int)MSG_ORDER_REQ_LEN);
    assert(id != 0);
    return id;
}

static msg_order_ack_t ack_ok(uint64_t cl, order_id_t oid, qty_t filled)
{
    msg_order_ack_t a;
    memset(&a, 0, sizeof(a));
    a.cl_ord_id = cl;
    a.order_id = oid;
    a.status = STATUS_NEW;
    a.reason = ERR_OK;
    a.filled_qty = filled;
    return a;
}

static msg_fill_noti_t fill_of(uint64_t cl, order_id_t oid, price_t price,
                               qty_t qty)
{
    msg_fill_noti_t f;
    memset(&f, 0, sizeof(f));
    f.cl_ord_id = cl;
    f.order_id = oid;
    snprintf(f.symbol, sizeof(f.symbol), "%s", SYMBOL);
    f.market = MARKET_KRX;
    f.side = SIDE_BUY;
    f.price = price;
    f.qty = qty;
    return f;
}

/* --- 1. 왕복 --- */

/* 주문 → 응답 → 체결이 한 논리 주문으로 이어진다 */
static void test_round_trip(void)
{
    sdk_t *s = fresh();

    uint64_t id = send_order(s, 68300, 731);
    assert(id == FIRST_ID); /* 첫 번호는 받은 값 그대로다 */
    assert(sdk_count(s) == 1);
    assert(sdk_pending_count(s) == 1);
    assert(sdk_live_count(s) == 0);

    const sdk_order_t *o = sdk_get(s, id);
    assert(o != NULL);
    assert(o->state == SDK_ORD_PENDING);
    assert(o->order_id == 0); /* 아직 상대의 번호를 모른다 */
    assert(strcmp(o->symbol, SYMBOL) == 0);
    assert(o->qty == 731);
    assert(o->sent_ts == TS);

    msg_order_ack_t a = ack_ok(id, 918273645, 0);
    assert(sdk_on_order_ack(s, &a) == ERR_OK);

    o = sdk_get(s, id);
    assert(o->state == SDK_ORD_LIVE);
    assert(o->order_id == 918273645);
    assert(sdk_pending_count(s) == 0);
    assert(sdk_live_count(s) == 1);

    /* 상대의 번호로도 찾힌다 */
    assert(sdk_get_by_order_id(s, 918273645) == o);

    msg_fill_noti_t f = fill_of(id, 918273645, 68300, 731);
    assert(sdk_on_fill(s, &f) == ERR_OK);

    o = sdk_get(s, id);
    assert(o->state == SDK_ORD_DONE); /* 전량 체결이면 끝이다 */
    assert(o->filled_qty == 731);
    assert(sdk_remaining(s, id) == 0);
    assert(sdk_avg_price(s, id) == 68300);
    assert(sdk_live_count(s) == 0);
    assert(sdk_orphans(s) == 0);

    sdk_destroy(s);
}

/* 번호가 겹치지 않고 하나씩 올라간다 */
static void test_ids_increase(void)
{
    sdk_t *s = fresh();

    uint64_t prev = 0;
    for (int i = 0; i < 50; i++) {
        uint64_t id = send_order(s, 68300, 10 + i);
        assert(id > prev);
        prev = id;
        /* 같은 번호가 두 번 나오지 않는다 */
        assert(sdk_get(s, id) != NULL);
    }
    assert(sdk_count(s) == 50);
    assert(sdk_pending_count(s) == 50);

    sdk_destroy(s);
}

/* --- 2. 부분 체결 누적 --- */

/*
 * **여러 번에 나눠 체결되면 평균 단가가 나온다.**
 * 값을 일부러 안 떨어지게 골라, 한 건만 세거나 덮어쓰는 구현이 통과하지
 * 못하게 한다.
 */
static void test_partial_fills(void)
{
    sdk_t   *s = fresh();
    uint64_t id = send_order(s, 68400, 500);

    msg_order_ack_t a = ack_ok(id, 5551212, 0);
    assert(sdk_on_order_ack(s, &a) == ERR_OK);

    msg_fill_noti_t f1 = fill_of(id, 5551212, 68300, 137);
    msg_fill_noti_t f2 = fill_of(id, 5551212, 68400, 211);
    assert(sdk_on_fill(s, &f1) == ERR_OK);
    assert(sdk_on_fill(s, &f2) == ERR_OK);

    const sdk_order_t *o = sdk_get(s, id);
    assert(o->filled_qty == 137 + 211);
    assert(o->state == SDK_ORD_LIVE); /* 아직 잔량이 있다 */
    assert(sdk_remaining(s, id) == 500 - 348);

    int64_t want = (int64_t)68300 * 137 + (int64_t)68400 * 211;
    assert(o->notional == want);
    assert(sdk_avg_price(s, id) == (price_t)(want / 348));

    /* 나머지가 채워지면 끝난다 */
    msg_fill_noti_t f3 = fill_of(id, 5551212, 68400, 152);
    assert(sdk_on_fill(s, &f3) == ERR_OK);
    assert(sdk_get(s, id)->state == SDK_ORD_DONE);
    assert(sdk_remaining(s, id) == 0);

    sdk_destroy(s);
}

/*
 * 응답의 체결 수량은 **누적치**다. 체결 통보와 겹칠 때 더하면 두 번 센다.
 */
static void test_ack_filled_is_cumulative(void)
{
    sdk_t   *s = fresh();
    uint64_t id = send_order(s, 68400, 400);

    msg_fill_noti_t f = fill_of(id, 0, 68400, 90);
    /* 아직 상대의 번호를 모르므로 우리 번호로 온다 */
    assert(sdk_on_fill(s, &f) == ERR_OK);
    assert(sdk_get(s, id)->filled_qty == 90);
    /* 체결이 먼저 와도 살아 있는 주문으로 본다 */
    assert(sdk_get(s, id)->state == SDK_ORD_LIVE);

    /* 같은 90을 누적치로 들고 온 응답 — 더하면 180이 된다 */
    msg_order_ack_t a = ack_ok(id, 4242, 90);
    assert(sdk_on_order_ack(s, &a) == ERR_OK);
    assert(sdk_get(s, id)->filled_qty == 90);

    /* 응답이 더 큰 누적치를 들고 오면 그것을 취한다 */
    msg_order_ack_t a2 = ack_ok(id, 4242, 150);
    assert(sdk_on_order_ack(s, &a2) == ERR_OK);
    assert(sdk_get(s, id)->filled_qty == 150);

    sdk_destroy(s);
}

/*
 * **낸 수량보다 많이 체결돼도 잔량이 음수가 되지 않는다.**
 *
 * 상대가 더 보내면 SDK는 거절할 방법이 없다 — 이미 일어난 체결이다.
 * 그렇다고 잔량을 음수로 돌려주면 전략이 그 값으로 산술을 해서 더 틀어진다.
 * 0으로 막는다.
 *
 * 넘쳤다는 **사실 자체를 잡는 것은 대사(T5-03의 `RECON_LEG_OVERFILL`)의
 * 일이다.** SDK는 전략이 이상한 값을 보지 않게만 한다.
 */
static void test_overfill_clamps_remaining(void)
{
    sdk_t   *s = fresh();
    uint64_t id = send_order(s, 68400, 100);

    msg_order_ack_t a = ack_ok(id, 1234, 0);
    assert(sdk_on_order_ack(s, &a) == ERR_OK);

    msg_fill_noti_t f1 = fill_of(id, 1234, 68400, 70);
    msg_fill_noti_t f2 = fill_of(id, 1234, 68400, 55); /* 합 125 > 100 */
    assert(sdk_on_fill(s, &f1) == ERR_OK);
    assert(sdk_on_fill(s, &f2) == ERR_OK);

    const sdk_order_t *o = sdk_get(s, id);
    assert(o->filled_qty == 125); /* 받은 것은 숨기지 않고 그대로 센다 */
    assert(o->state == SDK_ORD_DONE);
    assert(sdk_remaining(s, id) == 0); /* **음수가 아니다** */

    /* 취소까지 겹쳐도 마찬가지다 */
    msg_cancel_ack_t ca;
    memset(&ca, 0, sizeof(ca));
    ca.cl_ord_id = id;
    ca.order_id = 1234;
    ca.reason = ERR_OK;
    ca.canceled_qty = 40;
    assert(sdk_on_cancel_ack(s, &ca) == ERR_OK);
    assert(sdk_remaining(s, id) == 0);

    sdk_destroy(s);
}

/* --- 3. 취소와 정정 --- */

static void test_cancel(void)
{
    sdk_t   *s = fresh();
    uint64_t id = send_order(s, 68300, 300);

    uint8_t buf[SDK_BODY_MAX];

    /* 응답 전에도 취소를 낼 수 있다 — 상대 번호 자리는 0으로 간다 */
    int n = sdk_cancel(s, id, buf, sizeof(buf));
    assert(n == (int)MSG_CANCEL_REQ_LEN);

    msg_order_ack_t a = ack_ok(id, 90909, 0);
    assert(sdk_on_order_ack(s, &a) == ERR_OK);

    n = sdk_cancel(s, id, buf, sizeof(buf));
    assert(n == (int)MSG_CANCEL_REQ_LEN);

    msg_cancel_ack_t ca;
    memset(&ca, 0, sizeof(ca));
    ca.cl_ord_id = id;
    ca.order_id = 90909;
    ca.reason = ERR_OK;
    ca.canceled_qty = 300;
    assert(sdk_on_cancel_ack(s, &ca) == ERR_OK);

    const sdk_order_t *o = sdk_get(s, id);
    assert(o->canceled_qty == 300);
    assert(o->state == SDK_ORD_DONE);
    assert(sdk_remaining(s, id) == 0);

    /* **끝난 주문은 다시 취소할 수 없다** */
    assert(sdk_cancel(s, id, buf, sizeof(buf)) == ERR_NOT_SUPPORTED);

    sdk_destroy(s);
}

/*
 * **취소가 거부되면 주문은 그대로 살아 있다.**
 * 취소 실패를 주문 실패로 읽으면 전략이 있지도 않은 잔량을 잃는다.
 */
static void test_cancel_rejected(void)
{
    sdk_t   *s = fresh();
    uint64_t id = send_order(s, 68300, 300);

    msg_order_ack_t a = ack_ok(id, 7777, 0);
    assert(sdk_on_order_ack(s, &a) == ERR_OK);

    msg_cancel_ack_t ca;
    memset(&ca, 0, sizeof(ca));
    ca.cl_ord_id = id;
    ca.order_id = 7777;
    ca.reason = ERR_NOT_FOUND;
    ca.canceled_qty = 0;
    assert(sdk_on_cancel_ack(s, &ca) == ERR_OK);

    const sdk_order_t *o = sdk_get(s, id);
    assert(o->state == SDK_ORD_LIVE); /* 살아 있다 */
    assert(o->canceled_qty == 0);
    assert(sdk_remaining(s, id) == 300);

    sdk_destroy(s);
}

/*
 * **정정은 응답이 온 뒤에 반영한다.**
 * 미리 반영하면 거부됐을 때 전략이 있지도 않은 값을 본다.
 */
static void test_modify(void)
{
    sdk_t   *s = fresh();
    uint64_t id = send_order(s, 68300, 300);

    msg_order_ack_t a = ack_ok(id, 3131, 0);
    assert(sdk_on_order_ack(s, &a) == ERR_OK);

    uint8_t buf[SDK_BODY_MAX];
    int     n = sdk_modify(s, id, 68500, 250, buf, sizeof(buf));
    assert(n == (int)MSG_MODIFY_REQ_LEN);

    /* 아직 옛 값이다 */
    assert(sdk_get(s, id)->price == 68300);

    /* 거부되면 그대로다 */
    msg_modify_ack_t ma;
    memset(&ma, 0, sizeof(ma));
    ma.cl_ord_id = id;
    ma.order_id = 3131;
    ma.reason = ERR_NOT_SUPPORTED;
    ma.price = 68500;
    assert(sdk_on_modify_ack(s, &ma) == ERR_OK);
    assert(sdk_get(s, id)->price == 68300);

    /* 받아들여지면 반영한다 */
    ma.reason = ERR_OK;
    assert(sdk_on_modify_ack(s, &ma) == ERR_OK);
    assert(sdk_get(s, id)->price == 68500);

    sdk_destroy(s);
}

/* --- 4. 거부 --- */

static void test_rejected_order(void)
{
    sdk_t   *s = fresh();
    uint64_t id = send_order(s, 68300, 300);

    msg_order_ack_t a;
    memset(&a, 0, sizeof(a));
    a.cl_ord_id = id;
    a.order_id = 0;
    a.status = STATUS_REJECTED;
    a.reason = ERR_NO_MARGIN;
    assert(sdk_on_order_ack(s, &a) == ERR_OK);

    const sdk_order_t *o = sdk_get(s, id);
    assert(o->state == SDK_ORD_DONE); /* 거부된 주문은 끝난 주문이다 */
    assert(o->reject_reason == ERR_NO_MARGIN);
    assert(sdk_pending_count(s) == 0);
    assert(sdk_live_count(s) == 0);

    /* 끝났으므로 취소도 정정도 안 된다 */
    uint8_t buf[SDK_BODY_MAX];
    assert(sdk_cancel(s, id, buf, sizeof(buf)) == ERR_NOT_SUPPORTED);
    assert(sdk_modify(s, id, 1, 1, buf, sizeof(buf)) == ERR_NOT_SUPPORTED);

    sdk_destroy(s);
}

/* --- 5. 모르는 번호 --- */

/*
 * **조용히 버리지 않는다.**
 * 체결이 사라진 것을 아무도 모르면 장이 끝난 뒤에야 드러난다.
 */
static void test_orphans(void)
{
    sdk_t   *s = fresh();
    uint64_t id = send_order(s, 68300, 100);
    (void)id;

    msg_order_ack_t a = ack_ok(999999999ull, 1, 0);
    assert(sdk_on_order_ack(s, &a) == ERR_NOT_FOUND);
    assert(sdk_orphans(s) == 1);

    msg_fill_noti_t f = fill_of(888888888ull, 0, 100, 10);
    assert(sdk_on_fill(s, &f) == ERR_NOT_FOUND);
    assert(sdk_orphans(s) == 2);

    msg_cancel_ack_t ca;
    memset(&ca, 0, sizeof(ca));
    ca.cl_ord_id = 777777777ull;
    assert(sdk_on_cancel_ack(s, &ca) == ERR_NOT_FOUND);
    assert(sdk_orphans(s) == 3);

    msg_modify_ack_t ma;
    memset(&ma, 0, sizeof(ma));
    ma.cl_ord_id = 666666666ull;
    assert(sdk_on_modify_ack(s, &ma) == ERR_NOT_FOUND);
    assert(sdk_orphans(s) == 4);

    /* 제대로 된 주문은 멀쩡하다 */
    assert(sdk_count(s) == 1);

    sdk_destroy(s);
}

/* --- 6. 자리 부족 --- */

/*
 * **자리가 차면 거절하지 덮어쓰지 않는다.**
 * 덮어쓰면 그 주문의 체결이 갈 곳을 잃는다.
 */
static void test_pool_exhausted(void)
{
    sdk_t   *s = fresh();
    uint8_t  buf[SDK_BODY_MAX];
    uint64_t id = 0;

    for (int32_t i = 0; i < SDK_ORDERS_MAX; i++) {
        int n = sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX,
                              68300, 10, TS, buf, sizeof(buf), &id);
        assert(n == (int)MSG_ORDER_REQ_LEN);
    }
    assert(sdk_count(s) == SDK_ORDERS_MAX);

    uint64_t before = id;
    assert(sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 68300,
                         10, TS, buf, sizeof(buf), &id) == ERR_POOL_EXHAUSTED);
    assert(id == before); /* 건드리지 않았다 */

    /* 먼저 낸 주문이 살아 있다 — 덮어쓰지 않았다 */
    assert(sdk_get(s, FIRST_ID) != NULL);
    assert(sdk_count(s) == SDK_ORDERS_MAX);

    /* 끝난 것을 비우면 다시 받는다 */
    msg_order_ack_t a = ack_ok(FIRST_ID, 1, 0);
    a.reason = ERR_NO_MARGIN;
    assert(sdk_on_order_ack(s, &a) == ERR_OK);
    assert(sdk_reap_done(s) == 1);
    assert(sdk_count(s) == SDK_ORDERS_MAX - 1);
    assert(sdk_get(s, FIRST_ID) == NULL);

    assert(sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 68300,
                         10, TS, buf, sizeof(buf),
                         &id) == (int)MSG_ORDER_REQ_LEN);

    sdk_destroy(s);
}

/* --- 7. 미결 건수 --- */

/*
 * **몇 건이 떠 있는지 모르면 전략이 자기 위험을 계산할 수 없다.**
 * 상태가 옮겨 갈 때마다 수가 맞는지 본다.
 */
static void test_counts_track_states(void)
{
    sdk_t *s = fresh();

    uint64_t a1 = send_order(s, 68300, 100);
    uint64_t a2 = send_order(s, 68300, 200);
    uint64_t a3 = send_order(s, 68300, 300);
    assert(sdk_pending_count(s) == 3);
    assert(sdk_live_count(s) == 0);
    assert(sdk_count(s) == 3);

    msg_order_ack_t k1 = ack_ok(a1, 11, 0);
    assert(sdk_on_order_ack(s, &k1) == ERR_OK);
    assert(sdk_pending_count(s) == 2);
    assert(sdk_live_count(s) == 1);

    msg_order_ack_t k2 = ack_ok(a2, 22, 0);
    assert(sdk_on_order_ack(s, &k2) == ERR_OK);
    assert(sdk_pending_count(s) == 1);
    assert(sdk_live_count(s) == 2);

    /* a1을 전량 체결시킨다 */
    msg_fill_noti_t f = fill_of(a1, 11, 68300, 100);
    assert(sdk_on_fill(s, &f) == ERR_OK);
    assert(sdk_live_count(s) == 1);
    assert(sdk_pending_count(s) == 1);
    assert(sdk_count(s) == 3); /* 끝난 것도 자리에는 남아 있다 */

    /* a3는 아직 응답도 안 왔다 */
    assert(sdk_get(s, a3)->state == SDK_ORD_PENDING);

    assert(sdk_reap_done(s) == 1);
    assert(sdk_count(s) == 2);
    assert(sdk_pending_count(s) == 1);
    assert(sdk_live_count(s) == 1);

    sdk_destroy(s);
}

/* --- 8. 결정성 --- */

/*
 * **같은 입력이 같은 바이트를 만든다.**
 * SDK가 시스템 시각이나 전역 난수를 읽으면 여기서 깨진다.
 */
static void test_deterministic(void)
{
    uint8_t w1[SDK_BODY_MAX];
    uint8_t w2[SDK_BODY_MAX];
    memset(w1, 0xEE, sizeof(w1));
    memset(w2, 0x11, sizeof(w2));

    uint64_t id1 = 0, id2 = 0;

    sdk_t *s1 = fresh();
    sdk_t *s2 = fresh();

    int n1 = sdk_new_order(s1, SYMBOL, SIDE_SELL, ORDER_LIMIT, MARKET_NXT,
                           68350, 417, 12345, w1, sizeof(w1), &id1);
    int n2 = sdk_new_order(s2, SYMBOL, SIDE_SELL, ORDER_LIMIT, MARKET_NXT,
                           68350, 417, 12345, w2, sizeof(w2), &id2);
    assert(n1 == n2);
    assert(id1 == id2);
    assert(memcmp(w1, w2, (size_t)n1) == 0);

    sdk_destroy(s1);
    sdk_destroy(s2);
}

/* --- 9. 인자 --- */

static void test_args(void)
{
    uint8_t  buf[SDK_BODY_MAX];
    uint64_t id = 0;

    assert(sdk_create(NULL, 1) == NULL);
    assert(sdk_create("", 1) == NULL);
    /* 0은 "없음"이라 첫 번호로 쓸 수 없다 */
    assert(sdk_create(ACCOUNT, 0) == NULL);

    sdk_t *s = fresh();

    assert(sdk_new_order(NULL, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 1, 1,
                         TS, buf, sizeof(buf), &id) == ERR_NULL_PTR);
    assert(sdk_new_order(s, NULL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 1, 1, TS,
                         buf, sizeof(buf), &id) == ERR_NULL_PTR);
    assert(sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 1, 1,
                         TS, NULL, sizeof(buf), &id) == ERR_NULL_PTR);
    assert(sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 1, 1,
                         TS, buf, sizeof(buf), NULL) == ERR_NULL_PTR);

    assert(sdk_new_order(s, "", SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 1, 1, TS,
                         buf, sizeof(buf), &id) == ERR_INVALID_ARG);
    assert(sdk_new_order(s, SYMBOL, (side_t)9, ORDER_LIMIT, MARKET_KRX, 1, 1,
                         TS, buf, sizeof(buf), &id) == ERR_INVALID_ARG);
    assert(sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT,
                         (market_t)MARKET_COUNT, 1, 1, TS, buf, sizeof(buf),
                         &id) == ERR_INVALID_ARG);
    assert(sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 1, 0,
                         TS, buf, sizeof(buf), &id) == ERR_INVALID_QTY);

    /* 버퍼가 모자라면 쓰지 않는다. **자리도 먹지 않는다** */
    int32_t before = sdk_count(s);
    assert(sdk_new_order(s, SYMBOL, SIDE_BUY, ORDER_LIMIT, MARKET_KRX, 1, 1,
                         TS, buf, MSG_ORDER_REQ_LEN - 1, &id) < 0);
    assert(sdk_count(s) == before);

    assert(sdk_cancel(s, 12345, buf, sizeof(buf)) == ERR_NOT_FOUND);
    assert(sdk_modify(s, 12345, 1, 1, buf, sizeof(buf)) == ERR_NOT_FOUND);
    assert(sdk_cancel(NULL, 1, buf, sizeof(buf)) == ERR_NULL_PTR);
    assert(sdk_modify(s, 1, 1, 0, buf, sizeof(buf)) == ERR_INVALID_QTY);

    assert(sdk_on_order_ack(s, NULL) == ERR_NULL_PTR);
    assert(sdk_on_fill(NULL, NULL) == ERR_NULL_PTR);

    /* NULL을 받아도 터지지 않는다 */
    assert(sdk_get(NULL, 1) == NULL);
    assert(sdk_get_by_order_id(NULL, 1) == NULL);
    assert(sdk_pending_count(NULL) == 0);
    assert(sdk_live_count(NULL) == 0);
    assert(sdk_count(NULL) == 0);
    assert(sdk_remaining(NULL, 1) == 0);
    assert(sdk_avg_price(NULL, 1) == 0);
    assert(sdk_orphans(NULL) == 0);
    assert(sdk_reap_done(NULL) == 0);
    sdk_destroy(NULL);

    /* 없는 주문의 값은 0이다 */
    assert(sdk_remaining(s, 999) == 0);
    assert(sdk_avg_price(s, 999) == 0);

    sdk_destroy(s);
}

static void test_state_str(void)
{
    assert(strcmp(sdk_state_str(SDK_ORD_NONE), "NONE") == 0);
    assert(strcmp(sdk_state_str(SDK_ORD_PENDING), "PENDING") == 0);
    assert(strcmp(sdk_state_str(SDK_ORD_LIVE), "LIVE") == 0);
    assert(strcmp(sdk_state_str(SDK_ORD_DONE), "DONE") == 0);
    assert(strcmp(sdk_state_str((sdk_ord_state_t)77), "?") == 0);
}

int main(void)
{
    STEP(test_round_trip);
    STEP(test_ids_increase);
    STEP(test_partial_fills);
    STEP(test_ack_filled_is_cumulative);
    STEP(test_overfill_clamps_remaining);
    STEP(test_cancel);
    STEP(test_cancel_rejected);
    STEP(test_modify);
    STEP(test_rejected_order);
    STEP(test_orphans);
    STEP(test_pool_exhausted);
    STEP(test_counts_track_states);
    STEP(test_deterministic);
    STEP(test_args);
    STEP(test_state_str);
    return 0;
}
