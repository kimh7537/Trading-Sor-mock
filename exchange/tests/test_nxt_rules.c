/*
 * T1-15 NXT 규칙.
 *
 * 구간이 다섯이므로 경계가 네 개다. 경계마다 (직전 1초, 직전 1나노초, 정각, 직후)를
 * 모두 본다. 특히 메인마켓 시작이 09:00:00이 아니라 09:00:30이라는 것 —
 * SPEC이 굳이 "주의"로 적어 둔 지점이라 09:00:00과 09:00:29를 따로 본다.
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "market_rules.h"
#include "match.h"

#define BASE 10000
#define CAP 64

/* 한 시각의 판정을 (구간, 열림) 쌍으로 확인한다 */
static void at(ts_t ts, market_session_t want, bool want_open)
{
    market_session_t got = SESSION_CLOSED;
    bool open = NXT_RULES.is_open(ts, &got);
    assert(got == want);
    assert(open == want_open);
}

/* 다섯 구간의 안쪽 */
static void test_sessions_inside(void)
{
    at(TOD_NS(8, 0, 0), SESSION_PRE, true);
    at(TOD_NS(8, 30, 0), SESSION_PRE, true);
    at(TOD_NS(8, 55, 0), SESSION_PRE_BREAK, false);
    at(TOD_NS(12, 0, 0), SESSION_REGULAR, true);
    at(TOD_NS(15, 25, 0), SESSION_POST_BREAK, false);
    at(TOD_NS(18, 0, 0), SESSION_AFTER, true);
}

/* 네 경계 + 개장 전/폐장 후 */
static void test_boundaries(void)
{
    /* 08:00 개장 */
    at(TOD_NS(7, 59, 59), SESSION_CLOSED, false);
    at(TOD_NS(8, 0, 0) - 1, SESSION_CLOSED, false);
    at(TOD_NS(8, 0, 0), SESSION_PRE, true);
    at(TOD_NS(8, 0, 0) + 1, SESSION_PRE, true);

    /* 08:50 프리마켓 -> 오전 휴장 */
    at(TOD_NS(8, 49, 59), SESSION_PRE, true);
    at(TOD_NS(8, 50, 0) - 1, SESSION_PRE, true);
    at(TOD_NS(8, 50, 0), SESSION_PRE_BREAK, false);
    at(TOD_NS(8, 50, 1), SESSION_PRE_BREAK, false);

    /* 09:00:30 오전 휴장 -> 메인마켓. 09:00:00은 아직 휴장이다 */
    at(TOD_NS(9, 0, 0), SESSION_PRE_BREAK, false);
    at(TOD_NS(9, 0, 29), SESSION_PRE_BREAK, false);
    at(TOD_NS(9, 0, 30) - 1, SESSION_PRE_BREAK, false);
    at(TOD_NS(9, 0, 30), SESSION_REGULAR, true);
    at(TOD_NS(9, 0, 30) + 1, SESSION_REGULAR, true);

    /* 15:20 메인마켓 -> 오후 휴장 */
    at(TOD_NS(15, 19, 59), SESSION_REGULAR, true);
    at(TOD_NS(15, 20, 0) - 1, SESSION_REGULAR, true);
    at(TOD_NS(15, 20, 0), SESSION_POST_BREAK, false);

    /* 15:30 오후 휴장 -> 애프터마켓. KRX가 닫히는 바로 그 시각이다 */
    at(TOD_NS(15, 29, 59), SESSION_POST_BREAK, false);
    at(TOD_NS(15, 30, 0) - 1, SESSION_POST_BREAK, false);
    at(TOD_NS(15, 30, 0), SESSION_AFTER, true);

    /* 20:00 폐장 */
    at(TOD_NS(19, 59, 59), SESSION_AFTER, true);
    at(TOD_NS(20, 0, 0) - 1, SESSION_AFTER, true);
    at(TOD_NS(20, 0, 0), SESSION_CLOSED, false);
    at(TOD_NS(23, 59, 59), SESSION_CLOSED, false);
    at(TOD_NS(0, 0, 0), SESSION_CLOSED, false);
}

/* 구간별 허용 주문 유형 */
static void test_order_types(void)
{
    const order_type_t all[] = {ORDER_LIMIT, ORDER_MARKET, ORDER_IOC, ORDER_FOK,
                                ORDER_MIDPOINT};

    /* 메인마켓은 전부 */
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        assert(NXT_RULES.is_order_type_allowed(SESSION_REGULAR, all[i]));
    }

    /* 프리·애프터마켓은 지정가만 */
    const market_session_t limit_only[] = {SESSION_PRE, SESSION_AFTER};
    for (size_t s = 0; s < 2; s++) {
        assert(NXT_RULES.is_order_type_allowed(limit_only[s], ORDER_LIMIT));
        assert(!NXT_RULES.is_order_type_allowed(limit_only[s], ORDER_MARKET));
        assert(!NXT_RULES.is_order_type_allowed(limit_only[s], ORDER_IOC));
        assert(!NXT_RULES.is_order_type_allowed(limit_only[s], ORDER_FOK));
        assert(!NXT_RULES.is_order_type_allowed(limit_only[s], ORDER_MIDPOINT));
    }

    /* 휴장·폐장 구간은 아무것도 */
    const market_session_t closed[] = {SESSION_CLOSED, SESSION_PRE_BREAK,
                                SESSION_POST_BREAK};
    for (size_t s = 0; s < 3; s++) {
        for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
            assert(!NXT_RULES.is_order_type_allowed(closed[s], all[i]));
        }
    }
}

