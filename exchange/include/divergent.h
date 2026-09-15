#ifndef MINI_SOR_DIVERGENT_H
#define MINI_SOR_DIVERGENT_H

#include <stdint.h>

#include "synthetic.h"
#include "types.h"

/*
 * 두 시장에 의도적으로 다른 유동성을 넣는다.
 *
 * SOR이 "유리한 시장으로 배분한다"고 말하려면 시장 간에 유불리가 실제로 있어야 한다.
 * 양쪽에 같은 분포로 호가를 채우면 라우팅 결정이 무의미해지고, 어떤 전략을 써도
 * 평균 체결 단가가 같게 나온다. 그러면 측정할 것이 없다.
 *
 * 그래서 시나리오를 프리셋으로 둔다. 각 프리셋은 시장별 생성기 설정 한 쌍이다.
 *
 * 시드 하나로 두 시장의 수열이 모두 정해진다. 시장마다 다른 시드를 파생시키므로
 * 두 시장이 같은 주문을 내지 않으면서도, 같은 시드는 항상 같은 장면을 만든다.
 */

typedef enum {
    SCENARIO_BALANCED = 0, /* 양 시장 유사 — 라우팅이 무의미한 대조군 */
    SCENARIO_KRX_THIN,     /* KRX 유동성 부족 */
    SCENARIO_NXT_THIN,     /* NXT 유동성 부족 */
    SCENARIO_CROSSED       /* 한쪽 최우선호가가 다른 쪽보다 유리 */
} scenario_t;

typedef struct {
    scenario_t scenario;
    uint64_t   seed;

    price_t ref_price;  /* 두 시장의 기준가. CROSSED는 여기서 한쪽을 밀어낸다 */
    price_t price_low;  /* 생성 가격 하한 */
    price_t price_high; /* 상한 */

    ts_t    start_ts;
    int32_t orders_per_market; /* 각 시장에 넣을 주문 수 */
} divergent_config_t;

typedef struct divergent divergent_t;

/* 설정이 잘못됐거나 할당에 실패하면 NULL. */
divergent_t *divergent_create(const divergent_config_t *cfg);
void divergent_destroy(divergent_t *div);

/*
 * 해당 시장의 생성기. divergent_t가 소유하므로 따로 파괴하지 않는다.
 * 잘못된 시장이면 NULL.
 */
synth_gen_t *divergent_gen(divergent_t *div, market_t market);

/* 프리셋이 정한 시장별 기준가. CROSSED에서 두 값이 다르다. */
price_t divergent_ref_price(const divergent_t *div, market_t market);

/*
 * key = value 형식 설정 파일을 읽는다. '#' 뒤는 주석, 빈 줄은 무시한다.
 *
 * 인식하는 키: scenario, seed, ref_price, price_low, price_high, start_ts,
 * orders_per_market. 모르는 키는 ERR_INVALID_ARG로 거절한다 — 조용히 무시하면
 * 오타 난 설정으로 돌린 실험 결과를 나중에 해석할 수 없다.
 *
 * 성공하면 ERR_OK, 파일을 못 열면 ERR_NOT_FOUND, 형식이 틀리면 ERR_INVALID_ARG.
 */
int divergent_load(const char *path, divergent_config_t *out);

/* 시나리오 이름. 정의되지 않은 값에도 NULL을 반환하지 않는다. */
const char *scenario_str(scenario_t scenario);

/* 이름으로 시나리오를 찾는다. 없으면 ERR_NOT_FOUND. */
int scenario_from_str(const char *name, scenario_t *out);

#endif /* MINI_SOR_DIVERGENT_H */
