#include <stddef.h>

#include "market_rules.h"

/*
 * KRX 규칙 (docs/SPEC.md 2.1, 4.2).
 *
 * 정규장 09:00 ~ 15:30 하나뿐이다. 휴장 구간이 없으므로 장이 닫히면 취소도 안 된다.
 * 단일가 매매(시가·종가)는 Phase 1 범위 밖이라 연속 체결만 다룬다.
 *
 * 가격 제한폭(±30%)은 여기서 보지 않는다. 호가창이 기준가로 만들어질 때 이미
 * 다룰 수 있는 가격 구간이 정해지고, 그 밖의 주문은 ERR_PRICE_LIMIT으로 걸린다.
 * 같은 검사를 두 곳에 두면 한쪽만 고치는 일이 생긴다.
 */

#define KRX_OPEN TOD_NS(9, 0, 0)
#define KRX_CLOSE TOD_NS(15, 30, 0)

static bool krx_is_open(ts_t ts, market_session_t *out)
{
    int64_t tod = ts_time_of_day(ts);

    /* 시작은 포함, 끝은 제외. 15:30:00 정각은 이미 장 마감이다. */
    if (tod >= KRX_OPEN && tod < KRX_CLOSE) {
        *out = SESSION_REGULAR;
        return true;
    }
    *out = SESSION_CLOSED;
    return false;
}

static bool krx_type_allowed(market_session_t session, order_type_t type)
{
    if (session != SESSION_REGULAR) {
        return false;
    }
    /* 중간가는 NXT 메인마켓 전용이다. KRX는 받지 않는다. */
    return type != ORDER_MIDPOINT;
}

static bool krx_can_submit(market_session_t session)
{
    return session == SESSION_REGULAR;
}

static bool krx_can_cancel(market_session_t session)
{
    /* 휴장 구간이 없으므로 장이 열려 있을 때만 취소도 받는다. */
    return session == SESSION_REGULAR;
}

static bool krx_allows_reprice(order_type_t type)
{
    (void)type;
    /* KRX에는 가격이 규칙에서 나오는 유형이 없다. 전부 정정 가능. */
    return true;
}

static price_t krx_resolve_price(const order_book_t *book, const order_t *req)
{
    (void)book;
    /* KRX에는 호가에 따라 가격이 정해지는 주문이 없다. 실려 온 값 그대로. */
    return req->price;
}

const market_rules_t KRX_RULES = {
    .name = "KRX",
    .is_open = krx_is_open,
    .is_order_type_allowed = krx_type_allowed,
    .can_submit = krx_can_submit,
    .can_cancel = krx_can_cancel,
    .resolve_price = krx_resolve_price,
    .allows_reprice = krx_allows_reprice,
};
