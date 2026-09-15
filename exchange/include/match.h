#ifndef MINI_SOR_MATCH_H
#define MINI_SOR_MATCH_H

#include <stdbool.h>
#include <stdint.h>

#include "event.h"
#include "market_rules.h"
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

/*
 * 이벤트 소비자를 건다. NULL을 주면 이벤트를 만들지 않는다(기본값).
 * 싱크는 엔진보다 오래 살아야 한다 — 엔진은 포인터만 복사한다.
 */
void match_set_sink(match_engine_t *eng, const event_sink_t *sink);

/*
 * 시장 규칙 테이블을 건다. NULL이면 세션·유형 검사를 하지 않는다(기본값) —
 * 규칙과 무관한 매칭 자체를 시험하는 테스트가 그 상태로 돈다.
 * 테이블은 엔진보다 오래 살아야 한다. 엔진은 포인터만 들고 있는다.
 */
void match_set_rules(match_engine_t *eng, const market_rules_t *rules);

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

/*
 * 미체결 잔량을 취소한다.
 *
 * out은 "이번 호출의 결과"다 — filled_qty는 0이고(이번 호출로 체결된 것이 없다),
 * remaining_qty에 취소된 잔량이 담긴다. status는 STATUS_CANCELED.
 *
 *
 * ts는 이 취소 요청의 논리 시각이다. 이벤트에 그대로 실린다 — 엔진은 시스템 시각을
 * 읽지 않으므로 취소도 자기 시각을 들고 와야 한다.
 * 이미 전량 체결된 주문은 호가창에도 인덱스에도 없으므로 ERR_NOT_FOUND다.
 */
int match_cancel(match_engine_t *eng, order_id_t id, ts_t ts,
                 exec_result_t *out);

/*
 * 주문을 정정한다 (docs/SPEC.md 4.4).
 *
 * new_qty는 **원 주문 수량**이다. 잔량이 아니다. 따라서 이미 체결된 수량보다
 * 커야 한다 — 같거나 작으면 사실상 취소이므로 ERR_INVALID_QTY로 거절한다.
 * 기체결 수량(filled_qty)은 정정으로 사라지지 않는다.
 *
 * 시간 우선순위
 *  - 같은 가격 + 수량 감소: 유지 (제자리에서 수량만 줄인다)
 *  - 가격 변경 또는 수량 증가: 상실 (떼었다가 큐 뒤에 다시 붙인다)
 *
 * 정정된 가격이 반대편 최우선호가와 교차하면 ERR_INVALID_PRICE로 거절한다.
 * 정정 시 매칭은 하지 않는다 — 단순화 지점이며, 교차한 채로 두면 호가창의
 * 기본 불변조건이 깨지므로 받지 않는 쪽을 택했다.
 *
 * 거절되면 원 주문은 가격·수량·우선순위 모두 그대로다.
 */
int match_modify(match_engine_t *eng, order_id_t id, price_t new_price,
                 qty_t new_qty, ts_t ts, exec_result_t *out);

#endif /* MINI_SOR_MATCH_H */
