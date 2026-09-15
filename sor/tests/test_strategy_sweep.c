/*
 * T2-08 집행 전략 SWEEP.
 *
 * 완료 조건의 핵심은 "양 시장을 합쳐 가격 순으로 훑은 것과 결과가 같다"이다.
 * 그건 눈으로 맞춰 볼 성질이 아니라 **불변조건**이므로, 통합 호가창을 직접 훑어
 * 기대 배분을 따로 계산하고 계획과 대조한다. 그 계산이 곧 규칙의 정의다.
 *
 * 평균 체결 단가도 함께 본다 — 쓸어 담은 결과가 실제로 더 싼가가 이 전략의 존재
 * 이유이기 때문이다.
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
    return STRATEGY_SWEEP.plan(&STRATEGY_SWEEP, &fx->ctx, req, out);
}

static qty_t leg_qty(const exec_plan_t *plan, market_t m)
{
    for (int32_t i = 0; i < plan->leg_count; i++) {
        if (plan->legs[i].market == m) {
            return plan->legs[i].qty;
        }
    }
    return 0;
}

/*
 * 통합 호가창을 직접 훑어 기대 배분과 체결 금액을 계산한다.
 * 이 함수가 SWEEP의 정의이고, 구현은 여기에 맞아야 한다.
 */
typedef struct {
    qty_t   alloc[MARKET_COUNT];
    qty_t   swept;    /* 실제로 쓸어 담은 수량 */
    int64_t notional; /* 쓸어 담은 금액 */
} expected_t;

static expected_t sweep_by_hand(fixture_t *fx, const order_t *req)
{
    side_t maker_side = (req->side == SIDE_BUY) ? SIDE_SELL : SIDE_BUY;
    cons_level_view_t levels[64];
    int n = cons_snapshot(&fx->cons, maker_side, 64, fx->ctx.ts, levels);
    assert(n >= 0);

    expected_t e = {0};
    qty_t remaining = req->qty;

    for (int i = 0; i < n && remaining > 0; i++) {
        bool within = (req->side == SIDE_BUY) ? (levels[i].price <= req->price)
                                              : (levels[i].price >= req->price);
        if (!within) {
            break;
        }
        qty_t take =
            (remaining < levels[i].total_qty) ? remaining : levels[i].total_qty;
        e.alloc[levels[i].market] += take;
        e.notional += (int64_t)levels[i].price * (int64_t)take;
        e.swept += take;
        remaining -= take;
    }
    return e;
}

/* 한 시장이 더 싸면 거기부터, 다 쓰면 다른 시장으로 넘어간다 */
static void test_sweeps_cheapest_first(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* NXT 10,000에 100주 / KRX 10,010에 100주 / NXT 10,020에 100주 */
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10020, 100);

    exec_plan_t plan;
    order_t req = buy_req(10020, 250);
    assert(run(&fx, &req, &plan) == ERR_OK);

    /* 10,000(NXT 100) -> 10,010(KRX 100) -> 10,020(NXT 50) */
    assert(leg_qty(&plan, MARKET_NXT) == 150);
    assert(leg_qty(&plan, MARKET_KRX) == 100);
    assert(plan.planned_qty == 250);

    expected_t e = sweep_by_hand(&fx, &req);
    assert(leg_qty(&plan, MARKET_KRX) == e.alloc[MARKET_KRX]);
    assert(leg_qty(&plan, MARKET_NXT) == e.alloc[MARKET_NXT]);
    /* 평균 체결 단가: (10000x100 + 10010x100 + 10020x50) / 250 */
    assert(e.notional == 2502000);
    assert(e.notional / e.swept == 10008);

    fx_free(&fx);
}

/* 한 시장만 싸면 전량 그쪽으로 — 쪼갤 이유가 없다 */
static void test_single_market_when_cheapest_is_deep(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 1000);
    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 1000);

    exec_plan_t plan;
    order_t req = buy_req(10010, 400);
    assert(run(&fx, &req, &plan) == ERR_OK);

    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_NXT);
    assert(plan.legs[0].qty == 400);

    /* 같은 상황에서 SPLIT은 비례로 나눈다 — 두 전략이 실제로 갈린다 */
    exec_plan_t split;
    assert(STRATEGY_SPLIT.plan(&STRATEGY_SPLIT, &fx.ctx, &req, &split) == ERR_OK);
    assert(split.leg_count == 2);

    fx_free(&fx);
}

/* 같은 가격이면 양 시장을 모두 가져간다 */
static void test_same_price_both_markets(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 300);

    exec_plan_t plan;
    order_t req = buy_req(10000, 400);
    assert(run(&fx, &req, &plan) == ERR_OK);

    assert(leg_qty(&plan, MARKET_KRX) == 100);
    assert(leg_qty(&plan, MARKET_NXT) == 300);

    fx_free(&fx);
}

