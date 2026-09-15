/*
 * T2-06 집행 전략 BEST_PRICE.
 *
 * 확인할 것 셋.
 *  1. 평가에서 이긴 시장으로 간다 (그리고 기준선과 실제로 달라진다)
 *  2. **쪼개지 않는다.** 이긴 시장의 잔량이 모자라도 전량 그쪽으로 간다 —
 *     이게 SPLIT·SWEEP과 갈리는 지점이고, 셋을 비교해야 "쪼개는 것이 이득인가"를
 *     말할 수 있다
 *  3. 지금 체결되는 시장이 없어도 주문을 버리지 않는다. 버리면 체결률 차이가
 *     라우팅 품질과 무관한 이유로 생겨 T2-14의 비교가 오염된다
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"
#include "match.h"
#include "strategy.h"

#define BASE 10000
#define CAP 256
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

/* 한 다리, 지정한 시장, 전량인지 확인한다 */
static void assert_single_leg(const exec_plan_t *plan, const order_t *req,
                              market_t want)
{
    assert(plan->reason == ERR_OK);
    assert(plan->leg_count == 1);
    assert(plan->legs[0].market == want);
    assert(plan->legs[0].qty == req->qty);
    assert(plan->legs[0].limit_price == req->price);
    assert(plan->planned_qty == req->qty);
    assert(plan_validate(plan, req) == ERR_OK);
}

static int run(fixture_t *fx, const order_t *req, exec_plan_t *out)
{
    return STRATEGY_BEST_PRICE.plan(&STRATEGY_BEST_PRICE, &fx->ctx, req, out);
}

/* 유리한 시장으로 전량 간다 — 그리고 기준선과 실제로 달라진다 */
static void test_picks_better_market(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10100, 1000);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 1000); /* NXT가 싸다 */

    exec_plan_t plan;
    order_t req = buy_req(10100, 500);
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert_single_leg(&plan, &req, MARKET_NXT);

    /* 같은 상황에서 기준선은 KRX로 간다 — 두 전략이 실제로 갈린다 */
    exec_plan_t baseline;
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req,
                                  &baseline) == ERR_OK);
    assert(baseline.legs[0].market == MARKET_KRX);
    assert(plan.legs[0].market != baseline.legs[0].market);

    fx_free(&fx);
}

/* 반대 방향도 같다 — KRX가 유리하면 KRX로 */
static void test_picks_krx_when_better(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1000);
    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 1000);

    exec_plan_t plan;
    order_t req = buy_req(10100, 500);
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert_single_leg(&plan, &req, MARKET_KRX);

    fx_free(&fx);
}

/* 매도 주문 */
static void test_sell_side(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 1000);
    put(&fx, MARKET_NXT, SIDE_BUY, 9950, 1000); /* 매도에게는 NXT가 유리 */

    exec_plan_t plan;
    order_t req = buy_req(9900, 500);
    req.side = SIDE_SELL;
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert_single_leg(&plan, &req, MARKET_NXT);

    fx_free(&fx);
}

/*
 * **핵심** — 이긴 시장의 잔량이 모자라도 쪼개지 않는다.
 * NXT가 싸지만 50주뿐이고, KRX에 1000주가 있다. 그래도 500주 전량이 NXT로 간다.
 */
static void test_does_not_split(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 50);
    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 1000);

    exec_plan_t plan;
    order_t req = buy_req(10100, 500);

    /* 가격만 보게 해서 NXT가 확실히 이기게 한다 */
    be_weights_t price_only = {.price = 100, .fill = 0, .cost = 0, .state = 0};
    fx.ctx.weights = &price_only;

    assert(run(&fx, &req, &plan) == ERR_OK);
    assert_single_leg(&plan, &req, MARKET_NXT);
    assert(plan.leg_count == 1); /* 쪼개지 않았다 */

    fx_free(&fx);
}

/* 닫힌 시장은 고르지 않는다 */
static void test_skips_closed_market(void)
{
    exec_plan_t plan;
    order_t req = buy_req(10000, 200);

    /* 애프터마켓 — NXT만 열려 있다. KRX가 아무리 좋아도 NXT로 간다 */
    fixture_t after;
    fx_init(&after, true, T_NXT_ONLY);
    put(&after, MARKET_KRX, SIDE_SELL, 9000, 10000); /* 훨씬 싸지만 닫힘 */
    put(&after, MARKET_NXT, SIDE_SELL, 10000, 1000);
    assert(run(&after, &req, &plan) == ERR_OK);
    assert_single_leg(&plan, &req, MARKET_NXT);
    fx_free(&after);

    /* NXT 오후 휴장 — KRX로 */
    fixture_t noon;
    fx_init(&noon, true, T_KRX_ONLY);
    put(&noon, MARKET_NXT, SIDE_SELL, 9000, 10000);
    put(&noon, MARKET_KRX, SIDE_SELL, 10000, 1000);
    assert(run(&noon, &req, &plan) == ERR_OK);
    assert_single_leg(&plan, &req, MARKET_KRX);
    fx_free(&noon);

    /* 둘 다 닫힘 — 거부 */
    fixture_t closed;
    fx_init(&closed, true, T_CLOSED);
    assert(run(&closed, &req, &plan) == ERR_MARKET_CLOSED);
    assert(plan.leg_count == 0);
    assert(plan.reason == ERR_MARKET_CLOSED);
    fx_free(&closed);
}

/*
 * 지금 체결되는 시장이 없어도 주문을 버리지 않는다.
 * 버리면 조용한 장에서 BEST_PRICE만 체결률이 떨어지고, 그 차이가 라우팅 품질과
 * 무관한 이유로 생겨 비교가 오염된다.
 */
