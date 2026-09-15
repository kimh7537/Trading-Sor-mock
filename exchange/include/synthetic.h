#ifndef MINI_SOR_SYNTHETIC_H
#define MINI_SOR_SYNTHETIC_H

#include <stdint.h>

#include "order.h"
#include "types.h"

/*
 * 가상 참가자 — 확률적으로 주문을 만들어 호가창을 채운다.
 *
 * SOR을 시험하려면 양 시장에 호가가 있어야 하는데, 손으로 넣은 호가는 전략 간
 * 차이를 만들 만큼 다양하지 않다. 그래서 분포에서 뽑는다.
 *
 * **시드를 주입받는다. 전역 난수를 쓰지 않는다.**
 * 같은 시드는 항상 같은 주문 시퀀스를 만든다 — 이것이 T1-19 결정성 검증의 전제이고,
 * "전략 A와 B의 차이"를 말하려면 두 실행의 유동성이 같아야 한다는 요구에서 온다.
 *
 * 시스템 시각도 읽지 않는다. 시각은 start_ts에서 시작해 도착 간격만큼 스스로 나아간다.
 */

typedef struct {
    uint64_t seed; /* 0이면 생성 실패. 시드는 명시적으로 줘야 한다 */

    /*
     * 주문 도착은 포아송 과정이다. 도착 간격이 지수분포를 따른다.
     * arrival_per_sec은 초당 평균 주문 수. 0보다 커야 한다.
     */
    double arrival_per_sec;

    /*
     * 가격은 기준가에서 얼마나 떨어지는가로 정한다. 이격 폭(틱 수)이 지수분포를
     * 따르고, price_decay가 클수록 기준가 근처에 몰린다. 0보다 커야 한다.
     */
    double price_decay;

    /* 수량은 [qty_min, qty_max] 균등분포에서 뽑는다. */
    qty_t qty_min;
    qty_t qty_max;

    price_t ref_price;  /* 기준가. 이격의 중심 */
    price_t price_low;  /* 생성 가격의 하한. 보통 호가창의 제한폭 하한 */
    price_t price_high; /* 상한 */

    market_t   market;
    ts_t       start_ts; /* 첫 주문의 논리 시각 기준점 */
    order_id_t first_id; /* 주문번호 시작값. 0이면 생성 실패 */
} synth_config_t;

typedef struct synth_gen synth_gen_t;

/* 설정이 잘못됐거나 할당에 실패하면 NULL. */
synth_gen_t *synth_create(const synth_config_t *cfg);
void synth_destroy(synth_gen_t *gen);

/*
 * 다음 주문을 만들어 *out에 채운다. 항상 지정가다.
 * 주문번호는 1씩 증가하고, 논리 시각은 지수분포 도착 간격만큼 나아간다.
 * 인자가 NULL이면 ERR_NULL_PTR, 그 외에는 항상 ERR_OK.
 */
int synth_next(synth_gen_t *gen, order_t *out);

/* 지금까지 만든 주문 수. */
int64_t synth_count(const synth_gen_t *gen);

#endif /* MINI_SOR_SYNTHETIC_H */
