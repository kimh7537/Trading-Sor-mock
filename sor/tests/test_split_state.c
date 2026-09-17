/*
 * T2-10 분할 주문의 부분 집행 상태.
 *
 * 완료 조건 셋을 그대로 옮긴다.
 *  1. 논리 주문 상태: 한쪽만 체결 / 양쪽 부분 체결 / 한쪽 거부
 *  2. 논리 잔량 = 원 수량 - 모든 물리 주문의 체결 합계 (불변조건)
 *  3. 한쪽이 거부돼도 다른 쪽 체결은 유효하다 — 되돌리지 않는다
 *
 * "한쪽 거부"는 한 시장만 열려 있는 시각(18:00, NXT 단독)에 양 시장 계획을 보내
 * 만든다. 거래소가 실제로 거절하는 상황이지 흉내가 아니다.
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
#define T_CLOSED TOD_NS(23, 0, 0)

/*
 * 유동성 주문번호는 물리 주문번호와 겹치면 안 된다. 물리 번호는 논리번호 x 16이라
 * 작은 값을 쓰므로, 유동성은 100만 번대에서 뽑는다.
 */
static order_id_t MAKER_ID = 1000000;
static order_id_t NEXT_LOGICAL = 1;

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
    exec_context_t  ctx;
    venues_t        venues;
    order_map_t    *map;
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
        if (with_rules) {
            match_set_rules(fx->eng[m], rules[m]);
        }
        assert(cons_attach(&fx->cons, (market_t)m, match_book(fx->eng[m]),
                           with_rules ? rules[m] : NULL) == ERR_OK);
        fx->venues.eng[m] = fx->eng[m];
    }
    fx->ctx.cons = &fx->cons;
    fx->ctx.weights = NULL;
    fx->ctx.config = NULL;
    fx->ctx.ts = ts;

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

/* 상대 호가를 깔아 둔다. 양 시장이 다 열린 시각으로 넣는다. */
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

static order_t logical_req(side_t side, price_t limit, qty_t qty, ts_t ts)
{
    order_t r;
    memset(&r, 0, sizeof(r));
    r.id = NEXT_LOGICAL++;
    r.ts = ts;
    r.side = side;
    r.price = limit;
    r.qty = qty;
    r.type = ORDER_LIMIT;
    return r;
}

/* 시장별 수량을 그대로 다리로 만든 계획. 0주는 plan_add_leg가 걸러 낸다. */
static exec_plan_t manual_plan(const order_t *req, qty_t krx, qty_t nxt,
                               order_type_t type)
{
    exec_plan_t plan;
    plan_init(&plan);
    assert(plan_add_leg(&plan, MARKET_KRX, krx, req->price, type) == ERR_OK);
    assert(plan_add_leg(&plan, MARKET_NXT, nxt, req->price, type) == ERR_OK);
    return plan;
}

/* 보고서의 다리를 시장으로 찾는다. 없으면 NULL. */
static const leg_result_t *leg_of(const exec_report_t *rep, market_t m)
{
    for (int32_t i = 0; i < rep->leg_count; i++) {
        if (rep->legs[i].market == m) {
            return &rep->legs[i];
        }
    }
    return NULL;
}

/*
 * 완료 조건 2 — 어떤 결과에서도 성립해야 하는 불변조건.
 * 보고서와 매핑이 서로 다른 말을 하지 않는지도 함께 본다.
 */
static void check_invariant(const fixture_t *fx, const order_t *req,
                            const exec_report_t *rep)
{
    qty_t leg_sum = 0;
    for (int32_t i = 0; i < rep->leg_count; i++) {
        leg_sum += rep->legs[i].filled_qty;
    }

    assert(rep->order_qty == req->qty);
    assert(rep->filled_qty == leg_sum);
    assert(rep->unfilled_qty == req->qty - rep->filled_qty);
    assert(rep->filled_qty <= req->qty);

    /* 매핑이 보고서와 같은 값을 들고 있다. */
    assert(omap_filled_qty(fx->map, req->id) == rep->filled_qty);
    assert(omap_notional(fx->map, req->id) == rep->notional);
    assert(omap_remaining(fx->map, req->id) == rep->working_qty);

    /* 살아 있는 수량은 체결되지 않은 수량을 넘을 수 없다. */
    assert(rep->working_qty <= rep->unfilled_qty);

    order_status_t st;
    assert(exec_status(fx->map, req->id, &st) == ERR_OK);
    assert(st == rep->status);
}

