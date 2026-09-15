/*
 * T2-01 통합 호가창.
 *
 * 위험 지점은 둘이다.
 *  1. 동률. 두 시장의 최우선호가가 같을 때 어느 쪽을 고르는가 — 임의로 정해지면
 *     같은 입력에 다른 라우팅이 나오고, 전략 비교가 무의미해진다.
 *  2. 최우선호가와 스냅샷이 **같은 순서 규칙**을 쓰는가. 둘이 갈리면
 *     "1단 호가"와 "최우선호가"가 다른 시장을 가리키는 일이 생긴다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "consolidated.h"
#include "errors.h"
#include "match.h"

#define BASE 10000
#define CAP 512
#define T_BOTH TOD_NS(12, 0, 0)      /* KRX·NXT 모두 열림 */
#define T_NXT_ONLY TOD_NS(18, 0, 0)  /* 애프터마켓 — NXT만 */
#define T_KRX_ONLY TOD_NS(15, 25, 0) /* NXT 오후 휴장 — KRX만 */
#define T_CLOSED TOD_NS(22, 0, 0)    /* 둘 다 닫힘 */

static order_id_t NEXT_ID = 1;

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
} fixture_t;

static void fx_init(fixture_t *fx, bool with_rules)
{
    const market_rules_t *rules[MARKET_COUNT] = {
        [MARKET_KRX] = &KRX_RULES,
        [MARKET_NXT] = &NXT_RULES,
    };
    cons_init(&fx->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        fx->eng[m] = match_engine_create(BASE, CAP);
        assert(fx->eng[m] != NULL);
        assert(cons_attach(&fx->cons, (market_t)m, match_book(fx->eng[m]),
                           with_rules ? rules[m] : NULL) == ERR_OK);
    }
}

static void fx_free(fixture_t *fx)
{
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(fx->eng[m]);
    }
}

static void put(fixture_t *fx, market_t m, side_t side, price_t price, qty_t qty)
{
    exec_result_t res;
    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = T_BOTH;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = ORDER_LIMIT;
    req.market = m;
    assert(match_limit(fx->eng[m], &req, &res) == ERR_OK);
}

/* 빈 통합 호가창 */
static void test_empty(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    market_t m = MARKET_NXT;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == BOOK_PRICE_NONE);
    assert(m == MARKET_NXT); /* 못 찾으면 out_market을 건드리지 않는다 */
    assert(cons_best_ask(&fx.cons, T_BOTH, NULL) == BOOK_PRICE_NONE);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, BASE, T_BOTH) == 0);

    cons_level_view_t view[10];
    assert(cons_snapshot(&fx.cons, SIDE_BUY, 10, T_BOTH, view) == 0);

    fx_free(&fx);
}

/* 한쪽에만 호가가 있으면 그쪽이 통합 최우선호가다 */
static void test_single_side(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 100);

    market_t m = MARKET_KRX;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9900);
    assert(m == MARKET_NXT);
    assert(cons_best_ask(&fx.cons, T_BOTH, NULL) == BOOK_PRICE_NONE);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9900, T_BOTH) == 100);

    fx_free(&fx);
}

/* 가격이 다르면 유리한 가격이 이긴다 */
static void test_price_wins(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 1000); /* 잔량은 많지만 싸다 */
    put(&fx, MARKET_NXT, SIDE_BUY, 9950, 10);   /* 잔량은 적지만 비싸다 */

    market_t m;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9950);
    assert(m == MARKET_NXT); /* 가격이 잔량보다 우선한다 */

    put(&fx, MARKET_KRX, SIDE_SELL, 10100, 10);
    put(&fx, MARKET_NXT, SIDE_SELL, 10050, 1000);
    assert(cons_best_ask(&fx.cons, T_BOTH, &m) == 10050);
    assert(m == MARKET_NXT);

    fx_free(&fx);
}