/* 지정가를 넘는 단에서 멈춘다 */
static void test_stops_at_limit(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 10000); /* 지정가 밖 */

    exec_plan_t plan;
    order_t req = buy_req(10010, 500);
    assert(run(&fx, &req, &plan) == ERR_OK);

    /* 200주만 쓸어 담고, 남은 300주는 등록된다 */
    expected_t e = sweep_by_hand(&fx, &req);
    assert(e.swept == 200);
    assert(plan.planned_qty == 500); /* 합은 항상 원 주문과 같다 */

    assert(leg_qty(&plan, MARKET_NXT) + leg_qty(&plan, MARKET_KRX) == 500);
    assert(leg_qty(&plan, MARKET_NXT) >= e.alloc[MARKET_NXT]);
    assert(leg_qty(&plan, MARKET_KRX) >= e.alloc[MARKET_KRX]);

    fx_free(&fx);
}

/* 매도 주문 — 비싼 매수호가부터 */
static void test_sell_side(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_BUY, 9950, 100);
    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 100);
    put(&fx, MARKET_KRX, SIDE_BUY, 9850, 100);

    exec_plan_t plan;
    order_t req = buy_req(9850, 250);
    req.side = SIDE_SELL;
    assert(run(&fx, &req, &plan) == ERR_OK);

    /* 9,950(KRX 100) -> 9,900(NXT 100) -> 9,850(KRX 50) */
    assert(leg_qty(&plan, MARKET_KRX) == 150);
    assert(leg_qty(&plan, MARKET_NXT) == 100);

    expected_t e = sweep_by_hand(&fx, &req);
    assert(e.notional ==
           (int64_t)9950 * 100 + (int64_t)9900 * 100 + (int64_t)9850 * 50);

    fx_free(&fx);
}

