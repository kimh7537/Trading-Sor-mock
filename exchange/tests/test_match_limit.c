/*
 * T1-08 지정가 매칭.
 *
 * 최종 산출물이 "집행 전략별 평균 체결 단가"이므로, 이 테스트가 실제로 지키는 것은
 * notional / filled_qty 가 손으로 계산한 값과 정확히 같은가다.
 * 여러 레벨을 가로지를 때 각 체결이 제 가격으로 찍혔는지 한 건씩 대조한다.
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "match.h"

#define BASE 10000
#define CAP 256

static order_id_t NEXT_ID = 1;
static ts_t NEXT_TS = 1000;

static order_t req_of(side_t side, price_t price, qty_t qty)
{
    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = NEXT_TS++;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = ORDER_LIMIT;
    req.market = MARKET_KRX;
    return req;
}

/* 체결되지 않을 가격에 주문을 하나 놓는다. */
static order_id_t rest_at(match_engine_t *eng, side_t side, price_t price,
                          qty_t qty)
{
    exec_result_t res;
    order_t req = req_of(side, price, qty);
    assert(match_limit(eng, &req, &res) == ERR_OK);
    assert(res.filled_qty == 0);
    assert(res.resting);
    assert(res.status == STATUS_NEW);
    return req.id;
}

/* 체결 없이 전량 등록 */
static void test_no_cross(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_BUY, 9900, 100);
    rest_at(eng, SIDE_SELL, 10100, 100);

    const order_book_t *book = match_book(eng);
    assert(book_best_bid(book) == 9900);
    assert(book_best_ask(book) == 10100);
    assert(book_qty_at(book, SIDE_BUY, 9900) == 100);
    assert(book_qty_at(book, SIDE_SELL, 10100) == 100);

    match_engine_destroy(eng);
}

/* 즉시 전량 체결 */
static void test_full_fill(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    order_id_t maker = rest_at(eng, SIDE_SELL, 10100, 100);

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10100, 100);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.status == STATUS_FILLED);
    assert(res.filled_qty == 100);
    assert(res.remaining_qty == 0);
    assert(!res.resting);
    assert(res.fill_count == 1);
    assert(res.fills[0].price == 10100);
    assert(res.fills[0].qty == 100);
    assert(res.fills[0].maker_id == maker);
    assert(res.fills[0].taker_id == taker.id);
    assert(res.fills[0].ts == taker.ts);
    assert(res.notional == 10100 * 100);

    const order_book_t *book = match_book(eng);
    assert(book_best_ask(book) == BOOK_PRICE_NONE); /* 상대가 다 빠졌다 */
    assert(book_best_bid(book) == BOOK_PRICE_NONE); /* taker는 남지 않았다 */

    match_engine_destroy(eng);
}

/*
 * 지정가보다 유리한 호가가 있으면 그 가격에 체결된다.
 * 지정가는 상한일 뿐 체결 가격이 아니다.
 */
static void test_price_is_makers(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10000, 50); /* 지정가보다 싼 매도 */

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10100, 50); /* 10,100까지 낼 의사 */
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.filled_qty == 50);
    assert(res.fills[0].price == 10000); /* 지정가가 아니라 상대 호가 */
    assert(res.notional == 10000 * 50);

    match_engine_destroy(eng);
}

/* 부분 체결 후 잔량 등록 */
static void test_partial_then_rest(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10100, 30);

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10100, 100);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.status == STATUS_PARTIAL);
    assert(res.filled_qty == 30);
    assert(res.remaining_qty == 70);
    assert(res.resting);

    const order_book_t *book = match_book(eng);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);
    assert(book_best_bid(book) == 10100);             /* 잔량이 매수로 남았다 */
    assert(book_qty_at(book, SIDE_BUY, 10100) == 70); /* 잔량만 */

    /* 등록된 주문은 원 주문 수량을 보존해야 한다 — 이게 없으면 나중에
     * "이 주문의 평균 체결 단가"를 낼 수 없다. 호가창을 직접 들여다본다. */
    const order_t *resting = book_front((order_book_t *)book, SIDE_BUY, 10100);
    assert(resting != NULL);
    assert(resting->id == taker.id);
    assert(resting->qty == 100);       /* 원 수량 */
    assert(resting->filled_qty == 30); /* 이미 체결된 분 */
    assert(order_remaining_qty(resting) == 70);

    match_engine_destroy(eng);
}

