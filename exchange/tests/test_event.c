/*
 * T1-12 체결 이벤트 출력.
 *
 * 이벤트는 "몇 개 나왔나"가 아니라 "정확히 이 순서로 이것들이 나왔나"를 봐야 한다.
 * 순서가 결정적이라는 것이 T1-19의 전제이기 때문이다. 시나리오마다 기대 시퀀스를
 * 표로 적어 두고 종류·주문번호·가격·수량·잔량을 한 건씩 대조한다.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "match.h"

#define BASE 10000
#define CAP 256
#define LOG_MAX 64

typedef struct {
    order_event_t ev[LOG_MAX];
    int n;
} log_t;

static void on_event(const order_event_t *ev, void *ctx)
{
    log_t *log = ctx;
    assert(log->n < LOG_MAX);
    log->ev[log->n++] = *ev;
}

/* 기대 시퀀스 한 줄. 잔량까지 적어야 부분/전량 구분이 실제로 검증된다. */
typedef struct {
    event_type_t type;
    order_id_t   order_id;
    price_t      price;
    qty_t        qty;
    qty_t        remaining;
} want_t;

static void assert_seq(const log_t *log, const want_t *want, int n)
{
    if (log->n != n) {
        fprintf(stderr, "기대 %d건, 실제 %d건\n", n, log->n);
        for (int i = 0; i < log->n; i++) {
            fprintf(stderr, "  [%d] %s id=%llu price=%d qty=%d rem=%d\n", i,
                    event_type_str(log->ev[i].type),
                    (unsigned long long)log->ev[i].order_id, log->ev[i].price,
                    log->ev[i].qty, log->ev[i].remaining_qty);
        }
    }
    assert(log->n == n);
    for (int i = 0; i < n; i++) {
        const order_event_t *e = &log->ev[i];
        assert(e->type == want[i].type);
        assert(e->order_id == want[i].order_id);
        assert(e->price == want[i].price);
        assert(e->qty == want[i].qty);
        assert(e->remaining_qty == want[i].remaining);
        assert(event_type_str(e->type) != NULL);
    }
}

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
    req.market = MARKET_NXT; /* 기본값(KRX=0)과 달라야 전달 여부가 보인다 */
    return req;
}

/* 체결 없이 등록 — ACCEPTED 하나 */
static void test_accepted(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_sink(eng, &sink);

    exec_result_t res;
    order_t req = req_of(SIDE_BUY, 9900, 100, ORDER_LIMIT);
    assert(match_limit(eng, &req, &res) == ERR_OK);

    want_t want[] = {
        {EVENT_ACCEPTED, req.id, 9900, 100, 100},
    };
    assert_seq(&log, want, 1);
    assert(log.ev[0].market == MARKET_NXT);
    assert(log.ev[0].ts == req.ts);
    assert(log.ev[0].counterparty_id == ORDER_ID_INVALID);
    assert(log.ev[0].reason == ERR_OK);

    match_engine_destroy(eng);
}

/* 양쪽 다 전량 체결 — maker 먼저, taker 다음 */
static void test_full_fill_pair(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t maker = req_of(SIDE_SELL, 10000, 50, ORDER_LIMIT);
    assert(match_limit(eng, &maker, &res) == ERR_OK);

    match_set_sink(eng, &sink); /* 여기서부터 기록한다 */
    order_t taker = req_of(SIDE_BUY, 10000, 50, ORDER_LIMIT);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    want_t want[] = {
        {EVENT_EXECUTED, maker.id, 10000, 50, 0},
        {EVENT_EXECUTED, taker.id, 10000, 50, 0},
    };
    assert_seq(&log, want, 2);
    assert(log.ev[0].counterparty_id == taker.id);
    assert(log.ev[1].counterparty_id == maker.id);
    /* 두 이벤트 모두 taker가 들고 온 논리 시각을 쓴다 */
    assert(log.ev[0].ts == taker.ts && log.ev[1].ts == taker.ts);

    match_engine_destroy(eng);
}

/* taker가 더 크다 — maker 전량, taker 부분, 그리고 잔량 등록 */
static void test_partial_taker(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t maker = req_of(SIDE_SELL, 10000, 30, ORDER_LIMIT);
    assert(match_limit(eng, &maker, &res) == ERR_OK);

    match_set_sink(eng, &sink);
    order_t taker = req_of(SIDE_BUY, 10000, 100, ORDER_LIMIT);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    want_t want[] = {
        {EVENT_EXECUTED, maker.id, 10000, 30, 0},
        {EVENT_PARTIALLY_EXECUTED, taker.id, 10000, 30, 70},
        {EVENT_ACCEPTED, taker.id, 10000, 100, 70},
    };
    assert_seq(&log, want, 3);

    match_engine_destroy(eng);
}

