/*
 * T2-11 분할 주문 취소.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 논리 주문 취소는 **살아 있는 모든** 물리 주문을 취소한다
 *  2. 한쪽 취소가 실패하면 — **성공한 취소를 되돌리지 않는다.** 대신 실패한 다리가
 *     어느 것인지 남기고, 호출자가 재시도할 수 있게 한다 (근거는 executor.h)
 *  3. 부분 취소 결과가 논리 주문 상태에 정확히 반영된다
 *
 * 완료 조건이 지정한 세 경우: 양쪽 성공 / 한쪽 이미 체결 / 한쪽 시장 마감.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"
#include "executor.h"
#include "match.h"
#include "strategy.h"

#define BASE 10000
#define CAP 512
#define T_BOTH TOD_NS(12, 0, 0)
#define T_NXT_ONLY TOD_NS(18, 0, 0)
/* 다음 날 13:00. 시각을 거꾸로 돌리지 않고 "KRX가 다시 열린 뒤"를 만든다. */
#define T_NEXT_DAY (NS_PER_DAY + TOD_NS(13, 0, 0))

static order_id_t MAKER_ID = 1000000;
static order_id_t NEXT_LOGICAL = 1;

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
    venues_t        venues;
    order_map_t    *map;
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
        if (with_rules) {
            match_set_rules(fx->eng[m], rules[m]);
        }
        assert(cons_attach(&fx->cons, (market_t)m, match_book(fx->eng[m]),
                           with_rules ? rules[m] : NULL) == ERR_OK);
        fx->venues.eng[m] = fx->eng[m];
    }
    fx->map = omap_create(CAP);
    assert(fx->map != NULL);
}

static void fx_free(fixture_t *fx)
{
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(fx->eng[m]);
    }
    omap_destroy(fx->map);
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

static order_t logical_req(qty_t qty, ts_t ts)
{
    order_t r;
    memset(&r, 0, sizeof(r));
    r.id = NEXT_LOGICAL++;
    r.ts = ts;
    r.side = SIDE_BUY;
    r.price = 10000;
    r.qty = qty;
    r.type = ORDER_LIMIT;
    return r;
}

static exec_plan_t manual_plan(const order_t *req, qty_t krx, qty_t nxt)
{
    exec_plan_t plan;
    plan_init(&plan);
    assert(plan_add_leg(&plan, MARKET_KRX, krx, req->price, ORDER_LIMIT) ==
           ERR_OK);
    assert(plan_add_leg(&plan, MARKET_NXT, nxt, req->price, ORDER_LIMIT) ==
           ERR_OK);
    return plan;
}

static const cancel_leg_t *cleg_of(const cancel_report_t *rep, market_t m)
{
    for (int32_t i = 0; i < rep->leg_count; i++) {
        if (rep->legs[i].market == m) {
            return &rep->legs[i];
        }
    }
    return NULL;
}

/* 취소 뒤에도 성립해야 하는 것. 수량이 어디로도 새지 않는다. */
static void check_invariant(const fixture_t *fx, order_id_t id, qty_t order_qty)
{
    qty_t filled = omap_filled_qty(fx->map, id);
    qty_t canceled = omap_canceled_qty(fx->map, id);
    qty_t working = omap_remaining(fx->map, id);

    assert(filled + canceled + working == order_qty);
    assert(working >= 0);

    /* 살아 있다고 기록된 다리의 잔량 합이 곧 working이다. */
    const logical_order_t *lo = omap_get(fx->map, id);
    qty_t                  live_sum = 0;
    for (int32_t i = 0; i < lo->leg_count; i++) {
        const phys_leg_t *leg = &lo->legs[i];
        if (leg->live) {
            live_sum += leg->sent_qty - leg->filled_qty - leg->canceled_qty;
        } else {
            assert(leg->filled_qty + leg->canceled_qty == leg->sent_qty);
        }
    }
    assert(live_sum == working);
}

/* --- 1. 양쪽 성공 --- */