/* 가격이 같으면 잔량이 많은 시장 */
static void test_tie_broken_by_qty(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 50);
    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 200);

    market_t m;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9900);
    assert(m == MARKET_NXT);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9900, T_BOTH) == 250); /* 합산 */

    /* 반대로 뒤집어도 규칙이 같다 */
    fixture_t fx2;
    fx_init(&fx2, false);
    put(&fx2, MARKET_KRX, SIDE_BUY, 9900, 200);
    put(&fx2, MARKET_NXT, SIDE_BUY, 9900, 50);
    assert(cons_best_bid(&fx2.cons, T_BOTH, &m) == 9900);
    assert(m == MARKET_KRX);
    fx_free(&fx2);

    fx_free(&fx);
}

/* 가격도 잔량도 같으면 시장 열거 순서 (KRX) */
static void test_tie_broken_by_market(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 100); /* NXT를 먼저 넣어도 */
    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 100);

    market_t m;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9900);
    assert(m == MARKET_KRX); /* 넣은 순서와 무관하다 */

    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 100);
    put(&fx, MARKET_KRX, SIDE_SELL, 10100, 100);
    assert(cons_best_ask(&fx.cons, T_BOTH, &m) == 10100);
    assert(m == MARKET_KRX);

    fx_free(&fx);
}

/* 시장 간 교차 — 한쪽 매수호가가 다른 쪽 매도호가보다 높다 */
static void test_cross_between_markets(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    /* 각 시장 안에서는 교차하지 않지만 시장을 겹치면 교차한다 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_NXT, SIDE_BUY, 10050, 100);

    market_t bm, am;
    price_t bid = cons_best_bid(&fx.cons, T_BOTH, &bm);
    price_t ask = cons_best_ask(&fx.cons, T_BOTH, &am);

    assert(bid == 10050 && bm == MARKET_NXT);
    assert(ask == 10000 && am == MARKET_KRX);
    assert(bid > ask); /* 통합 호가창은 교차할 수 있다 */

    fx_free(&fx);
}

/* 닫힌 시장은 빠진다 */
static void test_closed_market_excluded(void)
{
    fixture_t fx;
    fx_init(&fx, true); /* 이번에는 규칙을 붙인다 */

    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 100);
    put(&fx, MARKET_NXT, SIDE_BUY, 9950, 100); /* NXT가 더 유리 */

    market_t m;

    /* 둘 다 열림 — NXT가 이긴다 */
    assert(cons_is_open(&fx.cons, MARKET_KRX, T_BOTH));
    assert(cons_is_open(&fx.cons, MARKET_NXT, T_BOTH));
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == 9950 && m == MARKET_NXT);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9950, T_BOTH) == 100);

    /* NXT 오후 휴장 — KRX만 남는다 */
    assert(cons_is_open(&fx.cons, MARKET_KRX, T_KRX_ONLY));
    assert(!cons_is_open(&fx.cons, MARKET_NXT, T_KRX_ONLY));
    assert(cons_best_bid(&fx.cons, T_KRX_ONLY, &m) == 9900 && m == MARKET_KRX);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9950, T_KRX_ONLY) == 0);

    /* 애프터마켓 — NXT만 남는다 */
    assert(!cons_is_open(&fx.cons, MARKET_KRX, T_NXT_ONLY));
    assert(cons_is_open(&fx.cons, MARKET_NXT, T_NXT_ONLY));
    assert(cons_best_bid(&fx.cons, T_NXT_ONLY, &m) == 9950 && m == MARKET_NXT);

    /* 둘 다 닫힘 */
    assert(cons_best_bid(&fx.cons, T_CLOSED, NULL) == BOOK_PRICE_NONE);
    assert(cons_qty_at(&fx.cons, SIDE_BUY, 9900, T_CLOSED) == 0);
    cons_level_view_t view[4];
    assert(cons_snapshot(&fx.cons, SIDE_BUY, 4, T_CLOSED, view) == 0);

    fx_free(&fx);
}

