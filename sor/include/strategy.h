#ifndef MINI_SOR_STRATEGY_H
#define MINI_SOR_STRATEGY_H

#include <stdbool.h>
#include <stdint.h>

#include "best_execution.h"
#include "consolidated.h"
#include "order.h"
#include "types.h"

/*
 * 집행 전략 — 논리 주문 하나를 어느 시장에 얼마씩 보낼지 정한다.
 *
 * **전략은 주문을 보내지 않는다. 계획만 만든다.** 이유가 셋이다.
 *
 *  1. T2-14가 네 전략을 **같은 유동성**에서 비교해야 한다. 계획은 호가창을 건드리지
 *     않는 값이므로, 같은 입력에 같은 계획이 나오는지를 집행 없이 그대로 검증할 수
 *     있다. 전략이 직접 집행하면 첫 전략이 호가창을 바꿔 놓아 비교가 성립하지 않는다
 *  2. SWEEP조차 통합 호가창만 보고 미리 계산된다 — "양 시장을 합쳐 가격 순으로
 *     훑은 것과 결과가 같아야 한다"가 곧 계획이 미리 정해진다는 뜻이다
 *  3. 부분 체결을 보고 다음 판단을 바꾸는 적응형 전략은 Phase 2 범위 밖이다.
 *     필요해지면 계획을 여러 번 만드는 것으로 표현할 수 있다
 *
 * 계획에는 **물리 주문번호가 없다.** 번호 배정과 논리↔물리 매핑은 T2-09가 맡는다.
 * 여기서 정해 버리면 T2-09가 이미 굳은 결정을 물려받는다.
 */

/* 계획의 다리 수 상한. 시장이 둘이므로 2면 충분하지만 여유를 둔다. */
#define PLAN_LEGS_MAX 8

/* 한 시장에 보낼 몫. */
typedef struct {
    market_t     market;
    qty_t        qty;
    price_t      limit_price; /* 이 시장에 보낼 지정가 */
    order_type_t type;
} plan_leg_t;

/*
 * 집행 계획.
 *
 * 불변조건: `leg_count > 0`이면 다리 수량의 합이 원 주문 수량과 **정확히** 같다.
 * 쪼개다가 한 주라도 새면 논리 주문의 잔량 계산이 전부 어긋난다.
 * `plan_validate()`가 그것을 검사한다.
 */
typedef struct {
    plan_leg_t legs[PLAN_LEGS_MAX];
    int32_t    leg_count;
    qty_t      planned_qty; /* 다리 수량의 합 */
    int        reason;      /* 계획을 못 세운 이유. 세웠으면 ERR_OK */
} exec_plan_t;

/*
 * 전략이 판단에 쓰는 재료. 전략마다 필요한 것이 달라서 묶어 둔다 —
 * KRX_ONLY는 통합 호가창조차 거의 안 보고, BEST_PRICE는 평가 결과를 쓴다.
 */
typedef struct {
    const cons_book_t  *cons;
    const be_weights_t *weights; /* NULL이면 기본값 */
    const be_config_t  *config;  /* NULL이면 기본값 */
    ts_t                ts;
} exec_context_t;

struct exec_strategy;

/*
 * 전략 테이블. T1-13의 `market_rules_t`와 같은 방식이다 — 전략을 늘리는 것이
 * 새 테이블 하나를 쓰는 일이어야지, 집행 경로에 분기를 더하는 일이면 안 된다.
 *
 * plan()은 계획을 세우면 ERR_OK, 못 세우면 음수 에러 코드를 돌려주고
 * out->reason에 같은 값을 남긴다. 실패해도 out은 항상 유효한 빈 계획이다.
 */
typedef struct exec_strategy {
    const char *name;
    int (*plan)(const struct exec_strategy *self, const exec_context_t *ctx,
                const order_t *req, exec_plan_t *out);
} exec_strategy_t;

