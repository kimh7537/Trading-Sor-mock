#ifndef MINI_SOR_TYPES_H
#define MINI_SOR_TYPES_H

#include <stdint.h>

/*
 * 프로젝트 전역 기본 타입.
 *
 * 가격과 수량은 정수다. 부동소수를 쓰지 않는다 — 체결 단가 비교가 최종 산출물이므로
 * 반올림 오차가 누적되면 측정 자체를 신뢰할 수 없다.
 */

typedef int32_t  price_t;    /* 원 단위 정수 가격 */
typedef int32_t  qty_t;      /* 주식 수량 */
typedef uint64_t order_id_t; /* 주문 식별자. 0은 '없음' */
typedef int64_t  ts_t;       /* 논리 시각(나노초). 시스템 시각을 읽지 않는다 */

typedef enum {
    SIDE_BUY = 0,
    SIDE_SELL = 1
} side_t;

typedef enum {
    ORDER_LIMIT = 0,    /* 지정가 */
    ORDER_MARKET,       /* 시장가 */
    ORDER_IOC,          /* 즉시 체결분만, 잔량 취소 */
    ORDER_FOK,          /* 전량 즉시 체결 불가 시 전체 취소 */
    ORDER_MIDPOINT      /* 중간가. NXT 메인마켓 전용 */
} order_type_t;

typedef enum {
    STATUS_NEW = 0,
    STATUS_PARTIAL,
    STATUS_FILLED,
    STATUS_CANCELED,
    STATUS_REJECTED
} order_status_t;

typedef enum {
    MARKET_KRX = 0,
    MARKET_NXT = 1
} market_t;

#define MARKET_COUNT 2

/* --- 경계값 --- */

/* 호가 단위 최솟값이 1원이므로 유효 가격의 하한도 1원이다. */
#define PRICE_MIN ((price_t)1)
/* 국내 주식 최고가 종목도 수백만 원대다. 1천만 원이면 충분한 상한이고,
 * price_t(int32) 범위에 여유를 남겨 중간 계산에서 넘치지 않는다. */
#define PRICE_MAX ((price_t)10000000)

#define QTY_MIN ((qty_t)1)
#define QTY_MAX ((qty_t)1000000)

#define ORDER_ID_INVALID ((order_id_t)0)
#define TS_INVALID ((ts_t)-1)

/* 가격 제한폭: 전일 종가 기준 ±30% (docs/SPEC.md 3.1) */
#define PRICE_LIMIT_PCT 30

_Static_assert(PRICE_MIN < PRICE_MAX, "가격 하한이 상한보다 작아야 한다");
_Static_assert(QTY_MIN < QTY_MAX, "수량 하한이 상한보다 작아야 한다");
_Static_assert(PRICE_MAX <= INT32_MAX / (PRICE_LIMIT_PCT + 100) * 100,
               "상한가 계산(가격 x 130 / 100)이 price_t 범위를 넘지 않아야 한다");
_Static_assert((int64_t)PRICE_MAX * (int64_t)QTY_MAX < INT64_MAX,
               "체결 금액이 int64를 넘지 않아야 한다");

#endif /* MINI_SOR_TYPES_H */
