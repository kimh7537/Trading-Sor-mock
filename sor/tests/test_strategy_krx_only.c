/*
 * T2-05 집행 전략 KRX_ONLY (기준선).
 *
 * 기준선 전략이므로 "무엇을 하는가"보다 **"무엇을 하지 않는가"**가 중요하다.
 * NXT가 아무리 유리해도 쳐다보지 않아야 하고, NXT만 열려 있는 구간에서는
 * 주문을 거부해야 한다. 그 손해가 곧 다른 전략이 만들어 낼 차이다.
 *
 * 계획 자료구조(plan_*)의 불변조건도 여기서 함께 굳힌다 —
 * 수량 합이 원 주문과 같아야 한다는 것, 한 시장에 두 다리를 못 보낸다는 것.
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
#define T_NXT_ONLY TOD_NS(18, 0, 0)  /* 애프터마켓 — NXT만 */
#define T_KRX_ONLY TOD_NS(15, 25, 0) /* NXT 오후 휴장 — KRX만 */
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

/* 전량이 KRX 한 다리로 나온다 */
static void test_sends_all_to_krx(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1000);

    exec_plan_t plan;
    order_t req = buy_req(10000, 700);
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, &plan) ==
           ERR_OK);

    assert(plan.reason == ERR_OK);
    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_KRX);
    assert(plan.legs[0].qty == 700);
    assert(plan.legs[0].limit_price == 10000);
    assert(plan.legs[0].type == ORDER_LIMIT);
    assert(plan.planned_qty == 700);
    assert(plan_validate(&plan, &req) == ERR_OK);

    assert(strcmp(strategy_name(&STRATEGY_KRX_ONLY), "KRX_ONLY") == 0);

    fx_free(&fx);
}

/* NXT가 훨씬 유리해도 쳐다보지 않는다 — 이것이 기준선의 정의다 */
static void test_ignores_better_nxt(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10100, 10);    /* 비싸고 물량도 적다 */
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 10000); /* 싸고 물량도 많다 */

    exec_plan_t plan;
    order_t req = buy_req(10100, 500);
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, &plan) ==
           ERR_OK);

    assert(plan.leg_count == 1);
    assert(plan.legs[0].market == MARKET_KRX);
    assert(plan.legs[0].qty == 500);

    /* NXT 다리는 하나도 없다 */
    for (int32_t i = 0; i < plan.leg_count; i++) {
        assert(plan.legs[i].market != MARKET_NXT);
    }

    fx_free(&fx);
}

/* KRX에 상대 호가가 전혀 없어도 계획은 세운다 — 등록도 집행이다 */
static void test_plans_without_liquidity(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 1000); /* NXT에만 호가 */

    exec_plan_t plan;
    order_t req = buy_req(9900, 300);
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, &plan) ==
           ERR_OK);
    assert(plan.leg_count == 1 && plan.legs[0].market == MARKET_KRX);
    assert(plan.legs[0].qty == 300);

    fx_free(&fx);
}

/* 매도 주문도 같다 */
static void test_sell_side(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);
    put(&fx, MARKET_NXT, SIDE_BUY, 9950, 1000);
    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 1000);

    exec_plan_t plan;
    order_t req = buy_req(9900, 400);
    req.side = SIDE_SELL;
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, &plan) ==
           ERR_OK);
    assert(plan.leg_count == 1 && plan.legs[0].market == MARKET_KRX);

    fx_free(&fx);
}

/* KRX가 닫혀 있으면 거부한다. NXT가 열려 있어도 마찬가지다 */
static void test_rejects_when_krx_closed(void)
{
    exec_plan_t plan;
    order_t req = buy_req(10000, 100);

    /* 애프터마켓 — NXT만 열려 있다 */
    fixture_t after;
    fx_init(&after, true, T_NXT_ONLY);
    put(&after, MARKET_NXT, SIDE_SELL, 10000, 1000);
    assert(cons_is_open(&after.cons, MARKET_NXT, T_NXT_ONLY));
    assert(!cons_is_open(&after.cons, MARKET_KRX, T_NXT_ONLY));

    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &after.ctx, &req, &plan) ==
           ERR_MARKET_CLOSED);
    assert(plan.reason == ERR_MARKET_CLOSED);
    assert(plan.leg_count == 0);
    assert(plan.planned_qty == 0);
    assert(plan_validate(&plan, &req) == ERR_OK); /* 빈 계획도 유효한 계획이다 */
    fx_free(&after);

    /* 둘 다 닫힘 */
    fixture_t closed;
    fx_init(&closed, true, T_CLOSED);
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &closed.ctx, &req,
                                  &plan) == ERR_MARKET_CLOSED);
    fx_free(&closed);

    /* NXT만 닫힌 구간에서는 정상 */
    fixture_t krx;
    fx_init(&krx, true, T_KRX_ONLY);
    put(&krx, MARKET_KRX, SIDE_SELL, 10000, 1000);
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &krx.ctx, &req, &plan) ==
           ERR_OK);
    assert(plan.leg_count == 1 && plan.legs[0].market == MARKET_KRX);
    fx_free(&krx);
}

