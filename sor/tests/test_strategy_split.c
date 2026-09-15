/*
 * T2-07 집행 전략 SPLIT.
 *
 * 확인할 것 셋.
 *  1. 비례 배분이 손계산과 정확히 맞는가
 *  2. **단수가 어디로 가는가** — 최대 나머지 방식이 실제로 그렇게 도는가
 *  3. **합계 불변조건** — 어떤 입력에도 다리 수량의 합이 원 주문과 같은가.
 *     한 주라도 새면 논리 주문의 잔량 계산이 전부 어긋난다. 그래서 여기서는
 *     경우를 손으로 고르지 않고 **잔량 비율 12가지 x 수량 1~300주를 전부 훑어서**
 *     확인한다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"
#include "match.h"
#include "strategy.h"

#define BASE 10000
#define CAP 512
#define T_BOTH TOD_NS(12, 0, 0)
#define T_NXT_ONLY TOD_NS(18, 0, 0)
#define T_KRX_ONLY TOD_NS(15, 25, 0)
#define T_CLOSED TOD_NS(22, 0, 0)

static order_id_t NEXT_ID = 1;

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
    exec_context_t  ctx;
} fixture_t;

static void fx_init(fixture_t *fx, bool with_rules, ts_t ts)
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
    fx->ctx.cons = &fx->cons;
    fx->ctx.weights = NULL;
    fx->ctx.config = NULL;
    fx->ctx.ts = ts;
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
    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = T_BOTH;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = ORDER_LIMIT;
    req.market = m;
    assert(match_limit(fx->eng[m], &req, &res) == ERR_OK);
}

static order_t buy_req(price_t limit, qty_t qty)
{
    order_t r = {0};
    r.id = NEXT_ID++;
    r.ts = T_BOTH;
    r.side = SIDE_BUY;
    r.price = limit;
    r.qty = qty;
    r.type = ORDER_LIMIT;
    return r;
}

static int run(fixture_t *fx, const order_t *req, exec_plan_t *out)
{
    return STRATEGY_SPLIT.plan(&STRATEGY_SPLIT, &fx->ctx, req, out);
}

/* 계획에서 그 시장의 몫을 꺼낸다. 다리가 없으면 0. */
static qty_t leg_qty(const exec_plan_t *plan, market_t m)
{
    for (int32_t i = 0; i < plan->leg_count; i++) {
        if (plan->legs[i].market == m) {
            return plan->legs[i].qty;
        }
    }
    return 0;
}

/* 딱 떨어지는 비례 배분 */
static void test_exact_proportion(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* 잔량 300 : 700 -> 1000주를 300 : 700으로 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 300);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 700);

    exec_plan_t plan;
    order_t req = buy_req(10000, 1000);
    assert(run(&fx, &req, &plan) == ERR_OK);

    assert(plan.leg_count == 2);
    assert(leg_qty(&plan, MARKET_KRX) == 300);
    assert(leg_qty(&plan, MARKET_NXT) == 700);
    assert(plan.planned_qty == 1000);
    assert(plan_validate(&plan, &req) == ERR_OK);

    /* 같은 상황에서 BEST_PRICE는 한 다리다 — 두 전략이 실제로 갈린다 */
    exec_plan_t single;
    assert(STRATEGY_BEST_PRICE.plan(&STRATEGY_BEST_PRICE, &fx.ctx, &req,
                                    &single) == ERR_OK);
    assert(single.leg_count == 1);

    fx_free(&fx);
}

/* 1:1이면 반씩 */
static void test_even_split(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 500);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 500);

    exec_plan_t plan;
    order_t req = buy_req(10000, 400);
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert(leg_qty(&plan, MARKET_KRX) == 200);
    assert(leg_qty(&plan, MARKET_NXT) == 200);

    fx_free(&fx);
}

/*
 * 단수 처리 — 최대 나머지 방식.
 *
 * 잔량 1 : 2, 주문 10주.
 *   KRX: 10 x 1 / 3 = 3 나머지 1
 *   NXT: 10 x 2 / 3 = 6 나머지 2
 * 내림 합이 9이므로 1주가 남고, 나머지가 큰 NXT가 가져간다 -> 3 : 7.
 */
