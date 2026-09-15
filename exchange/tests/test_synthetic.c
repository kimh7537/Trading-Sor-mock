/*
 * T1-17 유동성 공급 — Synthetic.
 *
 * 완료 조건의 핵심은 "동일 시드 2회 실행 결과가 바이트 단위로 일치"다.
 * 그래서 이 테스트의 중심은 분포의 모양이 아니라 **재현성**이다.
 * 분포는 "대충 그 방향인가"만 본다 — 통계 검정을 여기서 할 이유가 없다.
 */
#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"
#include "match.h"
#include "synthetic.h"
#include "tick_size.h"

#define N 2000
#define BASE 10000

static synth_config_t base_cfg(uint64_t seed)
{
    synth_config_t cfg = {0};
    cfg.seed = seed;
    cfg.arrival_per_sec = 100.0;
    cfg.price_decay = 0.5; /* 평균 2틱 이격 */
    cfg.qty_min = 10;
    cfg.qty_max = 500;
    cfg.ref_price = BASE;
    cfg.price_low = 7000;
    cfg.price_high = 13000;
    cfg.market = MARKET_KRX;
    cfg.start_ts = 1000;
    cfg.first_id = 1;
    return cfg;
}

static void fill(synth_gen_t *gen, order_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        assert(synth_next(gen, &out[i]) == ERR_OK);
    }
}

/* 설정 검증 */
static void test_create_rejects(void)
{
    assert(synth_create(NULL) == NULL);

    synth_config_t c = base_cfg(1);
    c.seed = 0; /* 시드는 명시적으로 줘야 한다 */
    assert(synth_create(&c) == NULL);

    c = base_cfg(1);
    c.first_id = ORDER_ID_INVALID;
    assert(synth_create(&c) == NULL);

    c = base_cfg(1);
    c.arrival_per_sec = 0.0;
    assert(synth_create(&c) == NULL);
    c.arrival_per_sec = -1.0;
    assert(synth_create(&c) == NULL);

    c = base_cfg(1);
    c.price_decay = 0.0;
    assert(synth_create(&c) == NULL);

    c = base_cfg(1);
    c.qty_min = 0;
    assert(synth_create(&c) == NULL);
    c = base_cfg(1);
    c.qty_min = 100;
    c.qty_max = 50;
    assert(synth_create(&c) == NULL);

    c = base_cfg(1);
    c.ref_price = 20000; /* 범위 밖 */
    assert(synth_create(&c) == NULL);

    c = base_cfg(1);
    c.price_low = 0;
    assert(synth_create(&c) == NULL);

    synth_destroy(NULL);
    assert(synth_count(NULL) == 0);
}

/* 같은 시드 -> 바이트 단위로 같은 수열 */
static void test_same_seed_identical(void)
{
    static order_t a[N];
    static order_t b[N];

    synth_config_t cfg = base_cfg(0xC0FFEE);

    synth_gen_t *g1 = synth_create(&cfg);
    synth_gen_t *g2 = synth_create(&cfg);
    assert(g1 != NULL && g2 != NULL);

    fill(g1, a, N);
    fill(g2, b, N);

    assert(synth_count(g1) == N);
    assert(synth_count(g2) == N);
    assert(memcmp(a, b, sizeof(order_t) * N) == 0);

    synth_destroy(g1);
    synth_destroy(g2);
}

/* 다른 시드 -> 다른 수열. 이게 아니면 위 테스트가 무의미하다 */
static void test_different_seed_differs(void)
{
    static order_t a[N];
    static order_t b[N];

    synth_config_t c1 = base_cfg(0xC0FFEE);
    synth_config_t c2 = base_cfg(0xBEEF);

    synth_gen_t *g1 = synth_create(&c1);
    synth_gen_t *g2 = synth_create(&c2);
    assert(g1 != NULL && g2 != NULL);

    fill(g1, a, N);
    fill(g2, b, N);

    assert(memcmp(a, b, sizeof(order_t) * N) != 0);

    synth_destroy(g1);
    synth_destroy(g2);
}

