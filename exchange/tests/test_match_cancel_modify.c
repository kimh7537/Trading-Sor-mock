/*
 * T1-11 취소와 정정.
 *
 * "시간 우선순위를 잃었는가"는 잔량으로는 안 보인다. 같은 가격에 세 주문을 세워 두고
 * 하나를 정정한 뒤 큐 순서를 읽고, 실제로 체결을 흘려서 누가 먼저 체결되는지까지 본다.
 * 그게 우선순위의 유일한 관측 가능한 정의다.
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

static order_id_t rest_at(match_engine_t *eng, side_t side, price_t price,
                          qty_t qty)
{
    exec_result_t res;
    order_t req = req_of(side, price, qty);
    assert(match_limit(eng, &req, &res) == ERR_OK);
    assert(res.resting);
    return req.id;
}

/* 그 가격 레벨의 체결 순서를 주문번호로 읽어 온다 */
static int queue_of(const order_book_t *book, side_t side, price_t price,
                    order_id_t *out, int max)
{
    order_book_t *mut = (order_book_t *)book; /* 읽기만 한다 */
    int n = 0;
    for (const order_t *o = book_front(mut, side, price); o != NULL && n < max;
         o = o->next) {
        out[n++] = o->id;
    }
    return n;
}

/* 취소 — 잔량이 사라지고 최우선호가가 갱신된다 */
static void test_cancel(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t a = rest_at(eng, SIDE_BUY, 9950, 100);
    rest_at(eng, SIDE_BUY, 9900, 50);

    exec_result_t res;
    assert(match_cancel(eng, a, NEXT_TS++, &res) == ERR_OK);
    assert(res.status == STATUS_CANCELED);
    assert(res.remaining_qty == 100); /* 취소된 잔량 */
    assert(res.filled_qty == 0);
    assert(!res.resting);

    assert(book_qty_at(book, SIDE_BUY, 9950) == 0);
    assert(book_best_bid(book) == 9900); /* 다음 호가로 내려갔다 */

    /* 두 번 취소는 안 된다 */
    assert(match_cancel(eng, a, NEXT_TS++, &res) == ERR_NOT_FOUND);
    assert(match_cancel(eng, 999999, NEXT_TS++, &res) == ERR_NOT_FOUND);
    assert(match_cancel(NULL, a, 0, &res) == ERR_NULL_PTR);
    assert(match_cancel(eng, a, 0, NULL) == ERR_NULL_PTR);

    match_engine_destroy(eng);
}

/* 부분 체결된 주문의 취소 — 남은 잔량만 취소된다 */
static void test_cancel_partially_filled(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t resting = rest_at(eng, SIDE_SELL, 10000, 100);

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10000, 30);
    assert(match_limit(eng, &taker, &res) == ERR_OK);
    assert(res.filled_qty == 30);
    assert(book_qty_at(book, SIDE_SELL, 10000) == 70);

    assert(match_cancel(eng, resting, NEXT_TS++, &res) == ERR_OK);
    assert(res.remaining_qty == 70); /* 기체결 30은 취소 대상이 아니다 */
    assert(book_qty_at(book, SIDE_SELL, 10000) == 0);

    match_engine_destroy(eng);
}

/* 전량 체결된 주문은 취소도 정정도 안 된다 */
static void test_filled_order_gone(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    order_id_t maker = rest_at(eng, SIDE_SELL, 10000, 50);

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10000, 50);
    assert(match_limit(eng, &taker, &res) == ERR_OK);
    assert(res.filled_qty == 50);

    assert(match_cancel(eng, maker, NEXT_TS++, &res) == ERR_NOT_FOUND);
    assert(match_modify(eng, maker, 10000, 40, NEXT_TS++, &res) == ERR_NOT_FOUND);
    /* taker는 애초에 등록된 적이 없다 */
    assert(match_cancel(eng, taker.id, NEXT_TS++, &res) == ERR_NOT_FOUND);

    match_engine_destroy(eng);
}

/* 수량 감소만 — 시간 우선순위 유지 */
static void test_modify_qty_down_keeps_priority(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t a = rest_at(eng, SIDE_SELL, 10000, 50);
    order_id_t b = rest_at(eng, SIDE_SELL, 10000, 50);
    order_id_t c = rest_at(eng, SIDE_SELL, 10000, 50);

    exec_result_t res;
    assert(match_modify(eng, a, 10000, 20, NEXT_TS++, &res) == ERR_OK); /* 맨 앞을 줄인다 */
    assert(res.status == STATUS_NEW);
    assert(res.remaining_qty == 20);
    assert(res.resting);
    assert(book_qty_at(book, SIDE_SELL, 10000) == 20 + 50 + 50);

    order_id_t q[4];
    assert(queue_of(book, SIDE_SELL, 10000, q, 4) == 3);
    assert(q[0] == a && q[1] == b && q[2] == c); /* 순서 그대로 */

    /* 실제로 먼저 체결되는지까지 본다 */
    order_t taker = req_of(SIDE_BUY, 10000, 30);
    assert(match_limit(eng, &taker, &res) == ERR_OK);
    assert(res.fill_count == 2);
    assert(res.fills[0].maker_id == a && res.fills[0].qty == 20);
    assert(res.fills[1].maker_id == b && res.fills[1].qty == 10);

    match_engine_destroy(eng);
}