/* 상대가 더 크면 상대가 부분 체결로 남는다 */
static void test_maker_partial(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10100, 100);

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10100, 40);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.status == STATUS_FILLED);
    assert(res.filled_qty == 40);

    const order_book_t *book = match_book(eng);
    assert(book_best_ask(book) == 10100);
    assert(book_qty_at(book, SIDE_SELL, 10100) == 60); /* 잔량 */

    match_engine_destroy(eng);
}

/*
 * 여러 가격 레벨을 가로지르는 체결. 가격 우선으로 싼 매도부터 먹어야 한다.
 * 10,000 x 100 + 10,010 x 100 + 10,020 x 50 = 2,502,000, 수량 250, 평균 10,008.
 */
static void test_multi_level_average(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    /* 일부러 비싼 것부터 넣는다 — 가격 우선이 삽입 순서에 안 휘둘리는지 본다 */
    rest_at(eng, SIDE_SELL, 10020, 100);
    rest_at(eng, SIDE_SELL, 10000, 100);
    rest_at(eng, SIDE_SELL, 10010, 100);

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10020, 250);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.status == STATUS_FILLED);
    assert(res.filled_qty == 250);
    assert(res.fill_count == 3);

    /* 싼 순서대로 */
    assert(res.fills[0].price == 10000 && res.fills[0].qty == 100);
    assert(res.fills[1].price == 10010 && res.fills[1].qty == 100);
    assert(res.fills[2].price == 10020 && res.fills[2].qty == 50);

    assert(res.notional ==
           (int64_t)10000 * 100 + (int64_t)10010 * 100 + (int64_t)10020 * 50);
    assert(res.notional == 2502000);
    assert(res.notional / res.filled_qty == 10008); /* 평균 체결 단가 */

    const order_book_t *book = match_book(eng);
    assert(book_best_ask(book) == 10020);
    assert(book_qty_at(book, SIDE_SELL, 10020) == 50);

    match_engine_destroy(eng);
}

/* 지정가 밖의 레벨은 건드리지 않는다 */
static void test_stops_at_limit(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10000, 50);
    rest_at(eng, SIDE_SELL, 10050, 50); /* 지정가 밖 */

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10000, 200);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.filled_qty == 50); /* 10,050은 안 먹는다 */
    assert(res.remaining_qty == 150);
    assert(res.resting);

    const order_book_t *book = match_book(eng);
    assert(book_qty_at(book, SIDE_SELL, 10050) == 50);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 150);

    match_engine_destroy(eng);
}

/* 같은 가격 안에서는 먼저 낸 주문이 먼저 체결된다 */
static void test_time_priority(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    order_id_t first = rest_at(eng, SIDE_SELL, 10100, 30);
    order_id_t second = rest_at(eng, SIDE_SELL, 10100, 30);
    order_id_t third = rest_at(eng, SIDE_SELL, 10100, 30);

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10100, 75);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.fill_count == 3);
    assert(res.fills[0].maker_id == first && res.fills[0].qty == 30);
    assert(res.fills[1].maker_id == second && res.fills[1].qty == 30);
    assert(res.fills[2].maker_id == third && res.fills[2].qty == 15);

    /* 세 번째 주문의 잔량 15가 남는다 */
    const order_book_t *book = match_book(eng);
    assert(book_qty_at(book, SIDE_SELL, 10100) == 15);

    match_engine_destroy(eng);
}

