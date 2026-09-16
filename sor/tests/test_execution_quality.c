/*
 * T2-13 집행 품질 측정.
 *
 * 완료 조건: **손으로 계산한 값과 정확히 일치**해야 한다.
 * 그래서 이 파일의 기대값은 전부 주석에 계산 과정을 적어 둔다. 코드가 낸 값을
 * 그대로 베껴 적으면 테스트가 아니라 현상 기록이 된다.
 *
 *  - 평균 체결 단가 = 체결금액 / 체결수량 (정수)
 *  - 슬리피지 = 평균 체결 단가 - 기준가 (불리할수록 양수)
 *  - 체결률 = 체결수량 / 주문수량
 *  - bp 환산: 1bp = 0.01%, 0에서 먼 쪽으로 반올림
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"
#include "execution_quality.h"
#include "match.h"

#define BASE 10000
#define CAP 256
#define T_BOTH TOD_NS(12, 0, 0)
#define T_NXT_ONLY TOD_NS(18, 0, 0)

static order_id_t MAKER_ID = 1;

/* --- 1. bp 환산과 반올림 --- */

static void test_bp_rounding(void)
{
    /* 딱 떨어지는 값. 100 / 10000 = 1% = 100bp */
    assert(eq_to_bp(100, 10000) == 100);
    assert(eq_to_bp(1, 10000) == 1);
    assert(eq_to_bp(0, 10000) == 0);
    assert(eq_to_bp(10000, 10000) == BP_SCALE);

    /*
     * 0에서 먼 쪽으로 반올림.
     * 1 / 30000 = 0.3333bp -> 나머지 두 배(2 x 10000)가 30000 미만 -> 0
     * 2 / 30000 = 0.6667bp -> 나머지 두 배(2 x 20000)가 30000 이상 -> 1
     */
    assert(eq_to_bp(1, 30000) == 0);
    assert(eq_to_bp(2, 30000) == 1);

    /* 정확히 0.5bp는 올린다. 1 x 10000 / 20000 = 0.5 -> 1 */
    assert(eq_to_bp(1, 20000) == 1);

    /* 음수도 대칭이다. 같은 크기면 부호만 다르다. */
    assert(eq_to_bp(-1, 30000) == 0);
    assert(eq_to_bp(-2, 30000) == -1);
    assert(eq_to_bp(-1, 20000) == -1);
    assert(eq_to_bp(-100, 10000) == -100);

    /* 대칭성을 넓게 확인한다 — 한쪽만 버림이면 여기서 걸린다. */
    for (int64_t v = -50; v <= 50; v++) {
        assert(eq_to_bp(v, 30000) == -eq_to_bp(-v, 30000));
        assert(eq_to_bp(v, 7777) == -eq_to_bp(-v, 7777));
    }

    /* 나눌 기준이 없으면 비율도 없다. */
    assert(eq_to_bp(100, 0) == 0);
    assert(eq_to_bp(100, -1) == 0);
}

static void test_avg_price(void)
{
    /* 1주 10000 + 2주 10010 = 30020, 3주 -> 10006.67 -> 버림 10006 */
    assert(eq_avg_price(30020, 3) == 10006);
    assert(eq_avg_price(100000, 10) == 10000);
    assert(eq_avg_price(0, 0) == 0);
    assert(eq_avg_price(100, -1) == 0);
}

/* --- 2. 손계산 대조 --- */

/*
 * 매수 100주, 기준가 10,000원.
 * 40주 @10,000 + 60주 @10,010 = 400,000 + 600,600 = 1,000,600원
 *
 *   평균 단가 = 1,000,600 / 100 = 10,006원 (딱 떨어진다)
 *   슬리피지  = 10,006 - 10,000 = 6원
 *   기준 금액 = 10,000 x 100 = 1,000,000원
 *   bp        = (1,000,600 - 1,000,000) x 10,000 / 1,000,000 = 6bp
 *   체결률    = 100 / 100 = 10,000bp
 */
static void test_buy_hand_computed(void)
{
    eq_metrics_t m;
    assert(eq_measure(SIDE_BUY, 100, 100, 1000600, 10000, &m) == ERR_OK);

    assert(m.avg_price == 10006);
    assert(m.slippage == 6);
    assert(m.slippage_bp == 6);
    assert(m.fill_rate_bp == 10000);
    assert(m.notional == 1000600);
}

/*
 * 매도 100주, 기준가 10,000원.
 * 40주 @10,000 + 60주 @9,990 = 400,000 + 599,400 = 999,400원
 *
 *   평균 단가 = 999,400 / 100 = 9,994원
 *   슬리피지  = 10,000 - 9,994 = 6원  (매도는 싸게 팔수록 불리하다)
 *   bp        = (1,000,000 - 999,400) x 10,000 / 1,000,000 = 6bp
 *
 * 매수와 **같은 6bp**가 나와야 한다. 방향만 다른 같은 크기의 불리함이다.
 */
