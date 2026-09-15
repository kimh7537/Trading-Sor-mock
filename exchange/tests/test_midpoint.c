/*
 * T1-16 NXT 중간가 주문.
 *
 * 규칙은 "가격 = (최우선매수 + 최우선매도) / 2, 호가 단위로 내림"이다.
 * 검증할 것은 넷.
 *  1. 짝수·홀수 중간값이 각각 어떻게 떨어지는가
 *  2. 한쪽 호가가 비면 거부되는가
 *  3. 매수 중간가와 매도 중간가가 **같은 가격을 받아 서로 체결되는가**
 *     (내림으로 방향을 고정한 이유가 이것이다)
 *  4. 정정으로 임의 가격에 놓을 수 없는가
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "market_rules.h"
#include "match.h"
#include "tick_size.h"

#define BASE 10000
#define CAP 64

/* 메인마켓 한복판 */
#define T_MAIN TOD_NS(12, 0, 0)

static order_id_t NEXT_ID = 1;
static ts_t NEXT_TS = T_MAIN;

static order_t req_of(side_t side, price_t price, qty_t qty, order_type_t type)
{
    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = NEXT_TS++;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = type;
    req.market = MARKET_NXT;
    return req;
}

static match_engine_t *nxt_engine(price_t base)
{
    match_engine_t *eng = match_engine_create(base, CAP);
    assert(eng != NULL);
    match_set_rules(eng, &NXT_RULES);
    return eng;
}

static order_id_t rest_at(match_engine_t *eng, side_t side, price_t price,
                          qty_t qty)
{
    exec_result_t res;
    order_t req = req_of(side, price, qty, ORDER_LIMIT);
    assert(match_limit(eng, &req, &res) == ERR_OK);
    assert(res.resting);
    return req.id;
}

/* 중간값이 호가 단위에 딱 떨어지는 경우 */
static void test_even_midpoint(void)
{
    match_engine_t *eng = nxt_engine(BASE);
    const order_book_t *book = match_book(eng);

    /* 9,980 / 10,020 -> 중간 10,000. 10원 단위에 딱 맞는다 */
    rest_at(eng, SIDE_BUY, 9980, 100);
    rest_at(eng, SIDE_SELL, 10020, 100);

    exec_result_t res;
    order_t mid = req_of(SIDE_BUY, 0, 50, ORDER_MIDPOINT); /* 가격은 무시된다 */
    assert(match_limit(eng, &mid, &res) == ERR_OK);
    assert(res.resting);

    assert(book_qty_at(book, SIDE_BUY, 10000) == 50);
    assert(book_best_bid(book) == 10000); /* 기존 매수보다 유리하다 */
    assert(book_best_ask(book) == 10020); /* 매도는 건드리지 않았다 */

    match_engine_destroy(eng);
}

/* 중간값이 호가 단위에 안 떨어지면 내림한다 */
static void test_odd_midpoint_rounds_down(void)
{
    match_engine_t *eng = nxt_engine(BASE);
    const order_book_t *book = match_book(eng);

    /* 10,000 / 10,010 -> 중간 10,005. 10원 단위라 내림해 10,000 */
    rest_at(eng, SIDE_BUY, 10000, 100);
    rest_at(eng, SIDE_SELL, 10010, 100);
    assert(tick_size_of(10005) == 10);

    exec_result_t res;
    order_t mid = req_of(SIDE_BUY, 0, 50, ORDER_MIDPOINT);
    assert(match_limit(eng, &mid, &res) == ERR_OK);

    /* 스프레드가 한 틱이면 내림 결과가 매수호가와 같아진다 */
    assert(book_qty_at(book, SIDE_BUY, 10000) == 150); /* 기존 100 + 50 */
    assert(book_best_bid(book) == 10000);

    match_engine_destroy(eng);
}

/* 홀수 합 — 정수 나눗셈의 절단이 곧 내림이다 */
static void test_odd_sum(void)
{
    /* 1원 단위 구간에서 본다. 기준가 1,000 -> 700~1,300 */
    match_engine_t *eng = nxt_engine(1000);
    const order_book_t *book = match_book(eng);
    assert(tick_size_of(1000) == 1);

    exec_result_t res;
    order_t b = req_of(SIDE_BUY, 1000, 100, ORDER_LIMIT);
    order_t a = req_of(SIDE_SELL, 1003, 100, ORDER_LIMIT);
    assert(match_limit(eng, &b, &res) == ERR_OK);
    assert(match_limit(eng, &a, &res) == ERR_OK);

    /* (1000 + 1003) / 2 = 1001.5 -> 1001 */
    order_t mid = req_of(SIDE_BUY, 0, 10, ORDER_MIDPOINT);
    assert(match_limit(eng, &mid, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_BUY, 1001) == 10);
    assert(book_best_bid(book) == 1001);

    match_engine_destroy(eng);
}