static void test_both_legs_canceled(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    /* 상대 호가가 없다. 두 다리 다 등록만 되고 살아 있다. */
    order_t       req = logical_req(100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 40, 60);
    exec_report_t rep;
    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.working_qty == 100);

    cancel_report_t crep;
    assert(exec_cancel(fx.map, &fx.venues, req.id, T_BOTH, &crep) == ERR_OK);

    assert(crep.attempted == 2);
    assert(crep.failed == 0);
    assert(cleg_of(&crep, MARKET_KRX)->canceled_qty == 40);
    assert(cleg_of(&crep, MARKET_NXT)->canceled_qty == 60);
    assert(crep.canceled_qty == 100);
    assert(crep.working_qty == 0);
    /* 체결이 하나도 없이 취소됐다 — 거부가 아니라 취소다. */
    assert(crep.status == STATUS_CANCELED);

    check_invariant(&fx, req.id, 100);

    /* 두 번째 취소는 취소할 것이 없다. */
    cancel_report_t again;
    assert(exec_cancel(fx.map, &fx.venues, req.id, T_BOTH, &again) ==
           ERR_NOT_FOUND);
    assert(again.attempted == 0);
    assert(again.canceled_qty == 0);

    fx_free(&fx);
}

/* --- 2. 한쪽 이미 체결 --- */

static void test_one_leg_already_filled(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    /* KRX 다리(40주)는 전량 체결되고, NXT 다리(60주)만 남는다. */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 40);

    order_t       req = logical_req(100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 40, 60);
    exec_report_t rep;
    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.filled_qty == 40);

    cancel_report_t crep;
    assert(exec_cancel(fx.map, &fx.venues, req.id, T_BOTH, &crep) == ERR_OK);

    /* 이미 끝난 다리는 시도조차 하지 않는다. 실패가 아니다. */
    assert(!cleg_of(&crep, MARKET_KRX)->was_live);
    assert(cleg_of(&crep, MARKET_KRX)->canceled_qty == 0);
    assert(crep.attempted == 1);
    assert(crep.failed == 0);

    assert(cleg_of(&crep, MARKET_NXT)->canceled_qty == 60);
    assert(crep.canceled_qty == 60);
    assert(crep.working_qty == 0);

    /* 체결된 40주가 상태에서 사라지면 안 된다. */
    assert(omap_filled_qty(fx.map, req.id) == 40);
    assert(crep.status == STATUS_PARTIAL);

    check_invariant(&fx, req.id, 100);
    fx_free(&fx);
}

/* 전량 체결된 논리 주문은 취소할 것이 없다. */
static void test_fully_filled_has_nothing_to_cancel(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 40);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 60);

    order_t       req = logical_req(100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 40, 60);
    exec_report_t rep;
    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.status == STATUS_FILLED);

    cancel_report_t crep;
    assert(exec_cancel(fx.map, &fx.venues, req.id, T_BOTH, &crep) ==
           ERR_NOT_FOUND);
    assert(crep.attempted == 0);
    /* 취소를 못 했다고 해서 전량 체결이 흔들리지 않는다. */
    assert(crep.status == STATUS_FILLED);
    assert(omap_filled_qty(fx.map, req.id) == 100);

    check_invariant(&fx, req.id, 100);
    fx_free(&fx);
}

/* --- 3. 한쪽 시장 마감 --- */

/*
 * 완료 조건 2의 핵심. 18:00은 KRX가 닫혀 있어 취소도 받지 않는다.
 *
 * **NXT 쪽 취소는 그대로 둔다.** 되돌리는 것은 주문을 다시 내는 것이고, 큐의 원래
 * 자리는 이미 비었다. 되돌리는 사이에 체결되면 사용자는 취소됐다고 믿는데 체결이
 * 나는 최악의 결과가 된다. 실패한 다리는 살아 있으므로 **재시도**가 답이다.
 */
