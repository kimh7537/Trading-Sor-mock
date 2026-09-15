/*
 * T1-10 IOC / FOK.
 *
 * FOK의 핵심은 "실패했을 때 호가창이 전혀 안 바뀌었는가"다. 부분 체결 후 되돌리는
 * 구현이면 상대 주문들의 시간 우선순위가 어긋나므로, 실패 전후로 각 레벨의 잔량뿐
 * 아니라 맨 앞 주문의 주문번호까지 같은지 본다.
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

static order_id_t rest_at(match_engine_t *eng, side_t side, price_t price,
                          qty_t qty)
{
    exec_result_t res;
    order_t req = req_of(side, price, qty, ORDER_LIMIT);
    assert(match_limit(eng, &req, &res) == ERR_OK);
    assert(res.filled_qty == 0 && res.resting);
    return req.id;
}

/* 호가창 전체를 찍어 둔다. FOK 실패 전후 비교용. */
typedef struct {
    level_view_t bid[8];
    level_view_t ask[8];
    int nbid;
    int nask;
    order_id_t bid_front;
    order_id_t ask_front;
} snapshot_t;

static snapshot_t snap(const order_book_t *book)
{
    snapshot_t s;
    s.nbid = book_snapshot(book, SIDE_BUY, 8, s.bid);
    s.nask = book_snapshot(book, SIDE_SELL, 8, s.ask);

    /* book_front는 비const를 받는다. 읽기만 하므로 테스트에서만 캐스팅한다. */
    order_book_t *mut = (order_book_t *)book;
    const order_t *b =
        (s.nbid > 0) ? book_front(mut, SIDE_BUY, s.bid[0].price) : NULL;
    const order_t *a =
        (s.nask > 0) ? book_front(mut, SIDE_SELL, s.ask[0].price) : NULL;
    s.bid_front = (b != NULL) ? b->id : ORDER_ID_INVALID;
    s.ask_front = (a != NULL) ? a->id : ORDER_ID_INVALID;
    return s;
}

static void assert_same(const snapshot_t *a, const snapshot_t *b)
{
    assert(a->nbid == b->nbid && a->nask == b->nask);
    for (int i = 0; i < a->nbid; i++) {
        assert(a->bid[i].price == b->bid[i].price);
        assert(a->bid[i].total_qty == b->bid[i].total_qty);
        assert(a->bid[i].order_count == b->bid[i].order_count);
    }
    for (int i = 0; i < a->nask; i++) {
        assert(a->ask[i].price == b->ask[i].price);
        assert(a->ask[i].total_qty == b->ask[i].total_qty);
        assert(a->ask[i].order_count == b->ask[i].order_count);
    }
    /* 시간 우선순위까지 그대로여야 한다 */
    assert(a->bid_front == b->bid_front);
    assert(a->ask_front == b->ask_front);
}

/* IOC 부분 체결 — 붙는 만큼 붙고 잔량은 사라진다 */
static void test_ioc_partial(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10000, 30);
    rest_at(eng, SIDE_SELL, 10050, 100); /* 지정가 밖 */

    exec_result_t res;
    order_t ioc = req_of(SIDE_BUY, 10000, 100, ORDER_IOC);
    assert(match_ioc(eng, &ioc, &res) == ERR_OK);

    assert(res.status == STATUS_PARTIAL);
    assert(res.filled_qty == 30);
    assert(res.remaining_qty == 70);
    assert(!res.resting);
    assert(res.notional == 10000 * 30);

    const order_book_t *book = match_book(eng);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);     /* 잔량이 남지 않았다 */
    assert(book_qty_at(book, SIDE_SELL, 10050) == 100); /* 지정가 밖은 안 건드림 */

    match_engine_destroy(eng);
}

/* IOC 전량 체결 */
static void test_ioc_full(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10000, 50);
    rest_at(eng, SIDE_SELL, 10010, 50);

    exec_result_t res;
    order_t ioc = req_of(SIDE_BUY, 10010, 100, ORDER_IOC);
    assert(match_ioc(eng, &ioc, &res) == ERR_OK);

    assert(res.status == STATUS_FILLED);
    assert(res.filled_qty == 100);
    assert(res.notional == (int64_t)10000 * 50 + (int64_t)10010 * 50);

    match_engine_destroy(eng);
}

