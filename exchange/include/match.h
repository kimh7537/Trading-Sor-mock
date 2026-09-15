#ifndef MINI_SOR_MATCH_H
#define MINI_SOR_MATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "order.h"
#include "order_book.h"
#include "types.h"

/*
 * 매칭 엔진 (docs/SPEC.md 4).
 *
 * 가격 우선 -> 시간 우선으로 상대 호가를 소진한다.
 * 체결 가격은 먼저 호가창에 있던 주문(maker)의 가격이다.
 *
 * 엔진이 호가창·주문 풀·주문 인덱스를 모두 소유한다. 들어오는 주문은 값으로 넘기고,
 * 잔량이 남아 호가창에 등록될 때만 엔진이 풀에서 슬롯을 꺼내 복사한다.
 * 소유권을 나누면 "전량 체결된 상대 주문의 슬롯을 누가 돌려주는가"가 모호해진다.
 *
 * 시스템 시각을 읽지 않는다. 모든 시각은 들어오는 주문이 들고 온 논리 시각을 쓴다.
 */

/*
 * 한 번의 주문이 만들어 낼 수 있는 체결 건수 상한.
 * ponytail: 고정 배열. 넘치면 집계는 정확히 유지하고 목록만 잘린다(truncated).
 * T1-12에서 이벤트 싱크가 들어오면 목록은 그쪽으로 흘려보내고 이 배열은 없앤다.
 */
#define EXEC_FILLS_MAX 64

/* 체결 한 건. */
typedef struct {
    price_t    price;    /* 체결 가격 = maker의 호가 */
    qty_t      qty;
    order_id_t maker_id; /* 호가창에 있던 쪽 */
    order_id_t taker_id; /* 들어온 쪽 */
    ts_t       ts;       /* taker가 들고 온 논리 시각 */
} fill_t;

/* 주문 하나를 처리한 결과. */
typedef struct {
    fill_t  fills[EXEC_FILLS_MAX];
    int32_t fill_count;
    bool    truncated; /* 체결 건수가 EXEC_FILLS_MAX를 넘어 목록이 잘렸다 */

    qty_t   filled_qty; /* 총 체결 수량. 목록이 잘려도 정확하다 */
    int64_t notional;   /* 총 체결 금액. 평균 단가 = notional / filled_qty */
    qty_t   remaining_qty;
    bool    resting; /* 잔량이 호가창에 등록됐는가 */

    order_status_t status;
} exec_result_t;

typedef struct match_engine match_engine_t;

/*
 * 기준가로 호가창을, capacity로 주문 풀과 인덱스를 잡는다.
 * capacity는 동시에 호가창에 살아 있을 수 있는 주문 수의 상한이다.
 * 인자가 잘못됐거나 할당에 실패하면 NULL.
 */
match_engine_t *match_engine_create(price_t base_price, int32_t capacity);
void match_engine_destroy(match_engine_t *eng);

/* 호가 조회용. 엔진이 소유하므로 호출부가 파괴하지 않는다. */
const order_book_t *match_book(const match_engine_t *eng);

/*
 * 지정가 주문을 접수한다.
 *
 * req는 틀이다 — 엔진이 읽기만 하고 보관하지 않는다. id, side, price, qty, ts,
 * market을 채워 넣는다. filled_qty는 0이어야 한다.
 *
 * 매수는 최우선매도호가가 지정가 이하인 동안, 매도는 반대로 체결한다.
 * 남은 잔량은 호가창에 등록된다.
 *
 * 성공하면 ERR_OK. 가격 제한폭 밖이면 ERR_PRICE_LIMIT, 호가 단위에 안 맞으면
 * ERR_INVALID_TICK, 수량이 범위 밖이면 ERR_INVALID_QTY, 주문번호가 겹치면
 * ERR_DUPLICATE, 등록할 자리가 없으면 ERR_POOL_EXHAUSTED.
 * 거부되면 호가창은 전혀 바뀌지 않는다.
 */
int match_limit(match_engine_t *eng, const order_t *req, exec_result_t *out);

/*
 * 시장가 주문을 접수한다. req->price는 무시한다 — 가격 제한 없이 상대 호가를
 * 최우선부터 소진한다.
 *
 * 잔량이 남아도 호가창에 등록하지 않는다. 미체결분은 그대로 취소된다
 * (out->resting은 항상 false).
 * 반대 호가가 전혀 없으면 아무것도 체결하지 않고 ERR_NO_LIQUIDITY로 거부한다.
 */
int match_market(match_engine_t *eng, const order_t *req, exec_result_t *out);

/*
 * IOC — 즉시 체결 가능한 만큼만 체결하고 잔량을 취소한다.
 * 가격 검증은 지정가와 같다. 지정가와 다른 점은 잔량을 등록하지 않는다는 것뿐이다.
 * 한 건도 체결되지 않으면 ERR_NO_LIQUIDITY.
 */
int match_ioc(match_engine_t *eng, const order_t *req, exec_result_t *out);

/*
 * FOK — 전량 즉시 체결이 불가능하면 아무것도 체결하지 않는다.
 *
 * 체결 가능 수량을 먼저 세어 보고 모자라면 호가창을 건드리지 않은 채 거부한다.
 * 부분 체결 후 되돌리는 방식이 아니다 — 되돌리기는 시간 우선순위를 복원할 수 없다.
 * 전량 체결 불가면 ERR_NO_LIQUIDITY.
 */
int match_fok(match_engine_t *eng, const order_t *req, exec_result_t *out);

#endif /* MINI_SOR_MATCH_H */
