#include "synthetic.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#include "errors.h"
#include "tick_size.h"

/*
 * 난수는 xorshift64*다. 상태가 uint64 하나뿐이고 주기가 2^64-1이라 이 용도에 충분하다.
 * rand()를 쓰지 않는 이유는 품질이 아니라 **전역 상태** 때문이다 — 전역 상태를 쓰면
 * 두 시장의 생성기가 서로의 수열에 영향을 주고, 호출 순서가 결과를 바꾼다.
 * 생성기마다 상태를 따로 들면 그 문제가 사라진다.
 */
struct synth_gen {
    synth_config_t cfg;
    uint64_t       state;
    order_id_t     next_id;
    ts_t           now;
    int64_t        count;
};

static uint64_t next_u64(synth_gen_t *gen)
{
    uint64_t x = gen->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    gen->state = x;
    return x * 0x2545F4914F6CDD1DULL;
}

/* (0, 1) 열린 구간. 지수분포에 log(u)를 쓰므로 0이 나오면 안 된다. */
static double next_unit(synth_gen_t *gen)
{
    /* 상위 53비트만 쓴다 — double의 가수부 폭이다. */
    uint64_t bits = next_u64(gen) >> 11;
    double u = ((double)bits + 0.5) / 9007199254740992.0; /* 2^53 */
    assert(u > 0.0 && u < 1.0);
    return u;
}

/* 평균 1/rate인 지수분포 표본. */
static double next_exponential(synth_gen_t *gen, double rate)
{
    return -log(next_unit(gen)) / rate;
}

synth_gen_t *synth_create(const synth_config_t *cfg)
{
    if (cfg == NULL || cfg->seed == 0 || cfg->first_id == ORDER_ID_INVALID) {
        return NULL;
    }
    if (!(cfg->arrival_per_sec > 0.0) || !(cfg->price_decay > 0.0)) {
        return NULL; /* NaN도 여기서 걸린다 */
    }
    if (cfg->qty_min < QTY_MIN || cfg->qty_max > QTY_MAX ||
        cfg->qty_min > cfg->qty_max) {
        return NULL;
    }
    if (cfg->price_low < PRICE_MIN || cfg->price_high > PRICE_MAX ||
        cfg->price_low > cfg->price_high) {
        return NULL;
    }
    if (cfg->ref_price < cfg->price_low || cfg->ref_price > cfg->price_high) {
        return NULL;
    }

    synth_gen_t *gen = calloc(1, sizeof(*gen));
    if (gen == NULL) {
        return NULL;
    }
    gen->cfg = *cfg;
    gen->state = cfg->seed; /* xorshift는 0이 아닌 상태가 필요하다 */
    gen->next_id = cfg->first_id;
    gen->now = cfg->start_ts;
    gen->count = 0;

    return gen;
}

void synth_destroy(synth_gen_t *gen)
{
    free(gen);
}

/* 기준가에서 offset_ticks만큼 떨어진 유효 호가. 범위를 벗어나면 잘라낸다. */
static price_t price_at_offset(const synth_gen_t *gen, side_t side,
                               double offset_ticks)
{
    price_t ref = gen->cfg.ref_price;
    price_t tick = tick_size_of(ref);
    assert(tick > 0);

    /* 이격이 지나치게 크면 어차피 잘리므로 미리 막아 오버플로를 피한다. */
    if (offset_ticks > 1.0e6) {
        offset_ticks = 1.0e6;
    }
    int64_t steps = (int64_t)offset_ticks;
    int64_t delta = steps * (int64_t)tick;

    /* 매수는 기준가 아래, 매도는 위에 놓는다 — 스프레드가 생긴다. */
    int64_t raw =
        (side == SIDE_BUY) ? (int64_t)ref - delta : (int64_t)ref + delta;

    if (raw < (int64_t)gen->cfg.price_low) {
        raw = gen->cfg.price_low;
    }
    if (raw > (int64_t)gen->cfg.price_high) {
        raw = gen->cfg.price_high;
    }

    /*
     * 기준가의 호가 단위로 곱했으므로 다른 구간으로 넘어가면 어긋날 수 있다.
     * 매수는 내림, 매도는 올림으로 맞춘다 — 각자 제 방향으로 미는 것이 자연스럽다.
     */
    price_t aligned = round_to_tick((price_t)raw, side == SIDE_SELL);
    if (aligned == 0) {
        aligned = (price_t)raw;
    }
    if (aligned < gen->cfg.price_low) {
        aligned = round_to_tick(gen->cfg.price_low, true);
    }
    if (aligned > gen->cfg.price_high) {
        aligned = round_to_tick(gen->cfg.price_high, false);
    }
    return aligned;
}

int synth_next(synth_gen_t *gen, order_t *out)
{
    if (gen == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    /*
     * 뽑는 순서가 곧 수열의 소비 순서다. 순서를 바꾸면 같은 시드라도 다른 결과가
     * 나오므로, 이 순서 자체가 재현성 계약의 일부다. 함부로 바꾸지 않는다.
     *   1) 도착 간격  2) 매수/매도  3) 가격 이격  4) 수량
     */
    double gap_sec = next_exponential(gen, gen->cfg.arrival_per_sec);
    int64_t gap_ns = (int64_t)(gap_sec * 1.0e9);
    if (gap_ns < 1) {
        gap_ns = 1; /* 같은 시각에 두 주문이 겹치지 않게 최소 1나노초는 나아간다 */
    }
    gen->now += gap_ns;

    side_t side = (next_u64(gen) & 1u) ? SIDE_SELL : SIDE_BUY;
    double offset = next_exponential(gen, gen->cfg.price_decay);

    qty_t span = gen->cfg.qty_max - gen->cfg.qty_min + 1;
    qty_t qty = gen->cfg.qty_min + (qty_t)(next_u64(gen) % (uint64_t)span);

    *out = (order_t){0};
    out->id = gen->next_id++;
    out->ts = gen->now;
    out->side = side;
    out->price = price_at_offset(gen, side, offset);
    out->qty = qty;
    out->type = ORDER_LIMIT;
    out->market = gen->cfg.market;

    gen->count++;
    return ERR_OK;
}

int synth_set_ref_price(synth_gen_t *gen, price_t ref)
{
    if (gen == NULL) {
        return ERR_NULL_PTR;
    }
    if (ref < gen->cfg.price_low) {
        ref = gen->cfg.price_low;
    }
    if (ref > gen->cfg.price_high) {
        ref = gen->cfg.price_high;
    }
    gen->cfg.ref_price = ref;
    return ERR_OK;
}

price_t synth_ref_price(const synth_gen_t *gen)
{
    return gen != NULL ? gen->cfg.ref_price : 0;
}

int64_t synth_count(const synth_gen_t *gen)
{
    return gen != NULL ? gen->count : 0;
}