/* 빈 계획으로 되돌린다. 전략은 시작할 때 이것을 부른다. */
void plan_init(exec_plan_t *plan);

/*
 * 다리를 하나 붙인다. qty가 0 이하면 아무것도 하지 않고 ERR_OK를 돌려준다 —
 * "이 시장에는 0주"를 다리로 남기면 T2-09가 빈 물리 주문을 만들게 된다.
 * 자리가 없으면 ERR_BOOK_FULL.
 */
int plan_add_leg(exec_plan_t *plan, market_t market, qty_t qty, price_t price,
                 order_type_t type);

/*
 * 계획이 원 주문과 맞는지 검사한다. 성공하면 ERR_OK.
 *
 * 비어 있지 않은 계획의 수량 합이 원 주문 수량과 다르면 ERR_INVALID_QTY.
 * 같은 시장이 두 번 나오면 ERR_DUPLICATE — 한 시장에 두 다리를 보내면
 * 논리↔물리 매핑이 복잡해지기만 하고 얻는 것이 없다.
 */
int plan_validate(const exec_plan_t *plan, const order_t *req);

/*
 * 즉시 체결되지 않는 주문을 **어느 시장에 등록할 것인가**.
 *
 * 지정가 주문은 상대 호가가 없어도 호가창에 등록되는 것이 정상이고, 어느 시장에
 * 등록하느냐도 라우팅 결정의 일부다. 임시방편이 아니라 별도의 판단이다 —
 * 최선집행 평가(T2-03)는 "지금 체결되는가"를 재므로 이 상황에 답을 주지 못한다.
 *
 * 여기서 주문을 거부하면 안 된다. 조용한 장에서 BEST_PRICE만 주문을 버리고
 * KRX_ONLY는 등록하면, **체결률 차이가 라우팅 품질과 무관한 이유로** 생긴다.
 * T2-14의 비교가 그것 때문에 오염된다.
 *
 * 고르는 규칙 — 열린 시장 중에서 상대 최우선호가가 유리한 쪽. 호가가 있는 시장이
 * 없는 시장을 이긴다(호가가 있다는 것은 곧 체결 기회가 가깝다는 뜻이다).
 * 그래도 같으면 시장 열거 순서.
 * 열린 시장이 하나도 없으면 ERR_MARKET_CLOSED.
 */
int plan_resting_market(const exec_context_t *ctx, const order_t *req,
                         market_t *out_market);

/* 전략 이름. NULL을 받아도 NULL을 반환하지 않는다. */
const char *strategy_name(const exec_strategy_t *strategy);

/*
 * 기준선 전략 — KRX에만 전량 보낸다.
 *
 * NXT를 아예 보지 않는다. 복수시장 이전의 집행을 그대로 흉내 낸 것이고,
 * **다른 전략의 개선폭은 전부 이 전략 대비로 말한다.**
 */
extern const exec_strategy_t STRATEGY_KRX_ONLY;

/*
 * BEST_PRICE — 최선집행 평가에서 이긴 시장 **하나**에 전량 보낸다.
 *
 * 쪼개지 않는다. 이긴 시장의 잔량이 모자라도 쪼개지 않고 그대로 보낸다 —
 * 못 채운 잔량은 그 시장에 등록된다. 쪼개는 것은 SPLIT과 SWEEP의 일이고,
 * 셋을 비교해야 "쪼개는 것이 이득인가"를 말할 수 있다.
 */
extern const exec_strategy_t STRATEGY_BEST_PRICE;

/*
 * SPLIT — 양 시장의 즉시 체결 가능 잔량에 비례해서 나눠 보낸다.
 *
 * 단수는 최대 나머지 방식(Hare quota)으로 배분한다 — 나머지가 큰 시장부터 한 주씩.
 * 다리 수량의 합은 원 주문 수량과 정확히 같다.
 */
extern const exec_strategy_t STRATEGY_SPLIT;

#endif /* MINI_SOR_STRATEGY_H */