/* 한쪽 호가가 비어 있으면 거부 */
static void test_empty_side_rejected(void)
{
    match_engine_t *eng = nxt_engine(BASE);
    const order_book_t *book = match_book(eng);

    exec_result_t res;

    /* 양쪽 다 비었다 */
    order_t m1 = req_of(SIDE_BUY, 0, 10, ORDER_MIDPOINT);
    assert(match_limit(eng, &m1, &res) == ERR_INVALID_PRICE);
    assert(res.status == STATUS_REJECTED);

    /* 매수만 있다 */
    rest_at(eng, SIDE_BUY, 9900, 100);
    order_t m2 = req_of(SIDE_BUY, 0, 10, ORDER_MIDPOINT);
    assert(match_limit(eng, &m2, &res) == ERR_INVALID_PRICE);

    /* 매도가 생기면 된다 */
    rest_at(eng, SIDE_SELL, 10100, 100);
    order_t m3 = req_of(SIDE_BUY, 0, 10, ORDER_MIDPOINT);
    assert(match_limit(eng, &m3, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 10);

    match_engine_destroy(eng);
}

/*
 * 내림으로 방향을 고정한 이유. 매수 중간가와 매도 중간가가 같은 가격을 받아
 * 서로 체결된다. 편별로 방향이 갈렸다면 둘은 영원히 만나지 못한다.
 */
static void test_two_midpoints_meet(void)
{
    match_engine_t *eng = nxt_engine(BASE);
    const order_book_t *book = match_book(eng);

    /* 10,000 / 10,010 -> 중간 10,005 -> 내림 10,000 */
    rest_at(eng, SIDE_BUY, 10000, 100);
    rest_at(eng, SIDE_SELL, 10010, 100);

    exec_result_t res;
    order_t buy_mid = req_of(SIDE_BUY, 0, 40, ORDER_MIDPOINT);
    assert(match_limit(eng, &buy_mid, &res) == ERR_OK);
    assert(res.resting);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 140);

    /* 매도 중간가도 같은 10,000을 받아 매수 쪽과 체결된다 */
    order_t sell_mid = req_of(SIDE_SELL, 0, 40, ORDER_MIDPOINT);
    assert(match_limit(eng, &sell_mid, &res) == ERR_OK);
    assert(res.status == STATUS_FILLED);
    assert(res.filled_qty == 40);
    assert(res.fills[0].price == 10000);
    /* 먼저 있던 지정가 매수(100)가 시간 우선으로 먼저 체결된다 */
    assert(book_qty_at(book, SIDE_BUY, 10000) == 100);

    match_engine_destroy(eng);
}