static void test_remainder_goes_to_larger_remainder(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 2);

    exec_plan_t plan;
    order_t req = buy_req(10000, 10);
    assert(run(&fx, &req, &plan) == ERR_OK);

    assert(leg_qty(&plan, MARKET_KRX) == 3);
    assert(leg_qty(&plan, MARKET_NXT) == 7);
    assert(plan.planned_qty == 10);

    fx_free(&fx);
}

/* 뒤집어도 규칙이 같다 — 잔량 2:1이면 KRX가 단수를 가져간다 */
static void test_remainder_symmetric(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 2);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 1);

    exec_plan_t plan;
    order_t req = buy_req(10000, 10);
    assert(run(&fx, &req, &plan) == ERR_OK);

    assert(leg_qty(&plan, MARKET_KRX) == 7);
    assert(leg_qty(&plan, MARKET_NXT) == 3);

    fx_free(&fx);
}

/* 나머지가 같으면 잔량이 많은 쪽, 잔량도 같으면 시장 열거 순서 */
static void test_remainder_tie(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* 잔량이 완전히 같으면 나머지도 같다. 홀수 주문의 단수는 KRX로 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 100);

    exec_plan_t plan;
    order_t req = buy_req(10000, 7);
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert(leg_qty(&plan, MARKET_KRX) == 4); /* 3 + 단수 1 */
    assert(leg_qty(&plan, MARKET_NXT) == 3);
    assert(plan.planned_qty == 7);

    fx_free(&fx);
}

/*
 * 나머지가 큰 쪽과 잔량이 큰 쪽이 **갈리는** 경우.
 *
 * 앞의 케이스들은 둘이 우연히 일치해서, 단수 규칙을 "잔량이 많은 쪽"으로 바꿔도
 * 같은 답이 나온다. 그러면 최대 나머지를 쓴다는 것이 검증되지 않는다.
 *
 * 잔량 1 : 3, 주문 3주.
 *   KRX: 3 x 1 / 4 = 0 나머지 3
 *   NXT: 3 x 3 / 4 = 2 나머지 1
 * 내림 합이 2라 1주가 남는다. 나머지가 큰 KRX가 가져가 1 : 2.
 * 잔량 우선이었다면 NXT가 가져가 0 : 3이 됐을 것이다.
 */
static void test_remainder_beats_fillable(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 3);

    exec_plan_t plan;
    order_t req = buy_req(10000, 3);
    assert(run(&fx, &req, &plan) == ERR_OK);

    assert(leg_qty(&plan, MARKET_KRX) == 1); /* 잔량은 적지만 나머지가 크다 */
    assert(leg_qty(&plan, MARKET_NXT) == 2);
    assert(plan.planned_qty == 3);

    fx_free(&fx);
}

/*
 * 회귀 — 양쪽 다 주문보다 물량이 많을 때도 비율이 살아 있어야 한다.
 *
 * book_qty_up_to()는 요구 수량에 도달하면 세기를 멈춘다. 비례 기준을 그 값으로
 * 잡으면 두 잔량이 모두 주문 수량으로 잘려 **비율이 1:1로 뭉개진다.**
 * 실제로 처음에 그렇게 짰다가 여기서 걸렸다.
 */
static void test_ratio_survives_large_liquidity(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* 양쪽 다 100주 주문보다 훨씬 많다. 비율은 1 : 2 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 10000);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 20000);

    exec_plan_t plan;
    order_t req = buy_req(10000, 100);
    assert(run(&fx, &req, &plan) == ERR_OK);

    /* 잔량으로 자르면 50:50이 나온다. 그건 "잔량에 비례"가 아니다 */
    assert(leg_qty(&plan, MARKET_KRX) == 33);
    assert(leg_qty(&plan, MARKET_NXT) == 67);
    assert(plan.planned_qty == 100);

    fx_free(&fx);
}