/* --- 1. 양쪽 부분 체결 --- */

static void test_both_partially_filled(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* 양 시장에 30주씩만 있다. 100주를 50:50으로 보내면 양쪽 다 부분 체결이다. */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 30);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 30);

    order_t       req = logical_req(SIDE_BUY, 10000, 100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 50, 50, ORDER_LIMIT);
    exec_report_t rep;

    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);

    assert(rep.rejected_count == 0);
    assert(leg_of(&rep, MARKET_KRX)->filled_qty == 30);
    assert(leg_of(&rep, MARKET_NXT)->filled_qty == 30);
    assert(rep.filled_qty == 60);
    assert(rep.unfilled_qty == 40);
    /* 지정가라 남은 20주씩은 각 시장 호가창에 등록된다 — 아직 살아 있다. */
    assert(leg_of(&rep, MARKET_KRX)->resting);
    assert(leg_of(&rep, MARKET_NXT)->resting);
    assert(rep.working_qty == 40);
    assert(rep.status == STATUS_PARTIAL);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

/* --- 2. 한쪽만 체결 --- */

static void test_only_one_side_fills(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* KRX에만 물량이 있다. NXT 다리는 전량 등록된다. */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100);

    order_t       req = logical_req(SIDE_BUY, 10000, 100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 60, 40, ORDER_LIMIT);
    exec_report_t rep;

    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);

    assert(leg_of(&rep, MARKET_KRX)->filled_qty == 60);
    assert(leg_of(&rep, MARKET_NXT)->filled_qty == 0);
    assert(leg_of(&rep, MARKET_NXT)->resting);
    assert(rep.filled_qty == 60);
    assert(rep.working_qty == 40);
    assert(rep.status == STATUS_PARTIAL);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

/* --- 3. 한쪽 거부 --- */

/*
 * 완료 조건 3 — **한쪽이 거부돼도 다른 쪽 체결은 유효하다.**
 *
 * 18:00은 NXT만 열려 있다. 양 시장 계획을 그대로 보내면 KRX 다리는 거래소가
 * 거절하고 NXT 다리는 체결된다. 그 체결을 되돌리지 않는 것이 핵심이다.
 */
static void test_one_leg_rejected_other_stands(void)
{
    fixture_t fx;
    fx_init(&fx, true, T_NXT_ONLY);

    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 100);

    order_t       req = logical_req(SIDE_BUY, 10000, 100, T_NXT_ONLY);
    exec_plan_t   plan = manual_plan(&req, 40, 60, ORDER_LIMIT);
    exec_report_t rep;

    /* 한 다리라도 접수됐으므로 논리 주문은 거부가 아니다. */
    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);

    const leg_result_t *krx = leg_of(&rep, MARKET_KRX);
    const leg_result_t *nxt = leg_of(&rep, MARKET_NXT);

    assert(rep.rejected_count == 1);
    assert(krx->rc == ERR_MARKET_CLOSED);
    assert(krx->filled_qty == 0);

    /* 거부와 무관하게 NXT 60주는 그대로 체결됐다 — 되돌리지 않는다. */
    assert(nxt->rc == ERR_OK);
    assert(nxt->filled_qty == 60);
    assert(rep.filled_qty == 60);
    assert(rep.status == STATUS_PARTIAL);

    /*
     * 거부된 40주는 영원히 체결되지 않으므로 살아 있는 잔량에서 뺀다.
     * 체결되지 않은 수량(40)과 살아 있는 수량(0)이 다른 값이라는 것이 요점이다.
     */
    assert(rep.unfilled_qty == 40);
    assert(rep.working_qty == 0);
    assert(omap_canceled_qty(fx.map, req.id) == 40);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

