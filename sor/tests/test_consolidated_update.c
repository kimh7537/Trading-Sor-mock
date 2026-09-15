/*
 * T2-02 통합 호가 갱신.
 *
 * 통합 뷰는 캐시를 두지 않기로 했다(T2-01). 그 결정이 실제로 지켜지는지 —
 * 즉 시장 호가창이 바뀐 직후 통합 뷰가 **즉시** 새 값을 주는지 확인한다.
 *
 * 캐시를 몰래 들이면 이 테스트가 깨진다. 그게 이 파일의 존재 이유다.
 * 호가를 바꾸는 경로는 셋뿐이다 — 체결, 취소, 정정. 셋 다 본다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "consolidated.h"
#include "errors.h"
#include "match.h"

#define BASE 10000
#define CAP 512
#define T_BOTH TOD_NS(12, 0, 0)

static order_id_t NEXT_ID = 1;
static ts_t NEXT_TS = T_BOTH;

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
} fixture_t;

static void fx_init(fixture_t *fx)
{
    cons_init(&fx->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        fx->eng[m] = match_engine_create(BASE, CAP);
        assert(fx->eng[m] != NULL);
        assert(cons_attach(&fx->cons, (market_t)m, match_book(fx->eng[m]),
                           NULL) == ERR_OK);
    }
}

static void fx_free(fixture_t *fx)
{
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(fx->eng[m]);
    }
}

static order_id_t put(fixture_t *fx, market_t m, side_t side, price_t price,
                      qty_t qty)
{
    exec_result_t res;
    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = NEXT_TS++;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = ORDER_LIMIT;
    req.market = m;
    assert(match_limit(fx->eng[m], &req, &res) == ERR_OK);
    return req.id;
}

/* 체결로 최우선호가가 사라지면 통합 뷰가 다음 호가로 내려간다 */
static void test_after_execution(void)
{
    fixture_t fx;
    fx_init(&fx);

    put(&fx, MARKET_KRX, SIDE_SELL, 10100, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10050, 100); /* NXT가 최우선 */

    market_t m;
    assert(cons_best_ask(&fx.cons, T_BOTH, &m) == 10050 && m == MARKET_NXT);
    assert(cons_qty_at(&fx.cons, SIDE_SELL, 10050, T_BOTH) == 100);

    /* NXT의 10,050을 전부 먹는다 */
    exec_result_t res;
    order_t taker = {0};
    taker.id = NEXT_ID++;
    taker.ts = NEXT_TS++;
    taker.side = SIDE_BUY;
    taker.price = 10050;
    taker.qty = 100;
    taker.type = ORDER_LIMIT;
    taker.market = MARKET_NXT;
    assert(match_limit(fx.eng[MARKET_NXT], &taker, &res) == ERR_OK);
    assert(res.filled_qty == 100);

    /* 통합 뷰가 바로 KRX의 10,100으로 내려가야 한다 */
    assert(cons_best_ask(&fx.cons, T_BOTH, &m) == 10100);
    assert(m == MARKET_KRX);
    assert(cons_qty_at(&fx.cons, SIDE_SELL, 10050, T_BOTH) == 0);

    fx_free(&fx);
}

/* 부분 체결이면 잔량만 줄어든다 */
static void test_after_partial_execution(void)
{
    fixture_t fx;
    fx_init(&fx);

    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 500);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9900, T_BOTH) == 500);

    exec_result_t res;
    order_t taker = {0};
    taker.id = NEXT_ID++;
    taker.ts = NEXT_TS++;
    taker.side = SIDE_SELL;
    taker.price = 9900;
    taker.qty = 200;
    taker.type = ORDER_LIMIT;
    taker.market = MARKET_KRX;
    assert(match_limit(fx.eng[MARKET_KRX], &taker, &res) == ERR_OK);

    market_t m;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9900 && m == MARKET_KRX);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9900, T_BOTH) == 300);

    fx_free(&fx);
}

