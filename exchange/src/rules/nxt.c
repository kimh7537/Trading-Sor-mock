#include <stddef.h>

#include "market_rules.h"
#include "tick_size.h"

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
    market_session_t session;
    bool      open;
} NXT_SESSIONS[] = {
    {TOD_NS(8, 0, 0), TOD_NS(8, 50, 0), SESSION_PRE, true},
    {TOD_NS(8, 50, 0), TOD_NS(9, 0, 30), SESSION_PRE_BREAK, false},
    {TOD_NS(9, 0, 30), TOD_NS(15, 20, 0), SESSION_REGULAR, true},
    {TOD_NS(15, 20, 0), TOD_NS(15, 30, 0), SESSION_POST_BREAK, false},
    {TOD_NS(15, 30, 0), TOD_NS(20, 0, 0), SESSION_AFTER, true},
};

#define NXT_SESSION_COUNT (sizeof(NXT_SESSIONS) / sizeof(NXT_SESSIONS[0]))

static bool nxt_is_open(ts_t ts, market_session_t *out)
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

static bool nxt_type_allowed(market_session_t session, order_type_t type)
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

static bool nxt_can_submit(market_session_t session)
{
    return session == SESSION_PRE || session == SESSION_REGULAR ||
           session == SESSION_AFTER;
}

static bool nxt_can_cancel(market_session_t session)
{
    /* 휴장 구간에서도 취소는 받는다 — KRX와 갈리는 지점이다. */
    return session != SESSION_CLOSED;
}

/*
 * 중간가 (docs/SPEC.md 4.2, T1-16).
 *
 * 가격 = (최우선매수호가 + 최우선매도호가) / 2, 호가 단위로 **내림**.
 *
 * 내림으로 고정한 이유가 핵심이다. 매수는 내림·매도는 올림처럼 편별로 방향을
 * 나누면 같은 호가 상태에서 매수 중간가와 매도 중간가가 서로 다른 가격을 받아
 * 영원히 만나지 못한다. 방향이 하나여야 두 중간가 주문이 같은 가격에서 체결된다.
 *
 * 한쪽 호가가 비어 있으면 중간값이 정의되지 않는다. BOOK_PRICE_NONE을 돌려주면
 * 엔진이 ERR_INVALID_PRICE로 거부한다.
 *
 * 값은 이 시점의 호가로 한 번 정해지고 그 뒤로 갱신되지 않는다(접수 시점 고정).
 * 지속 갱신은 호가가 바뀔 때마다 등록된 중간가 주문을 전부 다른 레벨로 옮겨야 하고,
 * 그 이동이 또 호가를 바꿔 연쇄가 생긴다. 시간 우선순위를 어떻게 유지할지도
 * 정의되지 않는다. 단순화 지점이며 ADR-0003에 근거를 적었다.
 */
static price_t nxt_resolve_price(const order_book_t *book, const order_t *req)
{
    if (req->type != ORDER_MIDPOINT) {
        return req->price; /* 나머지 유형은 실려 온 값 그대로 */
    }

    price_t bid = book_best_bid(book);
    price_t ask = book_best_ask(book);
    if (bid == BOOK_PRICE_NONE || ask == BOOK_PRICE_NONE) {
        return BOOK_PRICE_NONE; /* 한쪽이 비면 중간값이 없다 */
    }

    /* 둘 다 양수이므로 정수 나눗셈의 절단이 곧 내림이다. */
    price_t mid = (bid + ask) / 2;

    /* 스프레드가 한 틱이면 내림 결과가 매수호가와 같아진다. 그것도 유효한 답이다. */
    return round_to_tick(mid, false);
}

static bool nxt_allows_reprice(order_type_t type)
{
    /* 중간가는 가격이 호가에서 나온다. 정정으로 임의 가격에 놓을 수 없다. */
    return type != ORDER_MIDPOINT;
}

const market_rules_t NXT_RULES = {
    .name = "NXT",
    .is_open = nxt_is_open,
    .is_order_type_allowed = nxt_type_allowed,
    .can_submit = nxt_can_submit,
    .can_cancel = nxt_can_cancel,
    .resolve_price = nxt_resolve_price,
    .allows_reprice = nxt_allows_reprice,
};
