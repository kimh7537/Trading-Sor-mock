#ifndef MINI_SOR_COMPARE_H
#define MINI_SOR_COMPARE_H

#include <stdint.h>

#include "divergent.h"
#include "execution_quality.h"
#include "strategy.h"
#include "types.h"

/*
 * 전략 비교 실험 하네스 — **Phase 2의 산출물을 만드는 코드다.**
 *
 * CLAUDE.md가 못 박은 목표가 여기서 숫자가 된다.
 * "집행 전략별로 평균 체결 단가가 얼마나 달랐는가"를 근거와 함께 제시한다.
 *
 * ---
 *
 * **유동성이 전략마다 완전히 같아야 한다.**
 *
 * 전략은 호가창을 바꾼다. 네 전략을 같은 호가창에 차례로 돌리면 첫 전략이 물량을
 * 먹어 치우고 나머지는 남은 것으로 재게 된다 — 비교가 아니라 순서 측정이 된다.
 *
 * 그래서 **전략마다 매칭 엔진을 새로 만들고 같은 시드로 다시 채운다.** 난수가
 * 시드에서만 나오고 시스템 시각을 읽지 않으므로(T1-17/T1-18) 이것이 성립한다.
 * 측정 대상 주문(taker)도 같은 규칙으로 다시 만들어 네 전략에 같은 것을 먹인다.
 *
 * ---
 *
 * **매수만 낸다.**
 *
 * 매수와 매도의 평균 체결 단가를 한 숫자로 합치면 의미가 없다 — 매수는 낮을수록,
 * 매도는 높을수록 좋아서 평균이 서로를 상쇄한다. 방향을 섞으려면 표를 둘로 나눠야
 * 하고 그것은 이 태스크의 범위를 넘는다. 라우팅 로직은 방향 대칭이므로
 * (T2-05~08의 테스트가 매도 쪽도 확인한다) 한 방향으로 재도 결론은 같다.
 */

#define COMPARE_STRATEGY_COUNT 4
#define COMPARE_SCENARIO_COUNT 4

/*
 * 측정 대상 주문의 논리 주문번호 시작값.
 *
 * 물리 주문번호가 `논리 x 16 + 시장 + 1`이므로(T2-09), 논리번호가 작으면 물리번호가
 * 유동성 주문번호(KRX 1~, NXT 10억~)와 겹친다. 2억에서 시작하면 물리번호가 32억
 * 위로 올라가 두 구간 어디와도 만나지 않는다.
 */
#define COMPARE_LOGICAL_ID_BASE ((order_id_t)200000000)

typedef struct {
    uint64_t seed;

    price_t ref_price;
    price_t price_low;
    price_t price_high;
    ts_t    start_ts;

    int32_t liquidity_orders; /* 시장당 유동성 주문 수 */
    int32_t taker_orders;     /* 측정 대상 주문 수 */

    qty_t   taker_qty_min;
    qty_t   taker_qty_max;
    int32_t aggression_ticks; /* 지정가를 기준가에서 얼마나 위로 놓는가 */
} compare_config_t;

/* 기본 설정. 네 시나리오 모두 이 설정으로 돈다. */
extern const compare_config_t COMPARE_DEFAULT;

/* 한 (시나리오, 전략) 칸. */
typedef struct {
    const char *strategy;
    scenario_t  scenario;

    qty_t   order_qty;      /* 낸 수량 합 */
    qty_t   filled_qty;     /* 체결 수량 합 */
    int64_t notional;       /* 체결 금액 합 */
    int64_t bench_notional; /* 기준가로 다 체결됐다면 들었을 금액 합 */

    price_t avg_price;    /* notional / filled_qty */
    int32_t slippage_bp;  /* 기준가 대비. 불리할수록 양수 */
    int32_t fill_rate_bp; /* 10000bp = 100% */

    /* KRX_ONLY의 평균 체결 단가 대비. **양수면 더 싸게 샀다는 뜻이다.** */
    int32_t vs_krx_only_bp;

    int32_t rejected_legs;
} compare_row_t;

typedef struct {
    compare_config_t cfg;
    compare_row_t    row[COMPARE_SCENARIO_COUNT][COMPARE_STRATEGY_COUNT];
} compare_result_t;

/*
 * 시나리오 4종 x 전략 4종 = 16칸을 채운다.
 *
 * cfg가 NULL이면 COMPARE_DEFAULT를 쓴다. **같은 설정으로 두 번 부르면 결과가
 * 바이트까지 같다** — 시스템 시각도 전역 난수도 읽지 않는다.
 * 할당에 실패하면 ERR_POOL_EXHAUSTED, 설정이 잘못되면 ERR_INVALID_ARG.
 */
int compare_run(const compare_config_t *cfg, compare_result_t *out);

/*
 * 결과를 마크다운 표로 쓴다. 파일을 못 열면 ERR_NOT_FOUND.
 *
 * date는 파일에 적을 날짜 문자열이다 — **하네스가 시스템 시각을 읽지 않는다.**
 * 시각을 읽으면 같은 시드로 돌린 두 결과의 파일이 달라진다.
 */
int compare_write_md(const compare_result_t *res, const char *date,
                     const char *path);

/* 전략 테이블. 표의 열 순서이기도 하다. 0번이 기준선인 KRX_ONLY다. */
const exec_strategy_t *compare_strategy(int32_t index);

#endif /* MINI_SOR_COMPARE_H */