/* 생성된 주문이 실제로 쓸 수 있는 형태인가 */
static void test_orders_are_valid(void)
{
    synth_config_t cfg = base_cfg(42);
    synth_gen_t *gen = synth_create(&cfg);
    assert(gen != NULL);

    order_t prev = {0};
    int n_buy = 0, n_sell = 0;
    qty_t qmin = QTY_MAX, qmax = 0;
    price_t pmin = PRICE_MAX, pmax = 0;

    for (int i = 0; i < N; i++) {
        order_t o;
        assert(synth_next(gen, &o) == ERR_OK);

        assert(o.id == (order_id_t)(i + 1)); /* 1씩 증가 */
        assert(o.type == ORDER_LIMIT);
        assert(o.market == MARKET_KRX);
        assert(o.filled_qty == 0);
        assert(o.prev == NULL && o.next == NULL);

        /* 논리 시각은 단조 증가한다 — 같은 시각에 두 주문이 겹치지 않는다 */
        if (i > 0) {
            assert(o.ts > prev.ts);
        }
        assert(o.ts > cfg.start_ts);

        assert(o.qty >= cfg.qty_min && o.qty <= cfg.qty_max);
        assert(o.price >= cfg.price_low && o.price <= cfg.price_high);
        assert(is_valid_tick(o.price)); /* 호가창이 받을 수 있는 가격 */

        if (o.side == SIDE_BUY) {
            n_buy++;
            assert(o.price <= cfg.ref_price); /* 매수는 기준가 아래 */
        } else {
            n_sell++;
            assert(o.price >= cfg.ref_price);
        }

        if (o.qty < qmin) {
            qmin = o.qty;
        }
        if (o.qty > qmax) {
            qmax = o.qty;
        }
        if (o.price < pmin) {
            pmin = o.price;
        }
        if (o.price > pmax) {
            pmax = o.price;
        }

        prev = o;
    }

    /* 한쪽으로 심하게 쏠리지 않는다. 동전 던지기라 2000번이면 넉넉한 범위다 */
    assert(n_buy > N / 3 && n_sell > N / 3);
    /* 수량·가격이 한 값에 고정돼 있지 않다 */
    assert(qmin < qmax);
    assert(pmin < pmax);

    synth_destroy(gen);
}

/*
 * 기준가가 호가 단위 구간 경계 근처면, 이격을 기준가의 틱으로 곱한 값이 다른
 * 구간에서는 유효 호가가 아니다. 4,990은 5원 단위인데 5,000부터는 10원 단위라
 * 4,990 + 5x11 = 5,045 같은 값이 나온다. 정렬을 빼먹으면 여기서 드러난다.
 */
static void test_tick_segment_crossing(void)
{
    synth_config_t cfg = base_cfg(99);
    cfg.ref_price = 4990;
    cfg.price_low = 3500;
    cfg.price_high = 6400;
    cfg.price_decay = 0.05; /* 평균 20틱 — 구간을 넘어가도록 넓게 */

    synth_gen_t *gen = synth_create(&cfg);
    assert(gen != NULL);

    int crossed = 0;
    for (int i = 0; i < N; i++) {
        order_t o;
        assert(synth_next(gen, &o) == ERR_OK);
        assert(is_valid_tick(o.price)); /* 어느 구간이든 유효 호가여야 한다 */
        assert(o.price >= cfg.price_low && o.price <= cfg.price_high);
        if (o.price >= 5000) {
            crossed++;
        }
    }
    assert(crossed > 0); /* 실제로 다른 구간까지 갔다 */

    synth_destroy(gen);
}

/*
 * 도착률이 극단적으로 높으면 간격이 1나노초 미만으로 나온다. 그래도 논리 시각은
 * 단조 증가해야 한다 — 같은 시각에 두 주문이 겹치면 시간 우선순위가 무너진다.
 */