/* 접수 시점 고정 — 호가가 바뀌어도 이미 등록된 중간가는 움직이지 않는다 */
static void test_price_is_frozen(void)
{
    match_engine_t *eng = nxt_engine(BASE);
    const order_book_t *book = match_book(eng);

    rest_at(eng, SIDE_BUY, 9980, 100);
    order_id_t ask = rest_at(eng, SIDE_SELL, 10020, 100);

    exec_result_t res;
    order_t mid = req_of(SIDE_BUY, 0, 50, ORDER_MIDPOINT);
    assert(match_limit(eng, &mid, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 50);

    /* 매도호가를 더 비싸게 바꾼다 — 지속 갱신이었다면 10,010이 됐을 것이다 */
    assert(match_cancel(eng, ask, NEXT_TS++, &res) == ERR_OK);
    rest_at(eng, SIDE_SELL, 10040, 100);

    /* 등록된 중간가 주문은 그대로 10,000에 있다 */
    assert(book_qty_at(book, SIDE_BUY, 10000) == 50);
    assert(book_qty_at(book, SIDE_BUY, 10010) == 0);

    match_engine_destroy(eng);
}

/* 메인마켓 밖에서는 접수되지 않는다 */
static void test_session_restricted(void)
{
    match_engine_t *eng = nxt_engine(BASE);

    /* 호가를 미리 채워 둔다 (메인마켓 시각으로) */
    rest_at(eng, SIDE_BUY, 9980, 100);
    rest_at(eng, SIDE_SELL, 10020, 100);

    exec_result_t res;
    const ts_t outside[] = {
        TOD_NS(8, 30, 0),  /* 프리마켓 */
        TOD_NS(18, 0, 0),  /* 애프터마켓 */
        TOD_NS(9, 0, 0),   /* 오전 휴장 */
        TOD_NS(15, 25, 0), /* 오후 휴장 */
        TOD_NS(21, 0, 0),  /* 폐장 */
    };
    for (size_t i = 0; i < sizeof(outside) / sizeof(outside[0]); i++) {
        order_t mid = req_of(SIDE_BUY, 0, 10, ORDER_MIDPOINT);
        mid.ts = outside[i];
        int rc = match_limit(eng, &mid, &res);
        assert(rc == ERR_NOT_SUPPORTED || rc == ERR_MARKET_CLOSED);
        assert(res.status == STATUS_REJECTED);
    }

    /* 메인마켓에서는 된다 */
    order_t ok = req_of(SIDE_BUY, 0, 10, ORDER_MIDPOINT);
    ok.ts = T_MAIN;
    assert(match_limit(eng, &ok, &res) == ERR_OK);

    /* KRX는 시각과 무관하게 중간가를 받지 않는다 */
    match_engine_t *krx = match_engine_create(BASE, CAP);
    assert(krx != NULL);
    match_set_rules(krx, &KRX_RULES);
    order_t on_krx = req_of(SIDE_BUY, 0, 10, ORDER_MIDPOINT);
    on_krx.ts = T_MAIN;
    on_krx.market = MARKET_KRX;
    assert(match_limit(krx, &on_krx, &res) == ERR_NOT_SUPPORTED);
    match_engine_destroy(krx);

    match_engine_destroy(eng);
}

/* 정정으로 임의 가격에 놓을 수 없다 — 요청 가격이 무시되고 다시 계산된다 */
static void test_modify_cannot_set_price(void)
{
    match_engine_t *eng = nxt_engine(BASE);
    const order_book_t *book = match_book(eng);

    rest_at(eng, SIDE_BUY, 9980, 100);
    rest_at(eng, SIDE_SELL, 10020, 100);

    exec_result_t res;
    order_t mid = req_of(SIDE_BUY, 0, 50, ORDER_MIDPOINT);
    assert(match_limit(eng, &mid, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 50);

    /* 9,900으로 옮기려 하면 거절된다 — 가격이 규칙에서 나오는 유형이다 */
    assert(match_modify(eng, mid.id, 9900, 50, NEXT_TS++, &res) ==
           ERR_NOT_SUPPORTED);
    assert(res.status == STATUS_REJECTED);
    assert(book_qty_at(book, SIDE_BUY, 9900) == 0);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 50); /* 그대로 */

    /* 같은 가격으로 수량만 바꾸는 정정은 된다 */
    assert(match_modify(eng, mid.id, 10000, 20, NEXT_TS++, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 20);

    /* 지정가 주문은 여전히 원하는 가격으로 옮길 수 있다 */
    order_t lim = req_of(SIDE_BUY, 9950, 10, ORDER_LIMIT);
    assert(match_limit(eng, &lim, &res) == ERR_OK);
    assert(match_modify(eng, lim.id, 9940, 10, NEXT_TS++, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_BUY, 9940) == 10);

    match_engine_destroy(eng);
}

/* 규칙 함수를 직접 — 호가 공백과 계산식 */
static void test_resolve_directly(void)
{
    match_engine_t *eng = nxt_engine(BASE);
    const order_book_t *book = match_book(eng);

    order_t mid = req_of(SIDE_BUY, 12345, 10, ORDER_MIDPOINT);
    assert(NXT_RULES.resolve_price(book, &mid) == BOOK_PRICE_NONE);

    rest_at(eng, SIDE_BUY, 9900, 10);
    rest_at(eng, SIDE_SELL, 10100, 10);
    /* (9900 + 10100) / 2 = 10000 */
    assert(NXT_RULES.resolve_price(book, &mid) == 10000);

    /* 중간가가 아닌 유형은 실려 온 값 그대로 */
    order_t lim = req_of(SIDE_BUY, 12345, 10, ORDER_LIMIT);
    assert(NXT_RULES.resolve_price(book, &lim) == 12345);

    match_engine_destroy(eng);
}

int main(void)
{
    test_even_midpoint();
    test_odd_midpoint_rounds_down();
    test_odd_sum();
    test_empty_side_rejected();
    test_two_midpoints_meet();
    test_price_is_frozen();
    test_session_restricted();
    test_modify_cannot_set_price();
    test_resolve_directly();
    return 0;
}