static void test_sell_hand_computed(void)
{
    eq_metrics_t m;
    assert(eq_measure(SIDE_SELL, 100, 100, 999400, 10000, &m) == ERR_OK);

    assert(m.avg_price == 9994);
    assert(m.slippage == 6);
    assert(m.slippage_bp == 6);
    assert(m.fill_rate_bp == 10000);
}

/* 기준가보다 유리하게 체결되면 슬리피지가 음수다. */
static void test_favorable_is_negative(void)
{
    eq_metrics_t m;

    /* 매수 100주를 9,990원에 다 채웠다. 999,000원. 10원 유리. -10bp */
    assert(eq_measure(SIDE_BUY, 100, 100, 999000, 10000, &m) == ERR_OK);
    assert(m.avg_price == 9990);
    assert(m.slippage == -10);
    assert(m.slippage_bp == -10);

    /* 매도 100주를 10,010원에. 1,001,000원. 10원 유리. -10bp */
    assert(eq_measure(SIDE_SELL, 100, 100, 1001000, 10000, &m) == ERR_OK);
    assert(m.slippage == -10);
    assert(m.slippage_bp == -10);
}

/*
 * 부분 체결.
 * 주문 100주, 35주만 체결. 10주 @10,000 + 25주 @10,010 = 100,000 + 250,250
 *                                                      = 350,250원
 *   평균 단가 = 350,250 / 35 = 10,007.14... -> 버림 10,007
 *   기준 금액 = 10,000 x 35 = 350,000
 *   bp        = 250 x 10,000 / 350,000 = 7.142... -> 7bp
 *   체결률    = 35 / 100 = 3,500bp (35%)
 *
 * **슬리피지 bp는 체결된 수량 기준이다.** 못 채운 65주는 가격이 없으므로 평균에
 * 섞을 수 없다. 못 채운 것은 체결률이 말한다 — 두 지표를 나눠 두는 이유다.
 */
static void test_partial_fill(void)
{
    eq_metrics_t m;
    assert(eq_measure(SIDE_BUY, 100, 35, 350250, 10000, &m) == ERR_OK);

    assert(m.avg_price == 10007);
    assert(m.slippage == 7);
    assert(m.slippage_bp == 7);
    assert(m.fill_rate_bp == 3500);
}

/*
 * **bp를 평균 단가에서 내면 틀리는 경우.**
 *
 * 주문 3주, 기준가 10,000. 1주 @10,000 + 2주 @10,010 = 30,020원.
 *   평균 단가 = 30,020 / 3 = 10,006.67 -> 버림 10,006
 *   평균 단가로 내면: (10,006 - 10,000) / 10,000 = 6bp
 *   금액으로 내면  : 20 x 10,000 / 30,000 = 6.67 -> 7bp
 *
 * 정답은 7bp다. 버림 오차 1원이 그대로 1bp를 먹었다 — 그래서 bp는 금액에서 낸다.
 */
static void test_bp_not_derived_from_avg_price(void)
{
    eq_metrics_t m;
    assert(eq_measure(SIDE_BUY, 3, 3, 30020, 10000, &m) == ERR_OK);

    assert(m.avg_price == 10006);
    assert(m.slippage == 6);    /* 표시용은 평균 단가에서 온다 */
    assert(m.slippage_bp == 7); /* 측정용은 금액에서 온다 */
}

/*
 * **전략끼리 비교할 때도 평균 단가를 거치면 틀린다**(T6-07).
 *
 * 기준선 100주에 1,001,990원(평균 10,019.90), 비교 대상 100주에 1,001,400원
 * (평균 10,014.00). 실제 차이는 5.90원 / 10,019.90 = 5.888bp -> 6bp.
 *   원 단위로 버린 평균끼리 빼면: (10,019 - 10,014) / 10,019 = 4.99 -> 5bp
 *
 * 1bp가 사라졌다. 전략 비교 표의 차이가 1~6bp 크기라 이 오차가 결론을 흔든다.
 */