static void test_extreme_arrival_rate(void)
{
    synth_config_t cfg = base_cfg(5);
    cfg.arrival_per_sec = 1.0e9; /* 평균 간격 1나노초 */

    synth_gen_t *gen = synth_create(&cfg);
    assert(gen != NULL);

    ts_t prev = cfg.start_ts;
    for (int i = 0; i < N; i++) {
        order_t o;
        assert(synth_next(gen, &o) == ERR_OK);
        assert(o.ts > prev);
        prev = o.ts;
    }

    synth_destroy(gen);
}

/* 감쇠 계수가 크면 기준가에 더 몰린다 */
static void test_decay_tightens(void)
{
    synth_config_t tight = base_cfg(7);
    tight.price_decay = 5.0; /* 평균 0.2틱 */
    synth_config_t loose = base_cfg(7);
    loose.price_decay = 0.1; /* 평균 10틱 */

    synth_gen_t *gt = synth_create(&tight);
    synth_gen_t *gl = synth_create(&loose);
    assert(gt != NULL && gl != NULL);

    int64_t sum_tight = 0, sum_loose = 0;
    for (int i = 0; i < N; i++) {
        order_t a, b;
        assert(synth_next(gt, &a) == ERR_OK);
        assert(synth_next(gl, &b) == ERR_OK);
        sum_tight += (a.price > BASE) ? (a.price - BASE) : (BASE - a.price);
        sum_loose += (b.price > BASE) ? (b.price - BASE) : (BASE - b.price);
    }
    assert(sum_tight < sum_loose);

    synth_destroy(gt);
    synth_destroy(gl);
}

/* 도착률이 높으면 같은 건수를 더 짧은 시간에 만든다 */
static void test_arrival_rate(void)
{
    synth_config_t fast = base_cfg(11);
    fast.arrival_per_sec = 1000.0;
    synth_config_t slow = base_cfg(11);
    slow.arrival_per_sec = 10.0;

    synth_gen_t *gf = synth_create(&fast);
    synth_gen_t *gs = synth_create(&slow);
    assert(gf != NULL && gs != NULL);

    order_t a = {0}, b = {0};
    for (int i = 0; i < N; i++) {
        assert(synth_next(gf, &a) == ERR_OK);
        assert(synth_next(gs, &b) == ERR_OK);
    }
    assert(a.ts < b.ts);

    synth_destroy(gf);
    synth_destroy(gs);
}

/* 만들어진 주문을 실제 엔진에 흘려 넣어 본다 */
static void test_feeds_engine(void)
{
    match_engine_t *eng = match_engine_create(BASE, 4096);
    assert(eng != NULL);
    const order_book_t *book = match_book(eng);

    synth_config_t cfg = base_cfg(2026);
    cfg.price_low = book_price_low(book);
    cfg.price_high = book_price_high(book);
    synth_gen_t *gen = synth_create(&cfg);
    assert(gen != NULL);

    int accepted = 0;
    for (int i = 0; i < N; i++) {
        order_t o;
        exec_result_t res;
        assert(synth_next(gen, &o) == ERR_OK);
        int rc = match_limit(eng, &o, &res);
        /* 풀이 찰 수는 있어도 그 밖의 이유로 거부되면 안 된다 */
        assert(rc == ERR_OK || rc == ERR_POOL_EXHAUSTED);
        if (rc == ERR_OK) {
            accepted++;
        }
    }
    assert(accepted > 0);

    /* 양쪽에 호가가 생겼다 */
    assert(book_best_bid(book) != BOOK_PRICE_NONE);
    assert(book_best_ask(book) != BOOK_PRICE_NONE);

    synth_destroy(gen);
    match_engine_destroy(eng);
}

int main(void)
{
    test_create_rejects();
    test_same_seed_identical();
    test_different_seed_differs();
    test_orders_are_valid();
    test_tick_segment_crossing();
    test_extreme_arrival_rate();
    test_decay_tightens();
    test_arrival_rate();
    test_feeds_engine();
    return 0;
}
