#ifndef MINI_SOR_BEST_EXECUTION_H
#define MINI_SOR_BEST_EXECUTION_H

#include <stdbool.h>
#include <stdint.h>

#include "consolidated.h"
#include "order.h"
#include "types.h"

/*
 * 최선집행기준 평가 (자본시장법 제68조).
 *
 * 증권사는 주문을 "최선의 조건"으로 집행할 의무가 있다. 그런데 최선이 무엇인지는
 * 한 가지 숫자가 아니다 — 가격이 좋아도 물량이 없으면 못 채우고, 물량이 있어도
 * 수수료가 비싸면 손해다. 그래서 네 항목을 각각 점수로 매기고 가중합한다.
 *
 *   가격        — 상대 최우선호가가 얼마나 유리한가
 *   체결 가능성 — 요구 수량을 지금 채울 수 있는가
 *   비용        — 시장별 수수료
 *   시장 상태   — 열려 있는가, 스프레드가 비정상적으로 넓지 않은가
 *
 * **항목별 점수를 따로 보관한다.** 합계만 남기면 "왜 그 시장을 골랐는가"를 설명할 수
 * 없다. 최선집행의무는 결과뿐 아니라 근거를 남기는 의무이기도 하고,
 * T2-12의 라우팅 로그가 이 값을 그대로 쓴다.
 *
 * **점수는 정수다.** 부동소수를 쓰면 같은 입력에 다른 순위가 나올 수 있고,
 * 그러면 전략 비교가 무의미해진다(결정성).
 */

/* 점수 척도. 0이 최악, BE_SCORE_MAX가 최선. */
#define BE_SCORE_MAX 10000

/*
 * 1bp(0.01%)의 불리함이 깎는 점수.
 *
 * 가격과 비용을 **같은 단위(bp)로 환산해 같은 계수로 깎는다.** 그래야 "수수료가 1bp
 * 싼 대신 가격이 1bp 불리한 시장"이 정확히 무승부가 된다. 두 항목의 척도가 다르면
 * 가중치를 아무리 조정해도 그 지점을 맞출 수 없다.
 *
 * 100이면 100bp(1%) 불리할 때 점수가 0이 된다. 국내 주식의 가격 제한폭이 ±30%이므로
 * 1% 차이는 라우팅 판단에서 이미 결정적인 크기다.
 */
#define BE_BP_PENALTY 100

/* 스프레드 1bp가 시장 상태 점수에서 깎는 값. 가격보다 완만하게 본다. */
#define BE_SPREAD_PENALTY 20

/* 한 시장에 대한 평가. */
typedef struct {
    bool eligible; /* 라우팅 후보인가 */
    int  reason;   /* eligible이 false일 때 그 이유(에러 코드) */

    /* 판단의 재료. 점수만 남기면 나중에 검산할 수 없다. */
    price_t quote;    /* 이 시장의 상대 최우선호가. 없으면 BOOK_PRICE_NONE */
    qty_t   fillable; /* 지정가 안에서 즉시 채울 수 있는 수량 */
    price_t spread;   /* 이 시장의 스프레드. 한쪽이 비면 0 */

    int32_t price_score;
    int32_t fill_score;
    int32_t cost_score;
    int32_t state_score;
    int32_t total; /* 가중평균. 0 ~ BE_SCORE_MAX */
} venue_score_t;

/* 항목별 가중치. 합이 0이면 평가할 수 없다. */
typedef struct {
    int32_t price;
    int32_t fill;
    int32_t cost;
    int32_t state;
} be_weights_t;

/*
 * 기본 가중치.
 *
 * 가격을 가장 무겁게 본다 — 최선집행의무의 1차 기준이 가격이기 때문이다.
 * 체결 가능성이 그다음이다. 못 채운 주문은 다음 호가로 넘어가 결국 더 비싸게
 * 체결되므로, 가격과 무관한 항목이 아니라 "지연된 가격"에 가깝다.
 * 비용과 시장 상태는 보조 지표다. 근거는 docs/decisions/0005 참조.
 */
extern const be_weights_t BE_WEIGHTS_DEFAULT;

/* 시장별 파라미터. */
typedef struct {
    int32_t fee_bp[MARKET_COUNT]; /* 수수료(bp). 음수면 거부 */
} be_config_t;

/* 기본 설정. NXT가 KRX보다 수수료가 낮다는 가정을 명시적으로 둔다. */
extern const be_config_t BE_CONFIG_DEFAULT;

/*
 * 두 시장을 평가해 out[]에 채운다.
 *
 * req는 평가 대상 주문이다 — side, price, qty를 본다. 매수면 상대편(매도) 호가를,
 * 매도면 매수 호가를 본다.
 *
 * 닫힌 시장, 상대 호가가 없는 시장, 지정가 안에 물량이 하나도 없는 시장은
 * `eligible = false`로 표시하고 점수를 0으로 둔다. **제외하지 않고 이유와 함께
 * 남긴다** — 왜 그 시장을 안 썼는지도 근거의 일부다.
 *
 * w나 cfg가 NULL이면 기본값을 쓴다.
 * 인자가 잘못되면 음수 에러 코드, 성공하면 ERR_OK.
 */
int be_evaluate(const cons_book_t *cons, const order_t *req, ts_t ts,
                const be_weights_t *w, const be_config_t *cfg,
                venue_score_t out[MARKET_COUNT]);

/*
 * 평가 결과에서 시장 하나를 고른다.
 *
 * 동점 규칙 — 총점이 같으면 **즉시 채울 수 있는 수량이 많은 시장**, 그것도 같으면
 * 시장 열거 순서. T2-01의 통합 호가창과 같은 방향의 규칙이다(둘 다 "물량이 많은
 * 쪽"으로 끊는다). 마지막 동률까지 결정적이어야 전략 비교가 성립한다.
 *
 * 후보가 하나도 없으면 ERR_NO_LIQUIDITY.
 */
int be_pick(const venue_score_t scores[MARKET_COUNT], market_t *out_market);

/*
 * key = value 설정 파일에서 가중치와 시장 파라미터를 읽는다.
 * T1-18의 divergent 설정과 같은 형식이다 — '#' 뒤는 주석, 빈 줄은 무시.
 *
 * 인식하는 키: weight_price, weight_fill, weight_cost, weight_state,
 * fee_krx_bp, fee_nxt_bp. 모르는 키는 거절한다 — 오타 난 설정으로 돌린 실험은
 * 나중에 해석할 수 없다.
 *
 * 적지 않은 키는 기본값을 그대로 쓴다. w나 cfg에 NULL을 주면 그쪽은 읽지 않는다.
 * 가중치 합이 0이거나 음수 값이 있으면 ERR_INVALID_ARG.
 * 파일을 못 열면 ERR_NOT_FOUND.
 */
int be_load_config(const char *path, be_weights_t *w, be_config_t *cfg);

#endif /* MINI_SOR_BEST_EXECUTION_H */
