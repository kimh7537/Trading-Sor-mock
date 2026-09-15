/*
 * T1-09 시장가 매칭.
 *
 * 지정가와 갈리는 지점 셋만 본다.
 *  1. 가격 제한이 없다 — 지정가라면 멈췄을 레벨까지 먹는다
 *  2. 잔량이 호가창에 남지 않는다
 *  3. 반대 호가가 없으면 전량 거부다
 * 다중 레벨 평균 단가는 여기서도 손계산과 대조한다.
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "match.h"

#define BASE 10000
#define CAP 256

static order_id_t NEXT_ID = 1;
static ts_t NEXT_TS = 1000;

static order_t req_of(side_t side, price_t price, qty_t qty, order_type_t type)
{
    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = NEXT_TS++;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = type;
    req.market = MARKET_KRX;
    return req;
}

static void rest_at(match_engine_t *eng, side_t side, price_t price, qty_t qty)
{
    exec_result_t res;
    order_t req = req_of(side, price, qty, ORDER_LIMIT);
    assert(match_limit(eng, &req, &res) == ERR_OK);
    assert(res.filled_qty == 0 && res.resting);
}

/* 빈 호가창이면 전량 거부. 체결도 등록도 없다 */
static void test_empty_book_rejected(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    exec_result_t res;
    order_t buy = req_of(SIDE_BUY, 0, 100, ORDER_MARKET);
    assert(match_market(eng, &buy, &res) == ERR_NO_LIQUIDITY);
    assert(res.status == STATUS_REJECTED);
    assert(res.filled_qty == 0);
    assert(!res.resting);
    assert(res.remaining_qty == 100);

    /* 같은 편 호가만 있어도 반대 호가가 없으면 거부다 */
    rest_at(eng, SIDE_BUY, 9900, 100);
    order_t buy2 = req_of(SIDE_BUY, 0, 100, ORDER_MARKET);
    assert(match_market(eng, &buy2, &res) == ERR_NO_LIQUIDITY);

    assert(book_best_bid(book) == 9900); /* 호가창은 그대로 */
    assert(book_qty_at(book, SIDE_BUY, 9900) == 100);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

/* 전량 체결 */
static void test_full_fill(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10100, 100);

    exec_result_t res;
    order_t buy = req_of(SIDE_BUY, 0, 100, ORDER_MARKET);
    assert(match_market(eng, &buy, &res) == ERR_OK);

    assert(res.status == STATUS_FILLED);
    assert(res.filled_qty == 100);
    assert(res.remaining_qty == 0);
    assert(!res.resting);
    assert(res.fills[0].price == 10100);

    const order_book_t *book = match_book(eng);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

/* 부분 체결 후 잔량 취소 — 호가창에 등록되지 않는다 */
static void test_partial_then_cancel(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10100, 30);

    exec_result_t res;
    /* price에 등록 가능한 값을 채워 둔다 — 잘못 등록하는 구현이면 여기서 드러난다.
     * 0으로 두면 제한폭에 막혀 버그가 가려진다. */
    order_t buy = req_of(SIDE_BUY, 10100, 100, ORDER_MARKET);
    assert(match_market(eng, &buy, &res) == ERR_OK);

    assert(res.status == STATUS_PARTIAL);
    assert(res.filled_qty == 30);
    assert(res.remaining_qty == 70);
    assert(!res.resting); /* 지정가였다면 여기서 등록됐다 */

    const order_book_t *book = match_book(eng);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);
    assert(book_best_bid(book) == BOOK_PRICE_NONE); /* 잔량이 남지 않았다 */

    match_engine_destroy(eng);
}

/*
 * 다중 레벨 소진. 지정가라면 10,000에서 멈췄을 주문이 끝까지 먹는다.
 * 10,000x100 + 10,010x100 + 10,020x100 = 3,003,000, 수량 300, 평균 10,010.
 */
static void test_multi_level_average(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10020, 100);
    rest_at(eng, SIDE_SELL, 10000, 100);
    rest_at(eng, SIDE_SELL, 10010, 100);

    exec_result_t res;
    /* price에 최우선호가와 같은 값을 넣어 둔다 — 무시돼야 한다 */
    order_t buy = req_of(SIDE_BUY, 10000, 300, ORDER_MARKET);
    assert(match_market(eng, &buy, &res) == ERR_OK);

    assert(res.status == STATUS_FILLED);
    assert(res.filled_qty == 300);
    assert(res.fill_count == 3);
    assert(res.fills[0].price == 10000);
    assert(res.fills[1].price == 10010);
    assert(res.fills[2].price == 10020);
    assert(res.notional == 3003000);
    assert(res.notional / res.filled_qty == 10010);

    const order_book_t *book = match_book(eng);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

/* 매도 시장가는 매수 호가를 비싼 것부터 먹는다 */
static void test_sell_side(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_BUY, 9900, 50);
    rest_at(eng, SIDE_BUY, 9950, 50);

    exec_result_t res;
    order_t sell = req_of(SIDE_SELL, 0, 80, ORDER_MARKET);
    assert(match_market(eng, &sell, &res) == ERR_OK);

    assert(res.filled_qty == 80);
    assert(res.fills[0].price == 9950 && res.fills[0].qty == 50);
    assert(res.fills[1].price == 9900 && res.fills[1].qty == 30);
    assert(res.notional == (int64_t)9950 * 50 + (int64_t)9900 * 30);

    const order_book_t *book = match_book(eng);
    assert(book_best_bid(book) == 9900);
    assert(book_qty_at(book, SIDE_BUY, 9900) == 20);

    match_engine_destroy(eng);
}

/* 인자 검증 */
static void test_rejects(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10100, 100);
    exec_result_t res;

    order_t zero = req_of(SIDE_BUY, 0, 0, ORDER_MARKET);
    assert(match_market(eng, &zero, &res) == ERR_INVALID_QTY);

    order_t noid = req_of(SIDE_BUY, 0, 10, ORDER_MARKET);
    noid.id = ORDER_ID_INVALID;
    assert(match_market(eng, &noid, &res) == ERR_INVALID_ARG);

    assert(match_market(NULL, &zero, &res) == ERR_NULL_PTR);
    assert(match_market(eng, NULL, &res) == ERR_NULL_PTR);
    assert(match_market(eng, &zero, NULL) == ERR_NULL_PTR);

    const order_book_t *book = match_book(eng);
    assert(book_qty_at(book, SIDE_SELL, 10100) == 100); /* 안 건드렸다 */

    match_engine_destroy(eng);
}

int main(void)
{
    test_empty_book_rejected();
    test_full_fill();
    test_partial_then_cancel();
    test_multi_level_average();
    test_sell_side();
    test_rejects();
    return 0;
}