static void test_avg_diff_not_derived_from_rounded_avg(void)
{
    /* 버린 평균으로 계산하면 5가 나온다는 것을 먼저 못 박는다 */
    price_t ra = eq_avg_price(1001990, 100);
    price_t rb = eq_avg_price(1001400, 100);
    assert(ra == 10019 && rb == 10014);
    assert(eq_to_bp((int64_t)ra - rb, ra) == 5);

    /* 체결 금액에서 직접 내면 6이다 */
    assert(eq_avg_diff_bp(1001990, 100, 1001400, 100) == 6);

    /* 방향이 바뀌면 부호만 바뀐다 */
    assert(eq_avg_diff_bp(1001400, 100, 1001990, 100) == -6);

    /* 같은 평균이면 0. 수량이 달라도 평균이 같으면 0이다 */
    assert(eq_avg_diff_bp(1001400, 100, 3004200, 300) == 0);

    /*
     * **분모는 기준(a)의 평균이다.** 차이가 몇 bp일 때는 분모를 a로 하든 b로
     * 하든 반올림 결과가 같아서 드러나지 않는다(변이 E4가 살아남았다). 평균이
     * 크게 다를 때로 못 박는다 — 20,000 대비 10,000은 5000bp, 10,000 대비면
     * 10000bp다.
     */
    assert(eq_avg_diff_bp(2000000, 100, 1000000, 100) == 5000);
    assert(eq_avg_diff_bp(1000000, 100, 2000000, 100) == -10000);

    /*
     * 원 단위 평균은 같은데 실제 평균이 다른 경우 — 옛 방식에서는 **항상 0**이었다.
     * 10,014.99 vs 10,014.00: 0.99원 = 0.989bp -> 1bp
     */
    assert(eq_avg_price(1001499, 100) == eq_avg_price(1001400, 100));
    assert(eq_avg_diff_bp(1001499, 100, 1001400, 100) == 1);
}

/* 체결이 없는 쪽이 있으면 비교할 가격이 없다 */
static void test_avg_diff_no_fill(void)
{
    assert(eq_avg_diff_bp(0, 0, 1001400, 100) == 0);
    assert(eq_avg_diff_bp(1001400, 100, 0, 0) == 0);
    assert(eq_avg_diff_bp(1001400, 0, 1001400, 100) == 0);
}

/* 체결률의 반올림도 같은 규칙을 쓴다. */
static void test_fill_rate_rounding(void)
{
    eq_metrics_t m;

    /* 1 / 3 = 3333.33bp -> 3333 */
    assert(eq_measure(SIDE_BUY, 3, 1, 10000, 10000, &m) == ERR_OK);
    assert(m.fill_rate_bp == 3333);

    /* 2 / 3 = 6666.67bp -> 6667 */
    assert(eq_measure(SIDE_BUY, 3, 2, 20000, 10000, &m) == ERR_OK);
    assert(m.fill_rate_bp == 6667);

    /* 1 / 8 = 1250bp */
    assert(eq_measure(SIDE_BUY, 8, 1, 10000, 10000, &m) == ERR_OK);
    assert(m.fill_rate_bp == 1250);
}

/* 체결이 없으면 잴 것이 없다. 0은 "좋음"이 아니라 "없음"이다. */
static void test_no_fill(void)
{
    eq_metrics_t m;
    assert(eq_measure(SIDE_BUY, 100, 0, 0, 10000, &m) == ERR_OK);

    assert(m.avg_price == 0);
    assert(m.slippage == 0);
    assert(m.slippage_bp == 0);
    assert(m.fill_rate_bp == 0);
    assert(m.filled_qty == 0);
}

static void test_measure_rejects(void)
{
    eq_metrics_t m;

    assert(eq_measure(SIDE_BUY, 100, 100, 1000000, 10000, NULL) ==
           ERR_NULL_PTR);
    assert(eq_measure((side_t)7, 100, 100, 1000000, 10000, &m) ==
           ERR_INVALID_ARG);
    assert(eq_measure(SIDE_BUY, 100, 100, 1000000, 0, &m) == ERR_INVALID_PRICE);
    assert(eq_measure(SIDE_BUY, 100, 100, 1000000, -1, &m) == ERR_INVALID_PRICE);
    assert(eq_measure(SIDE_BUY, 0, 0, 0, 10000, &m) == ERR_INVALID_QTY);
    assert(eq_measure(SIDE_BUY, 100, -1, 0, 10000, &m) == ERR_INVALID_QTY);
    /* 주문보다 많이 체결될 수 없다. */
    assert(eq_measure(SIDE_BUY, 100, 101, 1010000, 10000, &m) ==
           ERR_INVALID_QTY);
    /* 수량과 금액이 서로 다른 말을 한다. */
    assert(eq_measure(SIDE_BUY, 100, 0, 5, 10000, &m) == ERR_INVALID_ARG);
    assert(eq_measure(SIDE_BUY, 100, 10, 0, 10000, &m) == ERR_INVALID_ARG);
}

/* --- 3. 기준가 --- */

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
} fixture_t;

static void fx_init(fixture_t *fx, bool with_rules)
{
    const market_rules_t *rules[MARKET_COUNT] = {
        [MARKET_KRX] = &KRX_RULES,
        [MARKET_NXT] = &NXT_RULES,
    };

    cons_init(&fx->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        fx->eng[m] = match_engine_create(BASE, CAP);
        assert(fx->eng[m] != NULL);
        assert(cons_attach(&fx->cons, (market_t)m, match_book(fx->eng[m]),
                           with_rules ? rules[m] : NULL) == ERR_OK);
    }
}