/* 수량 증가 — 시간 우선순위 상실 (큐 뒤로) */
static void test_modify_qty_up_loses_priority(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t a = rest_at(eng, SIDE_SELL, 10000, 50);
    order_id_t b = rest_at(eng, SIDE_SELL, 10000, 50);
    order_id_t c = rest_at(eng, SIDE_SELL, 10000, 50);

    exec_result_t res;
    assert(match_modify(eng, a, 10000, 80, NEXT_TS++, &res) == ERR_OK);
    assert(res.remaining_qty == 80);
    assert(book_qty_at(book, SIDE_SELL, 10000) == 80 + 50 + 50);

    order_id_t q[4];
    assert(queue_of(book, SIDE_SELL, 10000, q, 4) == 3);
    assert(q[0] == b && q[1] == c && q[2] == a); /* a가 뒤로 갔다 */

    order_t taker = req_of(SIDE_BUY, 10000, 60);
    assert(match_limit(eng, &taker, &res) == ERR_OK);
    assert(res.fills[0].maker_id == b);
    assert(res.fills[1].maker_id == c);

    match_engine_destroy(eng);
}

/* 가격 변경 — 시간 우선순위 상실 */
static void test_modify_price_loses_priority(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t a = rest_at(eng, SIDE_SELL, 10010, 50);
    order_id_t b = rest_at(eng, SIDE_SELL, 10000, 50);
    order_id_t c = rest_at(eng, SIDE_SELL, 10000, 50);

    exec_result_t res;
    /* a를 10,000으로 내린다. 이미 있던 b, c보다 뒤에 서야 한다 */
    assert(match_modify(eng, a, 10000, 50, NEXT_TS++, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_SELL, 10010) == 0);
    assert(book_qty_at(book, SIDE_SELL, 10000) == 150);
    assert(book_best_ask(book) == 10000);

    order_id_t q[4];
    assert(queue_of(book, SIDE_SELL, 10000, q, 4) == 3);
    assert(q[0] == b && q[1] == c && q[2] == a);

    match_engine_destroy(eng);
}

/* 같은 값으로 정정하면 아무 일도 없다 — 괜히 우선순위를 잃지 않는다 */
static void test_modify_noop_keeps_priority(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t a = rest_at(eng, SIDE_SELL, 10000, 50);
    order_id_t b = rest_at(eng, SIDE_SELL, 10000, 50);

    exec_result_t res;
    assert(match_modify(eng, a, 10000, 50, NEXT_TS++, &res) == ERR_OK);

    order_id_t q[4];
    assert(queue_of(book, SIDE_SELL, 10000, q, 4) == 2);
    assert(q[0] == a && q[1] == b);

    match_engine_destroy(eng);
}

/* 부분 체결된 주문의 정정 — 기체결 수량은 사라지지 않는다 */
static void test_modify_partially_filled(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t resting = rest_at(eng, SIDE_SELL, 10000, 100);

    exec_result_t res;
    order_t taker = req_of(SIDE_BUY, 10000, 40);
    assert(match_limit(eng, &taker, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_SELL, 10000) == 60);

    /* 원 수량을 100 -> 70으로. 기체결 40은 그대로이므로 잔량은 30이 된다 */
    assert(match_modify(eng, resting, 10000, 70, NEXT_TS++, &res) == ERR_OK);
    assert(res.status == STATUS_PARTIAL); /* 기체결분이 있다 */
    assert(res.remaining_qty == 30);
    assert(book_qty_at(book, SIDE_SELL, 10000) == 30);

    order_book_t *mut = (order_book_t *)book;
    const order_t *o = book_front(mut, SIDE_SELL, 10000);
    assert(o != NULL && o->id == resting);
    assert(o->qty == 70);
    assert(o->filled_qty == 40); /* 정정이 기체결을 지우지 않았다 */

    /* 기체결 수량 이하로는 줄일 수 없다 — 그건 취소다 */
    assert(match_modify(eng, resting, 10000, 40, NEXT_TS++, &res) == ERR_INVALID_QTY);
    assert(match_modify(eng, resting, 10000, 10, NEXT_TS++, &res) == ERR_INVALID_QTY);
    assert(book_qty_at(book, SIDE_SELL, 10000) == 30); /* 안 바뀌었다 */

    /* 가격까지 바꾸는 경로가 더 위험하다. 여기서 늦게 걸리면 주문을 이미 호가창에서
     * 뗀 뒤라 그대로 사라진다. 거절되고 주문이 살아 있어야 한다. */
    assert(match_modify(eng, resting, 10010, 40, NEXT_TS++, &res) == ERR_INVALID_QTY);
    assert(match_cancel(eng, resting, NEXT_TS++, &res) == ERR_OK); /* 아직 살아 있다 */
    assert(res.remaining_qty == 30);

    match_engine_destroy(eng);
}