/* 스냅샷 — 가격 순서, 같은 가격이면 시장별로 따로 */
static void test_snapshot(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 100);
    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 300); /* 같은 가격, 잔량 많음 */
    put(&fx, MARKET_KRX, SIDE_BUY, 9950, 50);  /* 더 비싸다 */
    put(&fx, MARKET_NXT, SIDE_BUY, 9850, 500); /* 더 싸다 */

    cons_level_view_t view[10];
    int n = cons_snapshot(&fx.cons, SIDE_BUY, 10, T_BOTH, view);
    assert(n == 4);

    assert(view[0].price == 9950 && view[0].market == MARKET_KRX);
    /* 9,900 동률 — 잔량이 많은 NXT가 앞 */
    assert(view[1].price == 9900 && view[1].market == MARKET_NXT &&
           view[1].total_qty == 300);
    assert(view[2].price == 9900 && view[2].market == MARKET_KRX &&
           view[2].total_qty == 100);
    assert(view[3].price == 9850 && view[3].market == MARKET_NXT);

    /* 1단은 최우선호가와 같은 시장을 가리켜야 한다 */
    market_t m;
    assert(cons_best_bid(&fx.cons, T_BOTH, &m) == view[0].price);
    assert(m == view[0].market);

    /* depth를 줄이면 앞에서부터 그만큼만 */
    assert(cons_snapshot(&fx.cons, SIDE_BUY, 2, T_BOTH, view) == 2);
    assert(view[0].price == 9950 && view[1].price == 9900);

    /* 매도는 싼 가격부터 */
    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 100);
    put(&fx, MARKET_KRX, SIDE_SELL, 10050, 100);
    n = cons_snapshot(&fx.cons, SIDE_SELL, 10, T_BOTH, view);
    assert(n == 2);
    assert(view[0].price == 10050 && view[0].market == MARKET_KRX);
    assert(view[1].price == 10100 && view[1].market == MARKET_NXT);

    fx_free(&fx);
}

/* 인자 검증 */
static void test_args(void)
{
    cons_book_t cons;
    cons_init(&cons);
    cons_init(NULL);

    assert(cons_attach(NULL, MARKET_KRX, NULL, NULL) == ERR_NULL_PTR);
    assert(cons_attach(&cons, MARKET_KRX, NULL, NULL) == ERR_INVALID_ARG);
    assert(cons_attach(&cons, (market_t)99, NULL, NULL) == ERR_INVALID_ARG);

    /* 아무것도 안 붙은 통합 뷰 */
    assert(!cons_is_open(&cons, MARKET_KRX, T_BOTH));
    assert(cons_best_bid(&cons, T_BOTH, NULL) == BOOK_PRICE_NONE);
    assert(cons_best_bid(NULL, T_BOTH, NULL) == BOOK_PRICE_NONE);
    assert(cons_best_ask(NULL, T_BOTH, NULL) == BOOK_PRICE_NONE);
    assert(cons_qty_at(NULL, SIDE_BUY, BASE, T_BOTH) == 0);
    assert(!cons_is_open(NULL, MARKET_KRX, T_BOTH));

    cons_level_view_t view[4];
    assert(cons_snapshot(NULL, SIDE_BUY, 4, T_BOTH, view) == ERR_NULL_PTR);
    assert(cons_snapshot(&cons, SIDE_BUY, 4, T_BOTH, NULL) == ERR_NULL_PTR);
    assert(cons_snapshot(&cons, SIDE_BUY, 0, T_BOTH, view) == ERR_INVALID_ARG);
    assert(cons_snapshot(&cons, (side_t)9, 4, T_BOTH, view) == ERR_INVALID_ARG);
}

int main(void)
{
    test_args();
    test_empty();
    test_single_side();
    test_price_wins();
    test_tie_broken_by_qty();
    test_tie_broken_by_market();
    test_cross_between_markets();
    test_closed_market_excluded();
    test_snapshot();
    return 0;
}