static void fx_free(fixture_t *fx)
{
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(fx->eng[m]);
    }
}

static void put(fixture_t *fx, market_t m, side_t side, price_t price, qty_t qty)
{
    exec_result_t res;
    order_t       req;

    memset(&req, 0, sizeof(req));
    req.id = MAKER_ID++;
    req.ts = T_BOTH;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = ORDER_LIMIT;
    req.market = m;
    assert(match_limit(fx->eng[m], &req, &res) == ERR_OK);
}

static order_t req_of(side_t side, price_t limit, qty_t qty)
{
    order_t r;
    memset(&r, 0, sizeof(r));
    r.id = 999;
    r.ts = T_BOTH;
    r.side = side;
    r.price = limit;
    r.qty = qty;
    r.type = ORDER_LIMIT;
    return r;
}

/*
 * **기준가는 통합 최우선호가다.** 시장별이 아니다.
 *
 * KRX 매도 10,010 / NXT 매도 10,000이면 매수의 기준가는 10,000이다. KRX에만 보내는
 * 전략을 KRX 호가(10,010)로 재면 "슬리피지 0"이 나온다 — 더 싼 시장을 두고 비싼
 * 데서 산 사실이 잣대에서 지워진다. 그러면 전략 비교가 성립하지 않는다.
 */
static void test_benchmark_is_consolidated(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_KRX, SIDE_BUY, 9990, 100);
    put(&fx, MARKET_NXT, SIDE_BUY, 9980, 100);

    price_t bench;
    order_t buy = req_of(SIDE_BUY, 10020, 100);
    assert(eq_benchmark(&fx.cons, &buy, T_BOTH, &bench) == ERR_OK);
    assert(bench == 10000); /* 시장별 10,010이 아니라 통합 10,000 */

    order_t sell = req_of(SIDE_SELL, 9900, 100);
    assert(eq_benchmark(&fx.cons, &sell, T_BOTH, &bench) == ERR_OK);
    assert(bench == 9990);

    /*
     * KRX에 전량 보내 10,010에 100주 체결했다면, 통합 기준가 10,000 대비
     * (1,001,000 - 1,000,000) x 10,000 / 1,000,000 = 10bp 불리하다.
     */
    eq_metrics_t m;
    assert(eq_measure(SIDE_BUY, 100, 100, 1001000, 10000, &m) == ERR_OK);
    assert(m.slippage_bp == 10);

    fx_free(&fx);
}

/* 닫힌 시장의 호가는 기준가가 되지 않는다. */
static void test_benchmark_skips_closed_market(void)
{
    fixture_t fx;
    fx_init(&fx, true);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10010, 100);

    price_t bench;
    order_t buy = req_of(SIDE_BUY, 10020, 100);

    assert(eq_benchmark(&fx.cons, &buy, T_BOTH, &bench) == ERR_OK);
    assert(bench == 10000); /* 둘 다 열려 있으면 더 싼 쪽 */

    /* 18:00에는 KRX가 닫힌다. 그러면 NXT 호가가 기준이다. */
    assert(eq_benchmark(&fx.cons, &buy, T_NXT_ONLY, &bench) == ERR_OK);
    assert(bench == 10010);

    fx_free(&fx);
}

static void test_benchmark_rejects(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    price_t bench;
    order_t buy = req_of(SIDE_BUY, 10000, 100);

    /* 상대 호가가 하나도 없다. */
    assert(eq_benchmark(&fx.cons, &buy, T_BOTH, &bench) == ERR_NO_LIQUIDITY);

    assert(eq_benchmark(NULL, &buy, T_BOTH, &bench) == ERR_NULL_PTR);
    assert(eq_benchmark(&fx.cons, NULL, T_BOTH, &bench) == ERR_NULL_PTR);
    assert(eq_benchmark(&fx.cons, &buy, T_BOTH, NULL) == ERR_NULL_PTR);

    order_t bad = req_of((side_t)9, 10000, 100);
    assert(eq_benchmark(&fx.cons, &bad, T_BOTH, &bench) == ERR_INVALID_ARG);

    fx_free(&fx);
}

int main(void)
{
    test_bp_rounding();
    test_avg_price();
    test_buy_hand_computed();
    test_sell_hand_computed();
    test_favorable_is_negative();
    test_partial_fill();
    test_bp_not_derived_from_avg_price();
    test_avg_diff_not_derived_from_rounded_avg();
    test_avg_diff_no_fill();
    test_fill_rate_rounding();
    test_no_fill();
    test_measure_rejects();
    test_benchmark_is_consolidated();
    test_benchmark_skips_closed_market();
    test_benchmark_rejects();
    return 0;
}