/* 한쪽이 닫혀 있으면 열린 쪽만 훑는다 */
static void test_closed_market(void)
{
    exec_plan_t plan;
    order_t req = buy_req(10100, 300);

    fixture_t after;
    fx_init(&after, true, T_NXT_ONLY);
    put(&after, MARKET_KRX, SIDE_SELL, 9000, 10000); /* 훨씬 싸지만 닫힘 */
    put(&after, MARKET_NXT, SIDE_SELL, 10000, 10000);
    assert(run(&after, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_NXT);
    assert(plan.legs[0].qty == 300);
    fx_free(&after);

    fixture_t noon;
    fx_init(&noon, true, T_KRX_ONLY);
    put(&noon, MARKET_NXT, SIDE_SELL, 9000, 10000);
    put(&noon, MARKET_KRX, SIDE_SELL, 10000, 10000);
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

/* 쓸어 담을 것이 전혀 없으면 등록만 한다 */
static void test_nothing_to_sweep(void)
{
    exec_plan_t plan;
    order_t req = buy_req(10000, 200);

    fixture_t empty;
    fx_init(&empty, false, T_BOTH);
    assert(run(&empty, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].qty == 200);
    assert(plan.legs[0].market == MARKET_KRX);
    fx_free(&empty);

    fixture_t far;
    fx_init(&far, false, T_BOTH);
    put(&far, MARKET_KRX, SIDE_SELL, 10500, 100);
    put(&far, MARKET_NXT, SIDE_SELL, 10300, 100);
    assert(run(&far, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_NXT); /* 호가가 유리한 쪽에 등록 */
    fx_free(&far);
}

/*
 * SWEEP이 만드는 체결 금액이 SPLIT보다 낮거나 같아야 한다.
 *
 * 가격 순으로 담으므로 정의상 가장 싼 조합이다. 이게 아니면 SWEEP을 만들 이유가 없다.
 * 계획만으로는 체결 결과를 알 수 없으므로, 호가창을 새로 만들어 같은 유동성으로
 * 두 계획을 각각 흘려 넣고 잰다 — T2-14가 쓸 방법과 같다.
 */
static void test_sweep_is_not_worse_than_split(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* KRX는 싸지만 얇고, NXT는 비싸지만 두껍다 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100);
    put(&fx, MARKET_KRX, SIDE_SELL, 10050, 100);
    put(&fx, MARKET_NXT, SIDE_SELL, 10020, 1000);

    order_t req = buy_req(10050, 400);

    exec_plan_t sweep, split;
    assert(run(&fx, &req, &sweep) == ERR_OK);
    assert(STRATEGY_SPLIT.plan(&STRATEGY_SPLIT, &fx.ctx, &req, &split) == ERR_OK);
    assert(memcmp(&sweep, &split, sizeof(sweep)) != 0);

    int64_t cost[2] = {0};
    qty_t   filled[2] = {0};
    const exec_plan_t *plans[2] = {&sweep, &split};

    for (int p = 0; p < 2; p++) {
        fixture_t sim;
        fx_init(&sim, false, T_BOTH);
        put(&sim, MARKET_KRX, SIDE_SELL, 10000, 100);
        put(&sim, MARKET_KRX, SIDE_SELL, 10050, 100);
        put(&sim, MARKET_NXT, SIDE_SELL, 10020, 1000);

        for (int32_t i = 0; i < plans[p]->leg_count; i++) {
            const plan_leg_t *leg = &plans[p]->legs[i];
            exec_result_t res;
            order_t child = {0};
            child.id = NEXT_ID++;
            child.ts = T_BOTH;
            child.side = req.side;
            child.price = leg->limit_price;
            child.qty = leg->qty;
            child.type = leg->type;
            child.market = leg->market;
            assert(match_limit(sim.eng[leg->market], &child, &res) == ERR_OK);
            cost[p] += res.notional;
            filled[p] += res.filled_qty;
        }
        fx_free(&sim);
    }

    assert(filled[0] >= filled[1]); /* 더 많이 채우거나 같다 */
    if (filled[0] == filled[1]) {
        assert(cost[0] <= cost[1]);
    }

    fx_free(&fx);
}

/*
 * 불변조건 — 여러 호가 배치에서 "손으로 훑은 결과"와 계획이 일치한다.
 * 잔량이 남아 등록되는 경우까지 포함해 합계도 본다.
 */
static void test_matches_hand_sweep(void)
{
    const struct {
        price_t  p;
        qty_t    q;
        market_t m;
    } BOOKS[][6] = {
        {{10000, 50, MARKET_KRX},
         {10010, 50, MARKET_NXT},
         {10020, 50, MARKET_KRX},
         {10030, 50, MARKET_NXT},
         {10040, 50, MARKET_KRX},
         {10050, 50, MARKET_NXT}},
        {{10000, 300, MARKET_NXT},
         {10000, 200, MARKET_KRX},
         {10010, 100, MARKET_KRX},
         {10020, 400, MARKET_NXT},
         {10030, 10, MARKET_KRX},
         {10040, 10, MARKET_NXT}},
        {{10000, 1, MARKET_KRX},
         {10010, 1, MARKET_NXT},
         {10020, 1, MARKET_KRX},
         {10030, 1, MARKET_NXT},
         {10040, 1, MARKET_KRX},
         {10050, 1, MARKET_NXT}},
    };
    const size_t book_count = sizeof(BOOKS) / sizeof(BOOKS[0]);

    for (size_t b = 0; b < book_count; b++) {
        for (qty_t q = 1; q <= 900; q += 7) {
            fixture_t fx;
            fx_init(&fx, false, T_BOTH);
            for (int i = 0; i < 6; i++) {
                put(&fx, BOOKS[b][i].m, SIDE_SELL, BOOKS[b][i].p, BOOKS[b][i].q);
            }

            order_t req = buy_req(10050, q);
            exec_plan_t plan;
            assert(run(&fx, &req, &plan) == ERR_OK);
            assert(plan_validate(&plan, &req) == ERR_OK);
            assert(plan.planned_qty == q);

            expected_t e = sweep_by_hand(&fx, &req);
            qty_t leftover = q - e.swept;

            /* 쓸어 담은 몫은 반드시 들어 있고, 잔량은 한 시장에 더해진다 */
            qty_t extra = 0;
            for (int32_t m = 0; m < MARKET_COUNT; m++) {
                qty_t got = leg_qty(&plan, (market_t)m);
                assert(got >= e.alloc[m]);
                extra += got - e.alloc[m];
            }
            assert(extra == leftover);

            fx_free(&fx);
        }
    }
}

/* 인자 검증과 결정성 */
static void test_args_and_determinism(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 137);
    put(&fx, MARKET_NXT, SIDE_SELL, 10010, 263);

    exec_plan_t plan;
    order_t req = buy_req(10010, 300);

    assert(STRATEGY_SWEEP.plan(&STRATEGY_SWEEP, NULL, &req, &plan) ==
           ERR_NULL_PTR);
    assert(STRATEGY_SWEEP.plan(&STRATEGY_SWEEP, &fx.ctx, NULL, &plan) ==
           ERR_NULL_PTR);
    assert(STRATEGY_SWEEP.plan(&STRATEGY_SWEEP, &fx.ctx, &req, NULL) ==
           ERR_NULL_PTR);

    order_t bad = req;
    bad.qty = 0;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_QTY);
    bad.qty = QTY_MAX + 1;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_QTY);
    bad = req;
    bad.side = (side_t)9;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_ARG);

    assert(strcmp(strategy_name(&STRATEGY_SWEEP), "SWEEP") == 0);

    exec_plan_t a, b;
    assert(run(&fx, &req, &a) == ERR_OK);
    assert(run(&fx, &req, &b) == ERR_OK);
    assert(memcmp(&a, &b, sizeof(a)) == 0);

    fx_free(&fx);
}

int main(void)
{
    test_sweeps_cheapest_first();
    test_single_market_when_cheapest_is_deep();
    test_same_price_both_markets();
    test_stops_at_limit();
    test_sell_side();
    test_closed_market();
    test_nothing_to_sweep();
    test_sweep_is_not_worse_than_split();
    test_matches_hand_sweep();
    test_args_and_determinism();
    return 0;
}