static void test_rests_when_nothing_fillable(void)
{
    exec_plan_t plan;
    order_t req = buy_req(10000, 300);

    /* 양쪽 다 호가가 전혀 없다 */
    fixture_t empty;
    fx_init(&empty, false, T_BOTH);
    assert(run(&empty, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].qty == 300);
    assert(plan.legs[0].market == MARKET_KRX); /* 동률이면 열거 순서 */
    fx_free(&empty);

    /* 호가는 있지만 전부 지정가 밖이다 */
    fixture_t far;
    fx_init(&far, false, T_BOTH);
    put(&far, MARKET_KRX, SIDE_SELL, 10500, 1000);
    put(&far, MARKET_NXT, SIDE_SELL, 10300, 1000); /* NXT가 그나마 가깝다 */
    assert(run(&far, &req, &plan) == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_NXT); /* 호가가 유리한 쪽에 등록 */
    fx_free(&far);

    /* 한쪽에만 호가가 있으면 그쪽에 등록한다 — 체결 기회가 가깝다 */
    fixture_t one;
    fx_init(&one, false, T_BOTH);
    put(&one, MARKET_NXT, SIDE_SELL, 10500, 1000);
    assert(run(&one, &req, &plan) == ERR_OK);
    assert(plan.legs[0].market == MARKET_NXT);
    fx_free(&one);
}

/* 등록 시장 선택 규칙을 직접 */
static void test_resting_market_rule(void)
{
    fixture_t fx;
    fx_init(&fx, true, T_BOTH);
    order_t req = buy_req(10000, 100);
    market_t m;

    /* 호가가 전혀 없으면 열거 순서 */
    assert(plan_resting_market(&fx.ctx, &req, &m) == ERR_OK);
    assert(m == MARKET_KRX);

    /* 호가가 있는 시장이 없는 시장을 이긴다 */
    put(&fx, MARKET_NXT, SIDE_SELL, 10500, 100);
    assert(plan_resting_market(&fx.ctx, &req, &m) == ERR_OK);
    assert(m == MARKET_NXT);

    /* 둘 다 있으면 유리한 쪽 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10400, 100);
    assert(plan_resting_market(&fx.ctx, &req, &m) == ERR_OK);
    assert(m == MARKET_KRX);

    /* 닫힌 시장은 후보가 아니다 */
    fx.ctx.ts = T_NXT_ONLY;
    assert(plan_resting_market(&fx.ctx, &req, &m) == ERR_OK);
    assert(m == MARKET_NXT);

    fx.ctx.ts = T_CLOSED;
    assert(plan_resting_market(&fx.ctx, &req, &m) == ERR_MARKET_CLOSED);

    assert(plan_resting_market(NULL, &req, &m) == ERR_NULL_PTR);
    assert(plan_resting_market(&fx.ctx, NULL, &m) == ERR_NULL_PTR);
    assert(plan_resting_market(&fx.ctx, &req, NULL) == ERR_NULL_PTR);

    fx_free(&fx);
}

/* 가중치를 바꾸면 선택이 바뀐다 — 평가를 실제로 거쳐 간다는 확인 */
static void test_weights_change_choice(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* KRX: 가격이 좋고 물량이 적다. NXT: 가격이 나쁘고 물량이 많다 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 50);
    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 1000);

    exec_plan_t plan;
    order_t req = buy_req(10100, 500);
    be_config_t same_fee = {.fee_bp = {[MARKET_KRX] = 2, [MARKET_NXT] = 2}};
    fx.ctx.config = &same_fee;

    be_weights_t price_only = {.price = 100, .fill = 0, .cost = 0, .state = 0};
    fx.ctx.weights = &price_only;
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert(plan.legs[0].market == MARKET_KRX);

    be_weights_t fill_only = {.price = 0, .fill = 100, .cost = 0, .state = 0};
    fx.ctx.weights = &fill_only;
    assert(run(&fx, &req, &plan) == ERR_OK);
    assert(plan.legs[0].market == MARKET_NXT);

    fx_free(&fx);
}

/* 인자 검증 */
static void test_rejects_bad_args(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1000);

    exec_plan_t plan;
    order_t req = buy_req(10000, 100);

    assert(STRATEGY_BEST_PRICE.plan(&STRATEGY_BEST_PRICE, NULL, &req, &plan) ==
           ERR_NULL_PTR);
    assert(STRATEGY_BEST_PRICE.plan(&STRATEGY_BEST_PRICE, &fx.ctx, NULL,
                                    &plan) == ERR_NULL_PTR);
    assert(STRATEGY_BEST_PRICE.plan(&STRATEGY_BEST_PRICE, &fx.ctx, &req,
                                    NULL) == ERR_NULL_PTR);

    order_t bad = req;
    bad.qty = 0;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_QTY);
    bad.qty = QTY_MAX + 1;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_QTY);

    bad = req;
    bad.side = (side_t)9;
    assert(run(&fx, &bad, &plan) == ERR_INVALID_ARG);

    assert(strcmp(strategy_name(&STRATEGY_BEST_PRICE), "BEST_PRICE") == 0);

    fx_free(&fx);
}

/* 같은 입력에 항상 같은 계획 */
static void test_deterministic(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);
    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 700);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 300);

    order_t req = buy_req(10020, 400);
    exec_plan_t a, b;
    assert(run(&fx, &req, &a) == ERR_OK);
    assert(run(&fx, &req, &b) == ERR_OK);
    assert(memcmp(&a, &b, sizeof(a)) == 0);

    fx_free(&fx);
}

int main(void)
{
    test_picks_better_market();
    test_picks_krx_when_better();
    test_sell_side();
    test_does_not_split();
    test_skips_closed_market();
    test_rests_when_nothing_fillable();
    test_resting_market_rule();
    test_weights_change_choice();
    test_rejects_bad_args();
    test_deterministic();
    return 0;
}