/* 취소 뒤에도 즉시 반영된다 */
static void test_after_cancel(void)
{
    fixture_t fx;
    fx_init(&fx);

    order_id_t nxt_id = put(&fx, MARKET_NXT, SIDE_BUY, 9950, 100);
    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 100);

    market_t m;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9950 && m == MARKET_NXT);

    exec_result_t res;
    assert(match_cancel(fx.eng[MARKET_NXT], nxt_id, NEXT_TS++, &res) == ERR_OK);

    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9900);
    assert(m == MARKET_KRX); /* 최우선 시장이 바뀌었다 */
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9950, T_BOTH) == 0);

    fx_free(&fx);
}

/* 정정으로 가격이 옮겨가도 따라온다 */
static void test_after_modify(void)
{
    fixture_t fx;
    fx_init(&fx);

    order_id_t id = put(&fx, MARKET_KRX, SIDE_BUY, 9900, 100);
    put(&fx, MARKET_NXT, SIDE_BUY, 9920, 100); /* 처음엔 NXT가 유리 */

    market_t m;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9920 && m == MARKET_NXT);

    /* KRX 주문을 더 비싸게 올린다 */
    exec_result_t res;
    assert(match_modify(fx.eng[MARKET_KRX], id, 9950, 100, NEXT_TS++, &res) ==
           ERR_OK);

    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9950);
    assert(m == MARKET_KRX); /* 뒤집혔다 */
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9900, T_BOTH) == 0);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9950, T_BOTH) == 100);

    /* 수량 감소 정정도 잔량에 반영된다 */
    assert(match_modify(fx.eng[MARKET_KRX], id, 9950, 40, NEXT_TS++, &res) ==
           ERR_OK);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9950, T_BOTH) == 40);

    fx_free(&fx);
}

/* 스냅샷도 같이 따라온다 — 최우선호가만 갱신되고 스냅샷이 굳는 일이 없어야 한다 */
static void test_snapshot_follows(void)
{
    fixture_t fx;
    fx_init(&fx);

    order_id_t top = put(&fx, MARKET_NXT, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_KRX, SIDE_SELL, 10050, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 100);

    cons_level_view_t view[5];
    assert(cons_snapshot(&fx.cons, SIDE_SELL, 5, T_BOTH, view) == 3);
    assert(view[0].price == 10000 && view[0].market == MARKET_NXT);

    exec_result_t res;
    assert(match_cancel(fx.eng[MARKET_NXT], top, NEXT_TS++, &res) == ERR_OK);

    assert(cons_snapshot(&fx.cons, SIDE_SELL, 5, T_BOTH, view) == 2);
    assert(view[0].price == 10050 && view[0].market == MARKET_KRX);
    assert(view[1].price == 10100 && view[1].market == MARKET_NXT);

    /* 최우선호가와 1단이 여전히 같은 시장을 가리킨다 */
    market_t m;
    assert(cons_best_ask(&fx.cons, T_BOTH, &m) == view[0].price);
    assert(m == view[0].market);

    fx_free(&fx);
}

/* 전부 비면 통합 뷰도 빈다 */
static void test_drained(void)
{
    fixture_t fx;
    fx_init(&fx);

    order_id_t a = put(&fx, MARKET_KRX, SIDE_BUY, 9900, 100);
    order_id_t b = put(&fx, MARKET_NXT, SIDE_BUY, 9900, 100);

    exec_result_t res;
    assert(match_cancel(fx.eng[MARKET_KRX], a, NEXT_TS++, &res) == ERR_OK);
    market_t m;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9900 && m == MARKET_NXT);

    assert(match_cancel(fx.eng[MARKET_NXT], b, NEXT_TS++, &res) == ERR_OK);
    assert(cons_best_bid(&fx.cons, T_BOTH, NULL) == BOOK_PRICE_NONE);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9900, T_BOTH) == 0);

    cons_level_view_t view[5];
    assert(cons_snapshot(&fx.cons, SIDE_BUY, 5, T_BOTH, view) == 0);

    fx_free(&fx);
}

int main(void)
{
    test_after_execution();
    test_after_partial_execution();
    test_after_cancel();
    test_after_modify();
    test_snapshot_follows();
    test_drained();
    return 0;
}