static void test_one_market_closed(void)
{
    fixture_t fx;
    fx_init(&fx, true);

    order_t       req = logical_req(100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 40, 60);
    exec_report_t rep;
    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.working_qty == 100);

    cancel_report_t crep;
    assert(exec_cancel(fx.map, &fx.venues, req.id, T_NXT_ONLY, &crep) ==
           ERR_MARKET_CLOSED);

    const cancel_leg_t *krx = cleg_of(&crep, MARKET_KRX);
    const cancel_leg_t *nxt = cleg_of(&crep, MARKET_NXT);

    assert(crep.attempted == 2);
    assert(crep.failed == 1);
    assert(krx->rc == ERR_MARKET_CLOSED);
    assert(krx->canceled_qty == 0);

    /* 실패와 무관하게 NXT 60주는 취소된 채로 남는다 — 되돌리지 않는다. */
    assert(nxt->rc == ERR_OK);
    assert(nxt->canceled_qty == 60);
    assert(crep.canceled_qty == 60);

    /* KRX 40주는 아직 살아 있다. 부분 취소가 상태에 그대로 반영된다. */
    assert(crep.working_qty == 40);
    assert(crep.status == STATUS_NEW);
    assert(omap_canceled_qty(fx.map, req.id) == 60);

    check_invariant(&fx, req.id, 100);

    /* 재시도 — KRX가 다시 열리면 남은 다리만 취소된다. */
    cancel_report_t retry;
    assert(exec_cancel(fx.map, &fx.venues, req.id, T_NEXT_DAY, &retry) ==
           ERR_OK);
    assert(retry.attempted == 1);
    assert(retry.failed == 0);
    assert(cleg_of(&retry, MARKET_KRX)->canceled_qty == 40);
    /* 이미 취소된 NXT 다리를 두 번 세지 않는다. */
    assert(retry.canceled_qty == 40);
    assert(retry.working_qty == 0);
    assert(retry.status == STATUS_CANCELED);

    check_invariant(&fx, req.id, 100);
    fx_free(&fx);
}

/* 부분 체결 뒤 남은 잔량만 취소된다. */
static void test_partial_fill_then_cancel(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 10);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 25);

    order_t       req = logical_req(100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 50, 50);
    exec_report_t rep;
    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.filled_qty == 35);
    assert(rep.working_qty == 65);

    cancel_report_t crep;
    assert(exec_cancel(fx.map, &fx.venues, req.id, T_BOTH, &crep) == ERR_OK);

    assert(cleg_of(&crep, MARKET_KRX)->canceled_qty == 40); /* 50 - 10 */
    assert(cleg_of(&crep, MARKET_NXT)->canceled_qty == 25); /* 50 - 25 */
    assert(crep.canceled_qty == 65);
    assert(crep.working_qty == 0);
    /* 체결이 있으므로 취소가 아니라 부분 체결이다. */
    assert(crep.status == STATUS_PARTIAL);
    assert(omap_filled_qty(fx.map, req.id) == 35);

    check_invariant(&fx, req.id, 100);
    fx_free(&fx);
}

/* --- 4. 인자 검사 --- */

static void test_args(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    cancel_report_t crep;
    assert(exec_cancel(NULL, &fx.venues, 1, T_BOTH, &crep) == ERR_NULL_PTR);
    assert(exec_cancel(fx.map, NULL, 1, T_BOTH, &crep) == ERR_NULL_PTR);
    assert(exec_cancel(fx.map, &fx.venues, 1, T_BOTH, NULL) == ERR_NULL_PTR);

    /* 없는 논리 주문. */
    assert(exec_cancel(fx.map, &fx.venues, 9999, T_BOTH, &crep) ==
           ERR_NOT_FOUND);
    assert(exec_cancel(fx.map, &fx.venues, ORDER_ID_INVALID, T_BOTH, &crep) ==
           ERR_NOT_FOUND);

    fx_free(&fx);
}

int main(void)
{
    test_both_legs_canceled();
    test_one_leg_already_filled();
    test_fully_filled_has_nothing_to_cancel();
    test_one_market_closed();
    test_partial_fill_then_cancel();
    test_args();
    return 0;
}
