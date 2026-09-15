#include <stddef.h>

#include "market_rules.h"

/*
 * NXT 규칙 (docs/SPEC.md 2.2, 4.2).
 *
 * KRX와 달리 구간이 다섯이다. 구간 경계를 표 하나로 적고 순서대로 훑는다 —
 * if 사슬로 쓰면 경계를 하나 고칠 때 앞뒤 조건을 같이 봐야 해서 틀리기 쉽다.
 *
 * 메인마켓 시작이 09:00이 아니라 **09:00:30**이다(SPEC 2.2 주의). 초 단위가
 * 실제로 다르므로 표에 그대로 적는다.
 *
 * 휴장 구간은 "열려 있지 않지만 취소는 되는" 상태다. is_open이 false를 돌려주면서도
 * session은 PRE_BREAK / POST_BREAK로 채우는 이유가 이것이다.
 */

/* 구간 표. from 이상 to 미만. 순서대로 훑으므로 시각 오름차순이어야 한다. */
static const struct {
    int64_t   from;
    int64_t   to;
    session_t session;
    bool      open;
} NXT_SESSIONS[] = {
    {TOD_NS(8, 0, 0), TOD_NS(8, 50, 0), SESSION_PRE, true},
    {TOD_NS(8, 50, 0), TOD_NS(9, 0, 30), SESSION_PRE_BREAK, false},
    {TOD_NS(9, 0, 30), TOD_NS(15, 20, 0), SESSION_REGULAR, true},
    {TOD_NS(15, 20, 0), TOD_NS(15, 30, 0), SESSION_POST_BREAK, false},
    {TOD_NS(15, 30, 0), TOD_NS(20, 0, 0), SESSION_AFTER, true},
};

#define NXT_SESSION_COUNT (sizeof(NXT_SESSIONS) / sizeof(NXT_SESSIONS[0]))

static bool nxt_is_open(ts_t ts, session_t *out)
{
    int64_t tod = ts_time_of_day(ts);

    for (size_t i = 0; i < NXT_SESSION_COUNT; i++) {
        if (tod >= NXT_SESSIONS[i].from && tod < NXT_SESSIONS[i].to) {
            *out = NXT_SESSIONS[i].session;
            return NXT_SESSIONS[i].open;
        }
    }

    *out = SESSION_CLOSED;
    return false;
}

static bool nxt_type_allowed(session_t session, order_type_t type)
{
    switch (session) {
    case SESSION_REGULAR:
        /* 메인마켓만 전 주문 유형을 받는다. 중간가도 여기서만. */
        return true;
    case SESSION_PRE:
    case SESSION_AFTER:
        /* 프리·애프터마켓은 지정가만 (SPEC 2.2) */
        return type == ORDER_LIMIT;
    case SESSION_CLOSED:
    case SESSION_PRE_BREAK:
    case SESSION_POST_BREAK:
        return false;
    }
    return false;
}

static bool nxt_can_submit(session_t session)
{
    return session == SESSION_PRE || session == SESSION_REGULAR ||
           session == SESSION_AFTER;
}

static bool nxt_can_cancel(session_t session)
{
    /* 휴장 구간에서도 취소는 받는다 — KRX와 갈리는 지점이다. */
    return session != SESSION_CLOSED;
}

static price_t nxt_resolve_price(const order_book_t *book, const order_t *req)
{
    /*
     * 중간가는 T1-16에서 구현한다. 그때까지는 지정가와 같이 취급한다.
     * 지금 임의로 계산해 두면 T1-16이 그 구현을 검증 없이 물려받게 된다.
     */
    (void)book;
    return req->price;
}

const market_rules_t NXT_RULES = {
    .name = "NXT",
    .is_open = nxt_is_open,
    .is_order_type_allowed = nxt_type_allowed,
    .can_submit = nxt_can_submit,
    .can_cancel = nxt_can_cancel,
    .resolve_price = nxt_resolve_price,
};
