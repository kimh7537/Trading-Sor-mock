#ifndef MINI_SOR_ROUTING_LOG_H
#define MINI_SOR_ROUTING_LOG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "best_execution.h"
#include "strategy.h"
#include "types.h"

/*
 * 라우팅 판단 근거 로깅.
 *
 * 최선집행의무(자본시장법 제68조)는 결과뿐 아니라 **근거를 남기는 의무**이기도 하다.
 * "왜 이 시장에 보냈는가"에 답하지 못하면 의무를 지켰다고 말할 수 없다.
 *
 * ---
 *
 * **로그만 보고 라우팅 결정을 재현할 수 있어야 한다.**
 *
 * 그래서 결과(고른 시장)만 남기지 않는다. 판단에 들어간 것을 전부 남긴다.
 *
 *  - **재료**: 시장별 상대 최우선호가, 즉시 체결 가능 수량, 스프레드
 *  - **기준**: 가중치와 수수료 설정 — 같은 재료도 기준이 다르면 답이 달라진다
 *  - **중간값**: 항목별 점수 네 개와 총점
 *  - **결과**: 시장별 배분 수량
 *
 * 이만큼 있으면 로그를 읽는 쪽이 총점을 다시 계산해 검산할 수 있고, 어느 항목이
 * 판단을 뒤집었는지도 짚을 수 있다. 총점만 남기면 "가격 때문인가 수수료 때문인가"를
 * 영원히 알 수 없다.
 *
 * ---
 *
 * **이벤트 싱크 방식이다(T1-12와 같은 형태).**
 *
 * 로깅이 파일을 열거나 시각을 읽으면 SOR 엔진이 결정적이지 않게 된다. 싱크는 값을
 * 넘겨받기만 하고, 그것으로 무엇을 할지는 소비자가 정한다. 벤치는 메모리에 쌓고,
 * 운영은 파일에 쓰고, 테스트는 바이트 단위로 비교한다.
 *
 * **순서는 결정적이다.** 주문 하나에 결정 하나가 그 주문을 처리하는 시점에 나온다.
 * 같은 입력 시퀀스는 같은 결정 시퀀스를 만든다 — T1-19와 같은 성질이고,
 * 전략 비교(T2-14)가 성립하는 전제다.
 */

/* 라우팅 결정 한 건. */
typedef struct {
    order_id_t logical_id;
    ts_t       ts;
    side_t     side;
    price_t    limit_price;
    qty_t      order_qty;

    /* 어떤 전략이 판단했는가. 전략 테이블의 정적 문자열을 가리킨다. */
    const char *strategy;

    /* 판단의 재료와 항목별 점수. be_evaluate()의 결과 그대로다. */
    venue_score_t scores[MARKET_COUNT];

    /* 같은 재료라도 기준이 다르면 답이 달라진다. 기준도 함께 남긴다. */
    be_weights_t weights;
    be_config_t  config;

    /* 결과 — 시장별 배분 수량. 다리가 없는 시장은 0. */
    qty_t   alloc[MARKET_COUNT];
    int32_t leg_count;
    int     reason; /* 계획을 세웠으면 ERR_OK, 못 세웠으면 그 이유 */
} routing_decision_t;

/*
 * 결정 소비자. ctx는 소비자의 것이고 SOR 엔진은 손대지 않는다.
 * 콜백 안에서 전략을 다시 돌리지 않는다 — 재진입은 결정 순서를 흐트러뜨린다.
 */
typedef void (*routing_fn_t)(const routing_decision_t *d, void *ctx);

typedef struct {
    routing_fn_t fn;
    void        *ctx;
} routing_sink_t;

/* 싱크가 없거나(fn == NULL) 인자가 NULL이면 아무 일도 하지 않는다. */
void routing_emit(const routing_sink_t *sink, const routing_decision_t *d);

/*
 * 전략을 돌리고 그 판단 근거를 싱크로 흘려보낸다.
 *
 * 계획은 out에 담기고 반환값은 전략의 반환값 그대로다. **전략이 계획을 못 세워도
 * 결정은 남긴다** — 왜 아무 데도 안 보냈는지가 근거의 일부다.
 *
 * ctx의 가중치·설정이 NULL이면 기본값을 쓰고, 기록에는 실제로 쓴 값이 남는다.
 * sink는 NULL이어도 된다(그러면 그냥 전략만 돈다).
 */
int routing_plan(const exec_strategy_t *strategy, const exec_context_t *ctx,
                 const order_t *req, const routing_sink_t *sink,
                 exec_plan_t *out);

/*
 * 결정을 한 줄 텍스트로 쓴다. 사람이 읽고, 검산할 수 있는 형태다.
 *
 *   id=7 ts=43200000000000 side=BUY limit=10000 qty=100 strategy=SPLIT
 *   w=40/30/20/10 fee=3/2 KRX[elig=1 quote=10000 fill=300 spread=10
 *   s=9000/10000/9700/9800 total=9490 alloc=30] NXT[...] legs=2 reason=0
 *
 * 성공하면 쓴 길이(널 제외), 버퍼가 모자라면 ERR_INVALID_ARG.
 * 시각이나 난수를 읽지 않는다 — 같은 결정은 언제 불러도 같은 문자열이 된다.
 */
int routing_format(const routing_decision_t *d, char *buf, size_t cap);

#endif /* MINI_SOR_ROUTING_LOG_H */