/* IOC가 한 건도 못 붙으면 거부다 */
static void test_ioc_no_fill(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    rest_at(eng, SIDE_SELL, 10100, 100); /* 지정가보다 비싸다 */

    exec_result_t res;
    order_t ioc = req_of(SIDE_BUY, 10000, 100, ORDER_IOC);
    assert(match_ioc(eng, &ioc, &res) == ERR_NO_LIQUIDITY);
    assert(res.status == STATUS_REJECTED);
    assert(res.filled_qty == 0);
    assert(!res.resting);

    assert(book_qty_at(book, SIDE_SELL, 10100) == 100);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);

    /* 빈 호가창도 마찬가지 */
    match_engine_t *empty = match_engine_create(BASE, CAP);
    assert(empty != NULL);
    order_t ioc2 = req_of(SIDE_BUY, 10000, 10, ORDER_IOC);
    assert(match_ioc(empty, &ioc2, &res) == ERR_NO_LIQUIDITY);
    match_engine_destroy(empty);

    match_engine_destroy(eng);
}

/* FOK 성공 — 여러 레벨을 걸쳐도 전량이면 체결된다 */
static void test_fok_success(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10000, 40);
    rest_at(eng, SIDE_SELL, 10010, 40);
    rest_at(eng, SIDE_SELL, 10020, 40);

    exec_result_t res;
    order_t fok = req_of(SIDE_BUY, 10020, 100, ORDER_FOK);
    assert(match_fok(eng, &fok, &res) == ERR_OK);

    assert(res.status == STATUS_FILLED);
    assert(res.filled_qty == 100);
    assert(res.remaining_qty == 0);
    assert(!res.resting);
    assert(res.fill_count == 3);
    assert(res.notional ==
           (int64_t)10000 * 40 + (int64_t)10010 * 40 + (int64_t)10020 * 20);

    const order_book_t *book = match_book(eng);
    assert(book_qty_at(book, SIDE_SELL, 10020) == 20); /* 남은 20 */

    match_engine_destroy(eng);
}

