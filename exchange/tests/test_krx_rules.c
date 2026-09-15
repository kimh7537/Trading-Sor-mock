/*
 * T1-14 KRX 규칙.
 *
 * 세션 규칙의 위험 지점은 경계다. 09:00:00 정각이 열린 것인지, 15:30:00 정각이
 * 닫힌 것인지. 각 경계마다 (직전 1초, 직전 1나노초, 정각, 직후)를 모두 본다.
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "market_rules.h"
#include "match.h"

#define BASE 10000
#define CAP 64

static bool open_at(ts_t ts, session_t *out)
{
    return KRX_RULES.is_open(ts, out);
}

/* 장 시작·마감 경계 */
static void test_session_boundaries(void)
{
    session_t s;

    /* 09:00 직전은 닫혀 있다 */
    assert(!open_at(TOD_NS(8, 59, 59), &s) && s == SESSION_CLOSED);
    assert(!open_at(TOD_NS(9, 0, 0) - 1, &s) && s == SESSION_CLOSED);

    /* 09:00:00 정각부터 열린다 */
    assert(open_at(TOD_NS(9, 0, 0), &s) && s == SESSION_REGULAR);
    assert(open_at(TOD_NS(9, 0, 0) + 1, &s) && s == SESSION_REGULAR);
    assert(open_at(TOD_NS(9, 0, 1), &s) && s == SESSION_REGULAR);

    /* 장중 */
    assert(open_at(TOD_NS(12, 0, 0), &s) && s == SESSION_REGULAR);

    /* 15:30 직전까지 열려 있다 */
    assert(open_at(TOD_NS(15, 29, 59), &s) && s == SESSION_REGULAR);
    assert(open_at(TOD_NS(15, 30, 0) - 1, &s) && s == SESSION_REGULAR);

    /* 15:30:00 정각은 이미 마감이다 */
    assert(!open_at(TOD_NS(15, 30, 0), &s) && s == SESSION_CLOSED);
    assert(!open_at(TOD_NS(15, 30, 1), &s) && s == SESSION_CLOSED);

    /* 장 밖 */
    assert(!open_at(TOD_NS(0, 0, 0), &s) && s == SESSION_CLOSED);
    assert(!open_at(TOD_NS(8, 0, 0), &s) && s == SESSION_CLOSED); /* NXT는 열림 */
    assert(!open_at(TOD_NS(18, 0, 0), &s) && s == SESSION_CLOSED);
    assert(!open_at(TOD_NS(23, 59, 59), &s) && s == SESSION_CLOSED);

    /* 하루를 넘겨도 같은 시각이면 같은 판정이다 */
    assert(open_at(TOD_NS(10, 0, 0) + NS_PER_DAY * 3, &s));
    assert(open_at(TOD_NS(10, 0, 0) - NS_PER_DAY, &s)); /* 음수 시각 */
}

/* 주문 유형 — 중간가만 거부한다 */
static void test_order_types(void)
{
    const order_type_t ok[] = {ORDER_LIMIT, ORDER_MARKET, ORDER_IOC, ORDER_FOK};
    for (size_t i = 0; i < sizeof(ok) / sizeof(ok[0]); i++) {
        assert(KRX_RULES.is_order_type_allowed(SESSION_REGULAR, ok[i]));
    }
    assert(!KRX_RULES.is_order_type_allowed(SESSION_REGULAR, ORDER_MIDPOINT));

    /* 정규장이 아닌 구간에서는 아무것도 안 받는다 */
    assert(!KRX_RULES.is_order_type_allowed(SESSION_CLOSED, ORDER_LIMIT));
    assert(!KRX_RULES.is_order_type_allowed(SESSION_PRE, ORDER_LIMIT));
    assert(!KRX_RULES.is_order_type_allowed(SESSION_AFTER, ORDER_LIMIT));
}

/* KRX는 휴장 구간이 없다 — 닫히면 취소도 안 된다 */
static void test_submit_cancel(void)
{
    assert(KRX_RULES.can_submit(SESSION_REGULAR));
    assert(KRX_RULES.can_cancel(SESSION_REGULAR));

    assert(!KRX_RULES.can_submit(SESSION_CLOSED));
    assert(!KRX_RULES.can_cancel(SESSION_CLOSED));
    assert(!KRX_RULES.can_submit(SESSION_PRE_BREAK));
    assert(!KRX_RULES.can_cancel(SESSION_PRE_BREAK));
}

/* 정정으로 가격을 바꿀 수 있는 유형 — KRX는 전부 가능 */
static void test_allows_reprice(void)
{
    assert(KRX_RULES.allows_reprice(ORDER_LIMIT));
    assert(KRX_RULES.allows_reprice(ORDER_MARKET));
    assert(KRX_RULES.allows_reprice(ORDER_MIDPOINT)); /* 애초에 접수가 안 된다 */
}

/* 가격은 실려 온 값 그대로 */
static void test_resolve_price(void)
{
    order_t req = {0};
    req.price = 12345;
    assert(KRX_RULES.resolve_price(NULL, &req) == 12345);
}

/* 엔진에 걸었을 때 실제로 시각에 따라 갈리는지 */
static void test_through_engine(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_rules(eng, &KRX_RULES);
    const order_book_t *book = match_book(eng);

    exec_result_t res;
    order_id_t id = 1;

    /* 08:59:59 — 아직 안 받는다 */
    order_t early = {0};
    early.id = id++;
    early.ts = TOD_NS(8, 59, 59);
    early.side = SIDE_BUY;
    early.price = 9900;
    early.qty = 10;
    early.type = ORDER_LIMIT;
    early.market = MARKET_KRX;
    assert(match_limit(eng, &early, &res) == ERR_MARKET_CLOSED);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);

    /* 09:00:00 — 받는다 */
    order_t ontime = early;
    ontime.id = id++;
    ontime.ts = TOD_NS(9, 0, 0);
    assert(match_limit(eng, &ontime, &res) == ERR_OK);
    assert(book_best_bid(book) == 9900);

    /* 중간가는 정규장에도 거부 */
    order_t mid = early;
    mid.id = id++;
    mid.ts = TOD_NS(10, 0, 0);
    mid.type = ORDER_MIDPOINT;
    assert(match_limit(eng, &mid, &res) == ERR_NOT_SUPPORTED);

    /* 제한폭 밖은 세션과 무관하게 거부 — 호가창이 막는다 */
    order_t far = early;
    far.id = id++;
    far.ts = TOD_NS(10, 0, 0);
    far.price = book_price_high(book) + 10;
    assert(match_limit(eng, &far, &res) == ERR_PRICE_LIMIT);

    /* 15:30:00 취소는 안 되고 15:29:59 취소는 된다 */
    assert(match_cancel(eng, ontime.id, TOD_NS(15, 30, 0), &res) ==
           ERR_MARKET_CLOSED);
    assert(book_best_bid(book) == 9900); /* 아직 살아 있다 */
    assert(match_cancel(eng, ontime.id, TOD_NS(15, 29, 59), &res) == ERR_OK);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

int main(void)
{
    test_session_boundaries();
    test_order_types();
    test_submit_cancel();
    test_allows_reprice();
    test_resolve_price();
    test_through_engine();
    return 0;
}