/* 비례 몫이 0으로 떨어지는 시장은 다리를 만들지 않는다 */
static void test_zero_share_no_leg(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* 잔량 1 : 1000. 주문 10주면 KRX 몫은 10x1/1001 = 0 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 1000);

    exec_plan_t plan;
    order_t req = buy_req(10000, 10);
    assert(run(&fx, &req, &plan) == ERR_OK);

    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_NXT);
    assert(plan.legs[0].qty == 10);
    assert(plan_validate(&plan, &req) == ERR_OK);

    fx_free(&fx);
}

/* 한쪽에만 물량이 있으면 전량 그쪽으로 */
static void test_one_sided_liquidity(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 500);

    exec_plan_t plan;
    order_t req = buy_req(10000, 300);
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_NXT);
    assert(plan.legs[0].qty == 300);

    fx_free(&fx);
}

/* 지정가 밖의 물량은 비례 기준에 넣지 않는다 */
static void test_ignores_liquidity_beyond_limit(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* KRX는 지정가 안에 100주, NXT는 지정가 밖에만 1만 주 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10500, 10000);

    exec_plan_t plan;
    order_t req = buy_req(10000, 200);
    assert(run(&fx, &req, &plan) == ERR_OK);

    /* NXT의 1만 주는 이 주문에게는 없는 것과 같다 */
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_KRX);
    assert(plan.legs[0].qty == 200);

    fx_free(&fx);
}

/* 주문이 총 잔량보다 크면 비례대로 나누고 나머지는 각 시장에 등록된다 */
static void test_order_larger_than_liquidity(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 300);

    exec_plan_t plan;
    order_t req = buy_req(10000, 4000); /* 잔량 400의 10배 */
    assert(run(&fx, &req, &plan) == ERR_OK);

    assert(leg_qty(&plan, MARKET_KRX) == 1000);
    assert(leg_qty(&plan, MARKET_NXT) == 3000);
    assert(plan.planned_qty == 4000);

    fx_free(&fx);
}