/* 교차하는 가격으로는 정정할 수 없다 */
static void test_modify_rejects_cross(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t bid = rest_at(eng, SIDE_BUY, 9900, 50);
    rest_at(eng, SIDE_SELL, 10000, 50);

    exec_result_t res;
    /* 매수를 최우선매도호가까지 올리면 교차한다 */
    assert(match_modify(eng, bid, 10000, 50, NEXT_TS++, &res) == ERR_INVALID_PRICE);
    assert(match_modify(eng, bid, 10010, 50, NEXT_TS++, &res) == ERR_INVALID_PRICE);
    assert(res.status == STATUS_REJECTED);

    /* 거절 후에도 원 주문은 그대로 */
    assert(book_qty_at(book, SIDE_BUY, 9900) == 50);
    assert(book_best_bid(book) == 9900);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 0);

    /* 교차하지 않는 데까지는 올릴 수 있다 */
    assert(match_modify(eng, bid, 9990, 50, NEXT_TS++, &res) == ERR_OK);
    assert(book_best_bid(book) == 9990);

    match_engine_destroy(eng);
}

/* 정정 인자 검증. 거절되면 원 주문이 그대로여야 한다 */
static void test_modify_rejects(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    order_id_t a = rest_at(eng, SIDE_BUY, 9900, 50);
    order_id_t b = rest_at(eng, SIDE_BUY, 9900, 50);
    exec_result_t res;

    assert(match_modify(eng, a, book_price_high(book) + 10, 50, NEXT_TS++, &res) ==
           ERR_PRICE_LIMIT);
    assert(match_modify(eng, a, 9905, 50, NEXT_TS++, &res) == ERR_INVALID_TICK);
    assert(match_modify(eng, a, 9900, 0, NEXT_TS++, &res) == ERR_INVALID_QTY);
    assert(match_modify(eng, a, 9900, QTY_MAX + 1, NEXT_TS++, &res) == ERR_INVALID_QTY);
    assert(match_modify(eng, 999999, 9900, 10, NEXT_TS++, &res) == ERR_NOT_FOUND);
    assert(match_modify(NULL, a, 9900, 10, 0, &res) == ERR_NULL_PTR);
    assert(match_modify(eng, a, 9900, 10, 0, NULL) == ERR_NULL_PTR);

    /* 수량도 순서도 그대로 */
    assert(book_qty_at(book, SIDE_BUY, 9900) == 100);
    order_id_t q[4];
    assert(queue_of(book, SIDE_BUY, 9900, q, 4) == 2);
    assert(q[0] == a && q[1] == b);

    match_engine_destroy(eng);
}

/* 취소한 주문번호는 다시 쓸 수 있다 (슬롯이 풀로 돌아갔다) */
static void test_reuse_after_cancel(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    exec_result_t res;
    order_t req = req_of(SIDE_BUY, 9900, 50);
    assert(match_limit(eng, &req, &res) == ERR_OK);
    assert(match_cancel(eng, req.id, NEXT_TS++, &res) == ERR_OK);

    /* 같은 번호로 다시 낸다 — 인덱스에서 빠졌으므로 중복이 아니다 */
    order_t again = req_of(SIDE_BUY, 9950, 70);
    again.id = req.id;
    assert(match_limit(eng, &again, &res) == ERR_OK);
    assert(book_qty_at(book, SIDE_BUY, 9950) == 70);
    assert(match_cancel(eng, req.id, NEXT_TS++, &res) == ERR_OK);
    assert(res.remaining_qty == 70);

    match_engine_destroy(eng);
}

int main(void)
{
    test_cancel();
    test_cancel_partially_filled();
    test_filled_order_gone();
    test_modify_qty_down_keeps_priority();
    test_modify_qty_up_loses_priority();
    test_modify_price_loses_priority();
    test_modify_noop_keeps_priority();
    test_modify_partially_filled();
    test_modify_rejects_cross();
    test_modify_rejects();
    test_reuse_after_cancel();
    return 0;
}