/* 모든 다리가 거부되면 그때만 논리 주문이 거부다. */
static void test_all_legs_rejected(void)
{
    fixture_t fx;
    fx_init(&fx, true, T_CLOSED);

    order_t       req = logical_req(SIDE_BUY, 10000, 100, T_CLOSED);
    exec_plan_t   plan = manual_plan(&req, 40, 60, ORDER_LIMIT);
    exec_report_t rep;

    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) ==
           ERR_MARKET_CLOSED);

    assert(rep.rejected_count == 2);
    assert(rep.filled_qty == 0);
    assert(rep.working_qty == 0);
    assert(rep.status == STATUS_REJECTED);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

/* --- 4. 전량 체결과 미체결 --- */

static void test_fully_filled(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 40);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 60);

    order_t       req = logical_req(SIDE_BUY, 10000, 100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 40, 60, ORDER_LIMIT);
    exec_report_t rep;

    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.filled_qty == 100);
    assert(rep.unfilled_qty == 0);
    assert(rep.working_qty == 0);
    assert(rep.status == STATUS_FILLED);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

static void test_nothing_fills_but_rests(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    /* 상대 호가가 없다. 두 다리 모두 등록만 된다. */
    order_t       req = logical_req(SIDE_BUY, 10000, 100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 40, 60, ORDER_LIMIT);
    exec_report_t rep;

    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.filled_qty == 0);
    assert(rep.working_qty == 100);
    assert(rep.status == STATUS_NEW);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

/*
 * IOC 다리는 잔량을 등록하지 않는다. 그 수량은 취소로 기록되어 살아 있는 잔량에서
 * 빠진다 — "등록된 잔량"과 "그냥 안 채워진 수량"을 구분하는 지점이다.
 */
static void test_ioc_leftover_is_canceled(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 10);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 10);

    order_t       req = logical_req(SIDE_BUY, 10000, 100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 50, 50, ORDER_IOC);
    exec_report_t rep;

    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.filled_qty == 20);
    assert(rep.unfilled_qty == 80);
    assert(!leg_of(&rep, MARKET_KRX)->resting);
    assert(rep.working_qty == 0);
    assert(omap_canceled_qty(fx.map, req.id) == 80);
    assert(rep.status == STATUS_PARTIAL);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

/* --- 5. 체결 금액 --- */

/*
 * 여러 가격대를 소진한 체결을 **평균 단가 하나로 뭉뚱그리면 금액이 샌다.**
 *
 * 1주 @10000 + 2주 @10010 = 30,020원. 평균은 10,006.67원이고 정수로 자르면
 * 10,006 x 3 = 30,018원 — 2원이 사라진다. 평균 체결 단가가 이 프로젝트의 최종
 * 산출물이므로 그 오차를 여기서 만들면 안 된다.
 */
static void test_notional_is_exact_across_price_levels(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1);
    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 2);

    order_t       req = logical_req(SIDE_BUY, 10010, 3, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 3, 0, ORDER_LIMIT);
    exec_report_t rep;

    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.filled_qty == 3);
    assert(rep.notional == 10000 + 2 * 10010);
    assert(omap_notional(fx.map, req.id) == 30020);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

/*
 * T7-07 — 체결이 EXEC_FILLS_MAX 건을 넘어 목록이 잘려도 금액이 새지 않는다.
 *
 * 잘린 뒤의 몫(9주 = 6주 @10000 + 3주 @10010 = 90,030원)을 평균 하나로 넣으면
 * 10,003 x 9 = 90,027원 — 3원이 사라진다. 화면에서 원장 접수 응답의 평균가와
 * 주문 상세의 평균가가 1원 다르게 보여 드러났다.
 */
static void test_notional_is_exact_when_fill_list_truncated(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    const qty_t cheap = EXEC_FILLS_MAX + 6;
    for (qty_t i = 0; i < cheap; i++) {
        put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1);
    }
    for (int32_t i = 0; i < 3; i++) {
        put(&fx, MARKET_KRX, SIDE_SELL, 10010, 1);
    }

    order_t       req = logical_req(SIDE_BUY, 10010, cheap + 3, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, cheap + 3, 0, ORDER_LIMIT);
    exec_report_t rep;

    assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) == ERR_OK);
    assert(rep.filled_qty == cheap + 3);
    int64_t want = (int64_t)cheap * 10000 + 3 * 10010;
    assert(rep.notional == want);
    assert(omap_notional(fx.map, req.id) == want);

    check_invariant(&fx, &req, &rep);
    fx_free(&fx);
}