/* 인자 검증 */
static void test_rejects_bad_args(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    exec_plan_t plan;
    order_t req = buy_req(10000, 100);

    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, NULL, &req, &plan) ==
           ERR_NULL_PTR);
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, NULL, &plan) ==
           ERR_NULL_PTR);
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, NULL) ==
           ERR_NULL_PTR);

    order_t bad = req;
    bad.qty = 0;
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &bad, &plan) ==
           ERR_INVALID_QTY);
    assert(plan.leg_count == 0);

    bad = req;
    bad.qty = QTY_MAX + 1;
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &bad, &plan) ==
           ERR_INVALID_QTY);

    bad = req;
    bad.side = (side_t)9;
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &bad, &plan) ==
           ERR_INVALID_ARG);

    assert(strategy_name(NULL) != NULL);

    fx_free(&fx);
}

/* 계획 자료구조의 불변조건 */
static void test_plan_invariants(void)
{
    exec_plan_t plan;
    order_t req = buy_req(10000, 100);

    plan_init(&plan);
    plan_init(NULL);
    assert(plan.leg_count == 0 && plan.planned_qty == 0);
    assert(plan.reason == ERR_OK);

    /* 0주짜리 다리는 만들어지지 않는다 */
    assert(plan_add_leg(&plan, MARKET_KRX, 0, 10000, ORDER_LIMIT) == ERR_OK);
    assert(plan_add_leg(&plan, MARKET_KRX, -5, 10000, ORDER_LIMIT) == ERR_OK);
    assert(plan.leg_count == 0);

    /* 합이 원 주문과 같아야 유효하다 */
    assert(plan_add_leg(&plan, MARKET_KRX, 60, 10000, ORDER_LIMIT) == ERR_OK);
    assert(plan_validate(&plan, &req) == ERR_INVALID_QTY); /* 60 != 100 */
    assert(plan_add_leg(&plan, MARKET_NXT, 40, 10000, ORDER_LIMIT) == ERR_OK);
    assert(plan.planned_qty == 100);
    assert(plan_validate(&plan, &req) == ERR_OK);

    /* 같은 시장에 두 다리는 금지 */
    exec_plan_t dup;
    plan_init(&dup);
    assert(plan_add_leg(&dup, MARKET_KRX, 50, 10000, ORDER_LIMIT) == ERR_OK);
    assert(plan_add_leg(&dup, MARKET_KRX, 50, 10010, ORDER_LIMIT) == ERR_OK);
    assert(dup.leg_count == 2);
    assert(plan_validate(&dup, &req) == ERR_DUPLICATE);

    /* 잘못된 시장 */
    exec_plan_t bad;
    plan_init(&bad);
    assert(plan_add_leg(&bad, (market_t)99, 10, 10000, ORDER_LIMIT) ==
           ERR_INVALID_ARG);
    assert(plan_add_leg(NULL, MARKET_KRX, 10, 10000, ORDER_LIMIT) ==
           ERR_NULL_PTR);

    /* 자리가 없으면 거절 */
    exec_plan_t full;
    plan_init(&full);
    for (int i = 0; i < PLAN_LEGS_MAX; i++) {
        assert(plan_add_leg(&full, MARKET_KRX, 1, 10000, ORDER_LIMIT) == ERR_OK);
    }
    assert(plan_add_leg(&full, MARKET_KRX, 1, 10000, ORDER_LIMIT) ==
           ERR_BOOK_FULL);

    assert(plan_validate(NULL, &req) == ERR_NULL_PTR);
    assert(plan_validate(&plan, NULL) == ERR_NULL_PTR);
}

/* 같은 입력에 항상 같은 계획 — 전략 비교의 전제 */
static void test_deterministic(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1000);
    put(&fx, MARKET_NXT, SIDE_SELL, 9990, 1000);

    order_t req = buy_req(10000, 250);

    exec_plan_t a, b;
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, &a) ==
           ERR_OK);
    assert(STRATEGY_KRX_ONLY.plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, &b) ==
           ERR_OK);
    assert(memcmp(&a, &b, sizeof(a)) == 0);

    fx_free(&fx);
}

int main(void)
{
    test_plan_invariants();
    test_sends_all_to_krx();
    test_ignores_better_nxt();
    test_plans_without_liquidity();
    test_sell_side();
    test_rejects_when_krx_closed();
    test_rejects_bad_args();
    test_deterministic();
    return 0;
}