/* maker가 더 크다 — maker 부분, taker 전량. 등록 이벤트는 없다 */
static void test_partial_maker(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t maker = req_of(SIDE_SELL, 10000, 100, ORDER_LIMIT);
    assert(match_limit(eng, &maker, &res) == ERR_OK);

    match_set_sink(eng, &sink);
    order_t taker = req_of(SIDE_BUY, 10000, 40, ORDER_LIMIT);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    want_t want[] = {
        {EVENT_PARTIALLY_EXECUTED, maker.id, 10000, 40, 60},
        {EVENT_EXECUTED, taker.id, 10000, 40, 0},
    };
    assert_seq(&log, want, 2);

    match_engine_destroy(eng);
}

/* 여러 레벨을 가로지르면 싼 것부터, 레벨마다 쌍으로 */
static void test_multi_level_sequence(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t m1 = req_of(SIDE_SELL, 10010, 40, ORDER_LIMIT);
    order_t m2 = req_of(SIDE_SELL, 10000, 40, ORDER_LIMIT);
    assert(match_limit(eng, &m1, &res) == ERR_OK);
    assert(match_limit(eng, &m2, &res) == ERR_OK);

    match_set_sink(eng, &sink);
    order_t taker = req_of(SIDE_BUY, 10010, 60, ORDER_LIMIT);
    assert(match_limit(eng, &taker, &res) == ERR_OK);

    want_t want[] = {
        {EVENT_EXECUTED, m2.id, 10000, 40, 0}, /* 싼 쪽 먼저 */
        {EVENT_PARTIALLY_EXECUTED, taker.id, 10000, 40, 20},
        {EVENT_PARTIALLY_EXECUTED, m1.id, 10010, 20, 20},
        {EVENT_EXECUTED, taker.id, 10010, 20, 0},
    };
    assert_seq(&log, want, 4);

    match_engine_destroy(eng);
}

/* 시장가 잔량은 CANCELED로 끝난다 */
static void test_market_leftover_canceled(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t maker = req_of(SIDE_SELL, 10000, 30, ORDER_LIMIT);
    assert(match_limit(eng, &maker, &res) == ERR_OK);

    match_set_sink(eng, &sink);
    order_t taker = req_of(SIDE_BUY, 10000, 100, ORDER_MARKET);
    assert(match_market(eng, &taker, &res) == ERR_OK);

    want_t want[] = {
        {EVENT_EXECUTED, maker.id, 10000, 30, 0},
        {EVENT_PARTIALLY_EXECUTED, taker.id, 10000, 30, 70},
        {EVENT_CANCELED, taker.id, 10000, 70, 0}, /* 등록이 아니라 취소 */
    };
    assert_seq(&log, want, 3);

    match_engine_destroy(eng);
}

/* IOC 잔량도 CANCELED */
static void test_ioc_leftover_canceled(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t maker = req_of(SIDE_SELL, 10000, 30, ORDER_LIMIT);
    assert(match_limit(eng, &maker, &res) == ERR_OK);

    match_set_sink(eng, &sink);
    order_t taker = req_of(SIDE_BUY, 10000, 100, ORDER_IOC);
    assert(match_ioc(eng, &taker, &res) == ERR_OK);

    want_t want[] = {
        {EVENT_EXECUTED, maker.id, 10000, 30, 0},
        {EVENT_PARTIALLY_EXECUTED, taker.id, 10000, 30, 70},
        {EVENT_CANCELED, taker.id, 10000, 70, 0},
    };
    assert_seq(&log, want, 3);

    match_engine_destroy(eng);
}

/* 거부는 REJECTED 하나, 에러 코드가 실린다 */
static void test_rejected(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_sink(eng, &sink);

    exec_result_t res;
    order_t offtick = req_of(SIDE_BUY, 10005, 10, ORDER_LIMIT);
    assert(match_limit(eng, &offtick, &res) == ERR_INVALID_TICK);

    want_t want[] = {
        {EVENT_REJECTED, offtick.id, 10005, 10, 10},
    };
    assert_seq(&log, want, 1);
    assert(log.ev[0].reason == ERR_INVALID_TICK);

    /* FOK 전량 불가도 거부다 */
    log.n = 0;
    order_t fok = req_of(SIDE_BUY, 10000, 100, ORDER_FOK);
    assert(match_fok(eng, &fok, &res) == ERR_NO_LIQUIDITY);
    assert(log.n == 1);
    assert(log.ev[0].type == EVENT_REJECTED);
    assert(log.ev[0].reason == ERR_NO_LIQUIDITY);

    match_engine_destroy(eng);
}