/* 딱 맞게 전량 — 경계 */
static void test_fok_exact(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    rest_at(eng, SIDE_SELL, 10000, 50);
    rest_at(eng, SIDE_SELL, 10010, 50);

    exec_result_t res;
    order_t fok = req_of(SIDE_BUY, 10010, 100, ORDER_FOK);
    assert(match_fok(eng, &fok, &res) == ERR_OK);
    assert(res.filled_qty == 100);

    const order_book_t *book = match_book(eng);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

/* FOK 실패 — 호가창이 전혀 바뀌지 않는다 */
static void test_fok_failure_leaves_book(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    /* 같은 레벨에 여러 주문을 둬서 시간 우선순위가 관측되게 한다 */
    rest_at(eng, SIDE_SELL, 10000, 20);
    rest_at(eng, SIDE_SELL, 10000, 20);
    rest_at(eng, SIDE_SELL, 10010, 20);
    rest_at(eng, SIDE_SELL, 10050, 500); /* 지정가 밖이라 세면 안 된다 */
    rest_at(eng, SIDE_BUY, 9900, 100);

    snapshot_t before = snap(book);

    exec_result_t res;
    /* 지정가 안의 잔량은 60뿐인데 100을 요구한다 */
    order_t fok = req_of(SIDE_BUY, 10010, 100, ORDER_FOK);
    assert(match_fok(eng, &fok, &res) == ERR_NO_LIQUIDITY);

    assert(res.status == STATUS_REJECTED);
    assert(res.filled_qty == 0);
    assert(res.fill_count == 0);
    assert(res.notional == 0);
    assert(res.remaining_qty == 100);
    assert(!res.resting);

    snapshot_t after = snap(book);
    assert_same(&before, &after); /* 잔량도 순서도 그대로 */

    match_engine_destroy(eng);
}

/* 빈 호가창 FOK */
static void test_fok_empty_book(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t fok = req_of(SIDE_BUY, 10000, 10, ORDER_FOK);
    assert(match_fok(eng, &fok, &res) == ERR_NO_LIQUIDITY);
    assert(res.filled_qty == 0);

    const order_book_t *book = match_book(eng);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

/* 매도 FOK */
static void test_fok_sell(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    rest_at(eng, SIDE_BUY, 9950, 40);
    rest_at(eng, SIDE_BUY, 9900, 40);
    rest_at(eng, SIDE_BUY, 9800, 500); /* 지정가 밖 */

    exec_result_t res;
    order_t fail = req_of(SIDE_SELL, 9900, 100, ORDER_FOK);
    assert(match_fok(eng, &fail, &res) == ERR_NO_LIQUIDITY); /* 80뿐이다 */
    assert(book_qty_at(book, SIDE_BUY, 9950) == 40);

    order_t ok = req_of(SIDE_SELL, 9900, 80, ORDER_FOK);
    assert(match_fok(eng, &ok, &res) == ERR_OK);
    assert(res.filled_qty == 80);
    assert(res.fills[0].price == 9950); /* 비싼 매수부터 */
    assert(res.fills[1].price == 9900);
    assert(book_qty_at(book, SIDE_BUY, 9800) == 500); /* 안 건드렸다 */

    match_engine_destroy(eng);
}

/* book_qty_up_to 자체 */
static void test_qty_up_to(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    rest_at(eng, SIDE_SELL, 10000, 10);
    rest_at(eng, SIDE_SELL, 10010, 20);
    rest_at(eng, SIDE_SELL, 10020, 30);

    assert(book_qty_up_to(book, SIDE_SELL, 10000, 1000) == 10);
    assert(book_qty_up_to(book, SIDE_SELL, 10010, 1000) == 30);
    assert(book_qty_up_to(book, SIDE_SELL, 10020, 1000) == 60);
    assert(book_qty_up_to(book, SIDE_SELL, BOOK_PRICE_NONE, 1000) == 60);
    assert(book_qty_up_to(book, SIDE_SELL, 9990, 1000) == 0); /* 다 비싸다 */

    /* want에 도달하면 거기서 멈추고 want를 돌려준다 */
    assert(book_qty_up_to(book, SIDE_SELL, 10020, 25) == 25);
    assert(book_qty_up_to(book, SIDE_SELL, 10020, 60) == 60);
    assert(book_qty_up_to(book, SIDE_SELL, 10020, 61) == 60);

    assert(book_qty_up_to(NULL, SIDE_SELL, 10020, 10) == 0);
    assert(book_qty_up_to(book, SIDE_SELL, 10020, 0) == 0);

    match_engine_destroy(eng);
}

/* 인자 검증 */
static void test_rejects(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    rest_at(eng, SIDE_SELL, 10000, 100);
    exec_result_t res;

    order_t offtick = req_of(SIDE_BUY, 10005, 10, ORDER_IOC);
    assert(match_ioc(eng, &offtick, &res) == ERR_INVALID_TICK);
    assert(match_fok(eng, &offtick, &res) == ERR_INVALID_TICK);

    order_t far = req_of(SIDE_BUY, book_price_high(book) + 10, 10, ORDER_FOK);
    assert(match_ioc(eng, &far, &res) == ERR_PRICE_LIMIT);
    assert(match_fok(eng, &far, &res) == ERR_PRICE_LIMIT);

    order_t zero = req_of(SIDE_BUY, 10000, 0, ORDER_IOC);
    assert(match_ioc(eng, &zero, &res) == ERR_INVALID_QTY);
    assert(match_fok(eng, &zero, &res) == ERR_INVALID_QTY);

    assert(match_ioc(NULL, &zero, &res) == ERR_NULL_PTR);
    assert(match_ioc(eng, NULL, &res) == ERR_NULL_PTR);
    assert(match_ioc(eng, &zero, NULL) == ERR_NULL_PTR);
    assert(match_fok(NULL, &zero, &res) == ERR_NULL_PTR);
    assert(match_fok(eng, NULL, &res) == ERR_NULL_PTR);
    assert(match_fok(eng, &zero, NULL) == ERR_NULL_PTR);

    assert(book_qty_at(book, SIDE_SELL, 10000) == 100);

    match_engine_destroy(eng);
}

int main(void)
{
    test_ioc_partial();
    test_ioc_full();
    test_ioc_no_fill();
    test_fok_success();
    test_fok_exact();
    test_fok_failure_leaves_book();
    test_fok_empty_book();
    test_fok_sell();
    test_qty_up_to();
    test_rejects();
    return 0;
}