/* --- 6. 인자 검사 --- */

static void test_args(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    order_t       req = logical_req(SIDE_BUY, 10000, 100, T_BOTH);
    exec_plan_t   plan = manual_plan(&req, 40, 60, ORDER_LIMIT);
    exec_report_t rep;

    assert(exec_submit(NULL, &fx.venues, &req, &plan, &rep) == ERR_NULL_PTR);
    assert(exec_submit(fx.map, NULL, &req, &plan, &rep) == ERR_NULL_PTR);
    assert(exec_submit(fx.map, &fx.venues, NULL, &plan, &rep) == ERR_NULL_PTR);
    assert(exec_submit(fx.map, &fx.venues, &req, NULL, &rep) == ERR_NULL_PTR);
    assert(exec_submit(fx.map, &fx.venues, &req, &plan, NULL) == ERR_NULL_PTR);

    /* 계획이 원 주문과 안 맞으면 아무 시장에도 보내지 않는다. */
    exec_plan_t bad = manual_plan(&req, 40, 50, ORDER_LIMIT);
    assert(exec_submit(fx.map, &fx.venues, &req, &bad, &rep) == ERR_INVALID_QTY);
    assert(rep.status == STATUS_REJECTED);
    assert(omap_count(fx.map) == 0);

    /* 없는 논리 주문의 상태는 조회되지 않는다. */
    order_status_t st;
    assert(exec_status(fx.map, 9999, &st) == ERR_NOT_FOUND);
    assert(exec_status(fx.map, req.id, NULL) == ERR_NULL_PTR);

    /* 이름은 NULL을 돌려주지 않는다. */
    assert(exec_status_name(STATUS_FILLED) != NULL);
    assert(exec_status_name((order_status_t)99) != NULL);

    fx_free(&fx);
}

/*
 * 완료 조건 2의 전수 확인 — 배분과 유동성을 바꿔 가며 불변조건이 언제나 성립하는지.
 * 한 경우라도 어긋나면 논리 주문의 잔량 계산 전체를 믿을 수 없다.
 */
static void test_invariant_exhaustive(void)
{
    static const qty_t LIQ[] = {0, 1, 17, 50, 99, 200};
    const int32_t      NLIQ = (int32_t)(sizeof(LIQ) / sizeof(LIQ[0]));

    for (int32_t a = 0; a < NLIQ; a++) {
        for (int32_t b = 0; b < NLIQ; b++) {
            for (qty_t krx = 0; krx <= 100; krx += 25) {
                fixture_t fx;
                fx_init(&fx, false, T_BOTH);

                if (LIQ[a] > 0) {
                    put(&fx, MARKET_KRX, SIDE_SELL, 10000, LIQ[a]);
                }
                if (LIQ[b] > 0) {
                    put(&fx, MARKET_NXT, SIDE_SELL, 10000, LIQ[b]);
                }

                order_t     req = logical_req(SIDE_BUY, 10000, 100, T_BOTH);
                exec_plan_t plan =
                    manual_plan(&req, krx, 100 - krx, ORDER_LIMIT);
                exec_report_t rep;

                assert(exec_submit(fx.map, &fx.venues, &req, &plan, &rep) ==
                       ERR_OK);
                check_invariant(&fx, &req, &rep);

                fx_free(&fx);
            }
        }
    }
}

int main(void)
{
    test_both_partially_filled();
    test_only_one_side_fills();
    test_one_leg_rejected_other_stands();
    test_all_legs_rejected();
    test_fully_filled();
    test_nothing_fills_but_rests();
    test_ioc_leftover_is_canceled();
    test_notional_is_exact_across_price_levels();
    test_notional_is_exact_when_fill_list_truncated();
    test_args();
    test_invariant_exhaustive();
    return 0;
}