/* 취소와 정정 */
static void test_cancel_modify(void)
{
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t a = req_of(SIDE_BUY, 9900, 100, ORDER_LIMIT);
    assert(match_limit(eng, &a, &res) == ERR_OK);

    match_set_sink(eng, &sink);

    ts_t t1 = NEXT_TS++;
    assert(match_modify(eng, a.id, 9900, 60, t1, &res) == ERR_OK);
    ts_t t2 = NEXT_TS++;
    assert(match_cancel(eng, a.id, t2, &res) == ERR_OK);

    want_t want[] = {
        {EVENT_MODIFIED, a.id, 9900, 60, 60},
        {EVENT_CANCELED, a.id, 9900, 60, 0},
    };
    assert_seq(&log, want, 2);
    assert(log.ev[0].ts == t1); /* 정정 요청이 들고 온 시각 */
    assert(log.ev[1].ts == t2);

    /* 없는 주문의 취소는 이벤트를 내지 않는다 — 실을 정보가 없다 */
    log.n = 0;
    assert(match_cancel(eng, a.id, NEXT_TS++, &res) == ERR_NOT_FOUND);
    assert(log.n == 0);

    match_engine_destroy(eng);
}

/* 싱크가 없어도 정상 동작한다 */
static void test_no_sink(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t maker = req_of(SIDE_SELL, 10000, 50, ORDER_LIMIT);
    order_t taker = req_of(SIDE_BUY, 10000, 50, ORDER_LIMIT);
    assert(match_limit(eng, &maker, &res) == ERR_OK);
    assert(match_limit(eng, &taker, &res) == ERR_OK);
    assert(res.filled_qty == 50);

    /* 걸었다 떼도 마찬가지 */
    log_t log = {.n = 0};
    event_sink_t sink = {on_event, &log};
    match_set_sink(eng, &sink);
    match_set_sink(eng, NULL);
    order_t again = req_of(SIDE_BUY, 9900, 10, ORDER_LIMIT);
    assert(match_limit(eng, &again, &res) == ERR_OK);
    assert(log.n == 0);

    match_set_sink(NULL, &sink); /* 크래시하지 않는다 */
    event_emit(NULL, NULL);

    match_engine_destroy(eng);
}

/* 같은 입력을 두 번 흘리면 이벤트 시퀀스가 바이트 단위로 같다 */
static void test_deterministic_sequence(void)
{
    log_t logs[2];

    for (int run = 0; run < 2; run++) {
        logs[run].n = 0;
        event_sink_t sink = {on_event, &logs[run]};
        match_engine_t *eng = match_engine_create(BASE, CAP);
        assert(eng != NULL);
        match_set_sink(eng, &sink);

        /* 주문번호와 시각을 고정한다 — 실행마다 달라지면 비교가 무의미하다 */
        order_id_t id = 1;
        ts_t ts = 500;
        exec_result_t res;

        for (int i = 0; i < 12; i++) {
            order_t req = {0};
            req.id = id++;
            req.ts = ts++;
            req.side = (i % 2 == 0) ? SIDE_SELL : SIDE_BUY;
            req.price = 10000 + (price_t)((i % 5) - 2) * 10;
            req.qty = (qty_t)(10 + i * 3);
            req.type = ORDER_LIMIT;
            req.market = MARKET_KRX;
            (void)match_limit(eng, &req, &res);
        }
        (void)match_cancel(eng, 3, ts++, &res);
        (void)match_modify(eng, 5, 10000, 5, ts++, &res);

        match_engine_destroy(eng);
    }

    assert(logs[0].n == logs[1].n);
    assert(logs[0].n > 0); /* 아무 일도 안 일어났으면 비교가 무의미하다 */
    assert(memcmp(logs[0].ev, logs[1].ev,
                  sizeof(order_event_t) * (size_t)logs[0].n) == 0);
}

int main(void)
{
    test_accepted();
    test_full_fill_pair();
    test_partial_taker();
    test_partial_maker();
    test_multi_level_sequence();
    test_market_leftover_canceled();
    test_ioc_leftover_canceled();
    test_rejected();
    test_cancel_modify();
    test_no_sink();
    test_deterministic_sequence();
    return 0;
}