/* 한쪽이 닫혀 있으면 열린 쪽에 전량 */
static void test_closed_market(void)
{
    exec_plan_t plan;
    order_t req = buy_req(10000, 500);

    fixture_t after;
    fx_init(&after, true, T_NXT_ONLY);
    put(&after, MARKET_KRX, SIDE_SELL, 10000, 10000); /* 닫혀 있다 */
    put(&after, MARKET_NXT, SIDE_SELL, 10000, 100);
    assert(run(&after, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_NXT);
    assert(plan.legs[0].qty == 500);
    fx_free(&after);

    fixture_t noon;
    fx_init(&noon, true, T_KRX_ONLY);
    put(&noon, MARKET_NXT, SIDE_SELL, 10000, 10000);
    put(&noon, MARKET_KRX, SIDE_SELL, 10000, 100);
    assert(run(&noon, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_KRX);
    fx_free(&noon);

    fixture_t closed;
    fx_init(&closed, true, T_CLOSED);
    assert(run(&closed, &req, &plan) == ERR_MARKET_CLOSED);
    assert(plan.leg_count == 0);
    fx_free(&closed);
}

/* 어디에도 물량이 없으면 등록할 시장을 정해 한 다리로 */
static void test_no_liquidity_anywhere(void)
{
    exec_plan_t plan;
    order_t req = buy_req(10000, 250);

    fixture_t empty;
    fx_init(&empty, false, T_BOTH);
    assert(run(&empty, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].qty == 250);
    assert(plan.legs[0].market == MARKET_KRX);
    fx_free(&empty);

    /* 호가가 지정가 밖에만 있으면 유리한 쪽에 등록 */
    fixture_t far;
    fx_init(&far, false, T_BOTH);
    put(&far, MARKET_KRX, SIDE_SELL, 10500, 100);
    put(&far, MARKET_NXT, SIDE_SELL, 10300, 100);
    assert(run(&far, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_NXT);
    fx_free(&far);
}

/* 매도 주문도 같은 규칙 */
static void test_sell_side(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 200);
    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 600);

    exec_plan_t plan;
    order_t req = buy_req(9900, 800);
    req.side = SIDE_SELL;
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert(leg_qty(&plan, MARKET_KRX) == 200);
    assert(leg_qty(&plan, MARKET_NXT) == 600);

    fx_free(&fx);
}

/*
 * **합계 불변조건을 전수로 확인한다.**
 *
 * 손으로 고른 몇 케이스로는 단수 처리의 구멍을 놓친다. 잔량 비율을 여러 가지로
 * 바꿔 가며 수량 1~300주를 전부 돌려, 어떤 경우에도 합이 원 주문과 같은지 본다.
 */
static void test_sum_invariant_exhaustive(void)
{
    const struct {
        qty_t krx;
        qty_t nxt;
    } MIXES[] = {
        {1, 1},  {1, 2},  {2, 1},   {1, 3},   {3, 7},   {7, 3},
        {1, 97}, {97, 1}, {13, 17}, {50, 50}, {100, 1}, {1, 1000},
    };
    const size_t mix_count = sizeof(MIXES) / sizeof(MIXES[0]);

    for (size_t i = 0; i < mix_count; i++) {
        fixture_t fx;
        fx_init(&fx, false, T_BOTH);
        put(&fx, MARKET_KRX, SIDE_SELL, 10000, MIXES[i].krx);
        put(&fx, MARKET_NXT, SIDE_SELL, 10000, MIXES[i].nxt);

        for (qty_t q = 1; q <= 300; q++) {
            exec_plan_t plan;
            order_t req = buy_req(10000, q);
            assert(run(&fx, &req, &plan) == ERR_OK);

            /* 합이 정확히 원 주문과 같아야 한다 */
            assert(plan.planned_qty == q);
            assert(plan_validate(&plan, &req) == ERR_OK);

            qty_t sum = 0;
            for (int32_t k = 0; k < plan.leg_count; k++) {
                assert(plan.legs[k].qty > 0); /* 0주 다리는 없다 */
                sum += plan.legs[k].qty;
            }
            assert(sum == q);

            /* 비례에서 1주 넘게 벗어나지 않는다 (최대 나머지의 성질) */
            int64_t total = (int64_t)MIXES[i].krx + MIXES[i].nxt;
            int64_t ideal = (int64_t)q * MIXES[i].krx / total;
            qty_t got = leg_qty(&plan, MARKET_KRX);
            assert((int64_t)got >= ideal && (int64_t)got <= ideal + 1);
        }
        fx_free(&fx);
    }
}

/* 인자 검증과 결정성 */
static void test_args_and_determinism(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 137);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 263);

    exec_plan_t plan;
    order_t req = buy_req(10000, 100);

    assert(STRATEGY_SPLIT.plan(&STRATEGY_SPLIT, NULL, &req, &plan) ==
           ERR_NULL_PTR);
    assert(STRATEGY_SPLIT.plan(&STRATEGY_SPLIT, &fx.ctx, NULL, &plan) ==
           ERR_NULL_PTR);
    assert(STRATEGY_SPLIT.plan(&STRATEGY_SPLIT, &fx.ctx, &req, NULL) ==
           ERR_NULL_PTR);

    order_t bad = req;
    bad.qty = 0;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_QTY);
    bad.qty = QTY_MAX + 1;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_QTY);
    bad = req;
    bad.side = (side_t)9;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_ARG);

    assert(strcmp(strategy_name(&STRATEGY_SPLIT), "SPLIT") == 0);

    exec_plan_t a, b;
    assert(run(&fx, &req, &a) == ERR_OK);
    assert(run(&fx, &req, &b) == ERR_OK);
    assert(memcmp(&a, &b, sizeof(a)) == 0);

    fx_free(&fx);
}

int main(void)
{
    test_exact_proportion();
    test_even_split();
    test_remainder_goes_to_larger_remainder();
    test_remainder_symmetric();
    test_remainder_tie();
    test_remainder_beats_fillable();
    test_ratio_survives_large_liquidity();
    test_zero_share_no_leg();
    test_one_sided_liquidity();
    test_ignores_liquidity_beyond_limit();
    test_order_larger_than_liquidity();
    test_closed_market();
    test_no_liquidity_anywhere();
    test_sell_side();
    test_sum_invariant_exhaustive();
    test_args_and_determinism();
    return 0;
}
