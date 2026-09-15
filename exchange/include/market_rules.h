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
} session_t;

typedef struct market_rules {
    const char *name; /* 로그·테스트 식별용 */

    /*
     * 논리 시각이 거래 가능한 구간인가. 어느 구간인지는 *out으로 준다.
     * 닫혀 있어도 out에는 판정된 구간을 채운다 — 휴장 구간은 "닫혔지만 취소는 된다".
     */
    bool (*is_open)(ts_t ts, session_t *out);

    /* 이 구간에서 이 주문 유형을 받는가. */
    bool (*is_order_type_allowed)(session_t session, order_type_t type);

    /* 신규 주문을 받는가. 휴장 구간에서는 false. */
    bool (*can_submit)(session_t session);

    /* 취소를 받는가. 휴장 구간에서도 true인 것이 KRX·NXT 공통이다. */
    bool (*can_cancel)(session_t session);

    /*
     * 이 주문이 실제로 쓸 가격을 정한다.
     *
     * 지정가는 주문에 실린 가격을 그대로 돌려주면 된다. 중간가처럼 호가에 따라
     * 가격이 정해지는 주문은 여기서 계산한다. 정할 수 없으면 BOOK_PRICE_NONE.
     *
     * 엔진이 "중간가면 이렇게" 같은 분기를 갖지 않게 하는 것이 이 함수의 목적이다.
     */
    price_t (*resolve_price)(const order_book_t *book, const order_t *req);
} market_rules_t;

/* 세션 이름. 정의되지 않은 값에도 NULL을 반환하지 않는다. */
const char *session_str(session_t session);

#endif /* MINI_SOR_MARKET_RULES_H */