/* 매도 지정가는 매수 최우선호가가 지정가 이상일 때 체결된다 */
static void test_sell_side(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_BUY, 9900, 100);
    rest_at(eng, SIDE_BUY, 9950, 100); /* 더 비싼 매수가 먼저 체결돼야 한다 */

    exec_result_t res;
    order_t taker = req_of(SIDE_SELL, 9900, 150);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.status == STATUS_FILLED);
    assert(res.fill_count == 2);
    assert(res.fills[0].price == 9950 && res.fills[0].qty == 100); /* 비싼 쪽부터 */
    assert(res.fills[1].price == 9900 && res.fills[1].qty == 50);
    assert(res.notional == (int64_t)9950 * 100 + (int64_t)9900 * 50);

    const order_book_t *book = match_book(eng);
    assert(book_best_bid(book) == 9900);
    assert(book_qty_at(book, SIDE_BUY, 9900) == 50);

    match_engine_destroy(eng);
}

/* 거부되는 주문은 호가창을 전혀 바꾸지 않는다 */
static void test_rejects(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t live = rest_at(eng, SIDE_BUY, 9900, 100);
    exec_result_t res;

    /* 제한폭 밖 */
    order_t far = req_of(SIDE_BUY, book_price_high(book) + 10, 10);
    assert(match_limit(eng, &far, &res) == ERR_PRICE_LIMIT);
    assert(res.status == STATUS_REJECTED);

    /* 호가 단위 어긋남 (10,000원대는 10원 단위) */
    order_t offtick = req_of(SIDE_BUY, 10005, 10);
    assert(match_limit(eng, &offtick, &res) == ERR_INVALID_TICK);

    /* 수량 범위 밖 */
    order_t zero = req_of(SIDE_BUY, 10000, 0);
    assert(match_limit(eng, &zero, &res) == ERR_INVALID_QTY);
    order_t huge = req_of(SIDE_BUY, 10000, QTY_MAX + 1);
    assert(match_limit(eng, &huge, &res) == ERR_INVALID_QTY);

    /* 주문번호 중복 */
    order_t dup = req_of(SIDE_BUY, 9900, 10);
    dup.id = live;
    assert(match_limit(eng, &dup, &res) == ERR_DUPLICATE);

    /* 주문번호 0 */
    order_t noid = req_of(SIDE_BUY, 9900, 10);
    noid.id = ORDER_ID_INVALID;
    assert(match_limit(eng, &noid, &res) == ERR_INVALID_ARG);

    assert(match_limit(NULL, &dup, &res) == ERR_NULL_PTR);
    assert(match_limit(eng, NULL, &res) == ERR_NULL_PTR);
    assert(match_limit(eng, &dup, NULL) == ERR_NULL_PTR);

    /* 처음 놓은 주문 하나만 그대로 있다 */
    assert(book_best_bid(book) == 9900);
    assert(book_qty_at(book, SIDE_BUY, 9900) == 100);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

/* 체결 목록이 넘치면 잘리지만 집계는 정확하다 */
static void test_fill_truncation(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    const int n = EXEC_FILLS_MAX + 10;
    for (int i = 0; i < n; i++) {
        rest_at(eng, SIDE_SELL, 10100, 1);
    }

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10100, (qty_t)n);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    assert(res.fill_count == EXEC_FILLS_MAX);
    assert(res.truncated);
    assert(res.filled_qty == n); /* 집계는 정확 */
    assert(res.notional == (int64_t)10100 * n);
    assert(res.notional / res.filled_qty == 10100); /* 평균 단가도 정확 */

    match_engine_destroy(eng);
}

int main(void)
{
    test_no_cross();
    test_full_fill();
    test_price_is_makers();
    test_partial_then_rest();
    test_maker_partial();
    test_multi_level_average();
    test_stops_at_limit();
    test_time_priority();
    test_sell_side();
    test_rejects();
    test_fill_truncation();
    return 0;
}