/* 휴장 구간: 신규 불가, 취소 가능 */
static void test_submit_cancel(void)
{
    assert(NXT_RULES.can_submit(SESSION_PRE));
    assert(NXT_RULES.can_submit(SESSION_REGULAR));
    assert(NXT_RULES.can_submit(SESSION_AFTER));
    assert(!NXT_RULES.can_submit(SESSION_PRE_BREAK));
    assert(!NXT_RULES.can_submit(SESSION_POST_BREAK));
    assert(!NXT_RULES.can_submit(SESSION_CLOSED));

    /* 취소는 폐장을 빼고 전부 가능하다 — KRX와 갈리는 지점 */
    assert(NXT_RULES.can_cancel(SESSION_PRE));
    assert(NXT_RULES.can_cancel(SESSION_PRE_BREAK));
    assert(NXT_RULES.can_cancel(SESSION_REGULAR));
    assert(NXT_RULES.can_cancel(SESSION_POST_BREAK));
    assert(NXT_RULES.can_cancel(SESSION_AFTER));
    assert(!NXT_RULES.can_cancel(SESSION_CLOSED));
}

/* 중간가만 정정으로 가격을 못 바꾼다 */
static void test_allows_reprice(void)
{
    assert(NXT_RULES.allows_reprice(ORDER_LIMIT));
    assert(NXT_RULES.allows_reprice(ORDER_MARKET));
    assert(NXT_RULES.allows_reprice(ORDER_IOC));
    assert(NXT_RULES.allows_reprice(ORDER_FOK));
    assert(!NXT_RULES.allows_reprice(ORDER_MIDPOINT));
}

/* KRX와 NXT가 동시에 열려 있지 않은 구간이 실제로 있다 (SOR 테스트의 근거) */
static void test_asymmetry_with_krx(void)
{
    market_session_t ns, ks;

    /* 08:30 — NXT만 */
    assert(NXT_RULES.is_open(TOD_NS(8, 30, 0), &ns));
    assert(!KRX_RULES.is_open(TOD_NS(8, 30, 0), &ks));

    /* 18:00 — NXT만 */
    assert(NXT_RULES.is_open(TOD_NS(18, 0, 0), &ns));
    assert(!KRX_RULES.is_open(TOD_NS(18, 0, 0), &ks));

    /* 15:25 — KRX만 (NXT는 오후 휴장) */
    assert(!NXT_RULES.is_open(TOD_NS(15, 25, 0), &ns));
    assert(KRX_RULES.is_open(TOD_NS(15, 25, 0), &ks));

    /* 09:00:10 — KRX만 (NXT는 아직 오전 휴장) */
    assert(!NXT_RULES.is_open(TOD_NS(9, 0, 10), &ns));
    assert(KRX_RULES.is_open(TOD_NS(9, 0, 10), &ks));

    /* 12:00 — 둘 다 */
    assert(NXT_RULES.is_open(TOD_NS(12, 0, 0), &ns));
    assert(KRX_RULES.is_open(TOD_NS(12, 0, 0), &ks));
}

/* 엔진을 통한 확인 */
static void test_through_engine(void)
{
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_rules(eng, &NXT_RULES);
    const order_book_t *book = match_book(eng);

    exec_result_t res;
    order_id_t id = 1;

    order_t base = {0};
    base.side = SIDE_BUY;
    base.price = 9900;
    base.qty = 10;
    base.type = ORDER_LIMIT;
    base.market = MARKET_NXT;

    /* 프리마켓에 지정가는 받는다 */
    order_t pre = base;
    pre.id = id++;
    pre.ts = TOD_NS(8, 30, 0);
    assert(match_limit(eng, &pre, &res) == ERR_OK);
    assert(book_best_bid(book) == 9900);

    /* 프리마켓에 IOC는 거부 */
    order_t pre_ioc = base;
    pre_ioc.id = id++;
    pre_ioc.ts = TOD_NS(8, 30, 0);
    pre_ioc.type = ORDER_IOC;
    assert(match_ioc(eng, &pre_ioc, &res) == ERR_NOT_SUPPORTED);

    /* 오전 휴장에 신규는 거부, 취소는 통과 */
    order_t brk = base;
    brk.id = id++;
    brk.ts = TOD_NS(9, 0, 0); /* 09:00:00은 아직 휴장 */
    assert(match_limit(eng, &brk, &res) == ERR_MARKET_CLOSED);
    assert(match_cancel(eng, pre.id, TOD_NS(9, 0, 0), &res) == ERR_OK);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);

    /* 09:00:30부터는 신규도 받는다 */
    order_t main_open = base;
    main_open.id = id++;
    main_open.ts = TOD_NS(9, 0, 30);
    assert(match_limit(eng, &main_open, &res) == ERR_OK);

    /* 폐장 후에는 취소도 안 된다 */
    assert(match_cancel(eng, main_open.id, TOD_NS(21, 0, 0), &res) ==
           ERR_MARKET_CLOSED);
    /* 애프터마켓에는 된다 */
    assert(match_cancel(eng, main_open.id, TOD_NS(18, 0, 0), &res) == ERR_OK);

    match_engine_destroy(eng);
}

int main(void)
{
    test_sessions_inside();
    test_boundaries();
    test_order_types();
    test_submit_cancel();
    test_allows_reprice();
    test_asymmetry_with_krx();
    test_through_engine();
    return 0;
}
