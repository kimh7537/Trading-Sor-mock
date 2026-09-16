#ifndef MINI_SOR_MARKET_RULES_H
#define MINI_SOR_MARKET_RULES_H

#include <stdbool.h>

#include "order.h"
#include "order_book.h"
#include "types.h"

/*
 * 시장별 규칙 (docs/SPEC.md 2, 4.2).
 *
 * KRX와 NXT는 매칭 알고리즘이 같고 **언제 무엇을 받아 주는가**가 다르다.
 * 세션 구간이 다르고, 구간마다 허용하는 주문 유형이 다르고, NXT에는 가격이
 * 호가에 따라 정해지는 주문(중간가)이 있다.
 *
 * 그 차이를 함수 포인터 테이블로 뽑아낸다. 매칭 엔진은 `market_t`로 분기하지
 * 않는다 — 분기하기 시작하면 시장이 셋이 될 때 모든 분기점을 찾아다녀야 한다.
 * 엔진이 보는 것은 이 인터페이스뿐이고, 시장은 테이블 하나로 표현된다.
 *
 * 규칙 함수는 상태를 갖지 않는다. 같은 인자에 항상 같은 답을 낸다 — 결정성 요건이다.
 */

/*
 * 세션 구간. KRX는 CLOSED와 REGULAR만 쓰고, NXT는 다섯 구간을 모두 쓴다.
 * 한 열거형으로 합친 이유는 엔진이 시장별로 다른 타입을 다루지 않게 하기 위해서다.
 */
typedef enum {
    SESSION_CLOSED = 0, /* 장이 닫혀 있다 */
    SESSION_PRE,        /* 프리마켓 (NXT) */
    SESSION_PRE_BREAK,  /* 오전 휴장 (NXT) */
    SESSION_REGULAR,    /* 정규장 / 메인마켓 */
    SESSION_POST_BREAK, /* 오후 휴장 (NXT) */
    SESSION_AFTER       /* 애프터마켓 (NXT) */
} market_session_t;

typedef struct market_rules {
    const char *name; /* 로그·테스트 식별용 */

    /*
     * 논리 시각이 거래 가능한 구간인가. 어느 구간인지는 *out으로 준다.
     * 닫혀 있어도 out에는 판정된 구간을 채운다 — 휴장 구간은 "닫혔지만 취소는 된다".
     */
    bool (*is_open)(ts_t ts, market_session_t *out);

    /* 이 구간에서 이 주문 유형을 받는가. */
    bool (*is_order_type_allowed)(market_session_t session, order_type_t type);

    /* 신규 주문을 받는가. 휴장 구간에서는 false. */
    bool (*can_submit)(market_session_t session);

    /* 취소를 받는가. 휴장 구간에서도 true인 것이 KRX·NXT 공통이다. */
    bool (*can_cancel)(market_session_t session);

    /*
     * 이 주문이 실제로 쓸 가격을 정한다.
     *
     * 지정가는 주문에 실린 가격을 그대로 돌려주면 된다. 중간가처럼 호가에 따라
     * 가격이 정해지는 주문은 여기서 계산한다. 정할 수 없으면 BOOK_PRICE_NONE.
     *
     * 엔진이 "중간가면 이렇게" 같은 분기를 갖지 않게 하는 것이 이 함수의 목적이다.
     */
    price_t (*resolve_price)(const order_book_t *book, const order_t *req);

    /*
     * 정정으로 가격을 지정할 수 있는 유형인가.
     *
     * 가격이 규칙에서 나오는 유형(중간가)은 false다. 그런 주문의 가격을 정정으로
     * 바꿀 수 있게 하면 두 가지가 깨진다 — 접수 시점 고정이라는 약속이 무너지고,
     * 재계산이 자기 주문을 포함하므로 정정할 때마다 가격이 반대편으로 밀려 올라간다.
     * 수량 정정은 여전히 가능하다.
     */
    bool (*allows_reprice)(order_type_t type);
} market_rules_t;

/*
 * 논리 시각의 해석 규약.
 *
 * ts_t는 나노초 단위 논리 시각이고, 세션 판정은 **그날 자정 기준 경과 시간**으로 한다.
 * 날짜를 보지 않는 이유는 이 시뮬레이터가 하루치 거래를 다루기 때문이다.
 * 여러 날을 이어 붙이더라도 하루로 접어서 보면 세션 판정은 그대로 성립한다.
 *
 * 시스템 시각을 읽는 곳은 없다. 여기 들어오는 ts는 전부 입력 이벤트가 들고 온 값이다.
 */
#define NS_PER_SEC 1000000000LL
#define NS_PER_DAY (86400LL * NS_PER_SEC)

/* 시:분:초를 자정 기준 나노초로. 세션 경계를 표에 그대로 적기 위한 것이다. */
#define TOD_NS(h, m, s) \
    ((((int64_t)(h) * 3600) + ((int64_t)(m) * 60) + (int64_t)(s)) * NS_PER_SEC)

/* 논리 시각을 그날 자정 기준 나노초로 접는다. 음수 시각도 안전하게 다룬다. */
static inline int64_t ts_time_of_day(ts_t ts)
{
    return (((int64_t)ts % NS_PER_DAY) + NS_PER_DAY) % NS_PER_DAY;
}

/* 각 시장의 규칙 테이블. 전역 상수이며 프로세스 수명 내내 살아 있다. */
extern const market_rules_t KRX_RULES;
extern const market_rules_t NXT_RULES;

/* 세션 이름. 정의되지 않은 값에도 NULL을 반환하지 않는다. */
const char *market_session_str(market_session_t session);

#endif /* MINI_SOR_MARKET_RULES_H */
