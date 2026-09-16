/*
 * T5-03 — 실제 주문 흐름에서 뽑은 숫자로 대사한다.
 *
 * `core/tests/test_recon.c`는 손으로 만든 숫자를 먹인다. 그것만으로는
 * **대사가 실제 자료 모양과 맞는지** 알 수 없다 — 내가 상상한 주문과
 * `omap`이 실제로 만드는 주문이 다를 수 있다.
 *
 * 그래서 여기서는 진짜 계획을 세우고, 진짜 체결·취소를 반영하고, 그 결과를
 * 대사에 넘긴다. 이 파일이 `sor/tests`에 있는 이유도 그것이다 — `core`는
 * `sor`를 링크하지 않는다(대사가 대상에 매이지 않아야 하므로).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "order_map.h"
#include "recon.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

/*
 * `sor`의 논리 주문을 대사의 모양으로 **옮겨 적는다.**
 *
 * 이 열 줄이 독립성의 값이다. 구조체를 그대로 넘기면 편하지만 그 순간
 * `core`가 `sor`에 매이고, 대사가 대상의 세계관을 물려받는다.
 */
static recon_order_t copy_out(const logical_order_t *lo)
{
    recon_order_t o;
    memset(&o, 0, sizeof(o));
    o.logical_id = lo->logical_id;
    o.order_qty = lo->order_qty;
    o.leg_count = lo->leg_count;

    assert(lo->leg_count <= RECON_LEGS_MAX);
    for (int32_t i = 0; i < lo->leg_count; i++) {
        o.legs[i].phys_id = lo->legs[i].phys_id;
        o.legs[i].sent_qty = lo->legs[i].sent_qty;
        o.legs[i].filled_qty = lo->legs[i].filled_qty;
        o.legs[i].canceled_qty = lo->legs[i].canceled_qty;
        o.legs[i].notional = lo->legs[i].notional;
        o.legs[i].live = lo->legs[i].live;
    }
    return o;
}

static order_t make_order(order_id_t id, qty_t qty, price_t price)
{
    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = id;
    o.side = SIDE_BUY;
    o.price = price;
    o.qty = qty;
    o.type = ORDER_LIMIT;
    return o;
}

/*
 * 731주를 437 + 294로 갈라 보내고, 한쪽은 부분 체결 후 취소, 다른 쪽은
 * 일부만 체결된 채 살아 있다. 숫자는 맞아떨어지지 않게 골랐다.
 */
static order_map_t *drive(order_id_t *out_logical)
{
    order_map_t *map = omap_create(16);
    assert(map != NULL);

    order_t req = make_order(9137, 731, 68400);

    exec_plan_t plan;
    plan_init(&plan);
    assert(plan_add_leg(&plan, MARKET_KRX, 437, req.price, req.type) == ERR_OK);
    assert(plan_add_leg(&plan, MARKET_NXT, 294, req.price, req.type) == ERR_OK);

    order_id_t phys[PLAN_LEGS_MAX];
    assert(omap_register(map, &req, &plan, phys) == ERR_OK);

    assert(omap_on_accept(map, phys[0]) == ERR_OK);
    assert(omap_on_accept(map, phys[1]) == ERR_OK);

    /* KRX 다리: 291주 체결 후 잔량 146주 취소 */
    assert(omap_on_fill(map, phys[0], 291, 68350) == ERR_OK);
    assert(omap_on_cancel(map, phys[0], 146) == ERR_OK);

    /* NXT 다리: 113주만 체결. 아직 살아 있다 */
    assert(omap_on_fill(map, phys[1], 113, 68400) == ERR_OK);

    *out_logical = req.id;
    return map;
}

/* 진짜 흐름의 결과는 대사를 통과해야 한다 */
static void test_live_flow_is_clean(void)
{
    order_id_t   lid = 0;
    order_map_t *map = drive(&lid);

    const logical_order_t *lo = omap_get(map, lid);
    assert(lo != NULL);

    recon_order_t   o = copy_out(lo);
    recon_finding_t buf[16];
    recon_report_t  rep;
    recon_report_init(&rep, buf, 16);

    assert(recon_order(&o, &rep) == ERR_OK);
    if (!recon_clean(&rep)) {
        for (int32_t i = 0; i < rep.count; i++) {
            fprintf(stderr, "  %s (논리 %llu, 물리 %llu) 기대 %lld 실제 %lld\n",
                    recon_kind_str(rep.at[i].kind),
                    (unsigned long long)rep.at[i].logical_id,
                    (unsigned long long)rep.at[i].phys_id,
                    (long long)rep.at[i].expected,
                    (long long)rep.at[i].actual);
        }
    }
    assert(recon_clean(&rep));

    /* 대사가 본 숫자가 omap의 합계와 같다 — 옮겨 적기가 맞았다 */
    assert(omap_filled_qty(map, lid) == 291 + 113);
    assert(omap_canceled_qty(map, lid) == 146);
    assert(omap_remaining(map, lid) == 294 - 113);

    omap_destroy(map);
}

/*
 * **대사가 실제로 잡는지 본다.**
 *
 * 진짜 흐름을 만든 뒤 옮겨 적은 숫자 한 칸을 망가뜨린다. 원장과 체결 중
 * 한쪽만 틀어지는 상황이 실제로 이 모양이다 — 통보 하나를 놓치면 다리의
 * 체결 수량만 뒤처진다.
 */
static void test_live_flow_catches_injected(void)
{
    order_id_t   lid = 0;
    order_map_t *map = drive(&lid);

    const logical_order_t *lo = omap_get(map, lid);
    recon_order_t          o = copy_out(lo);

    /* NXT 다리의 체결 통보 하나를 놓쳤다고 하자 — 금액만 남고 수량이 0 */
    o.legs[1].filled_qty = 0;

    recon_finding_t buf[16];
    recon_report_t  rep;
    recon_report_init(&rep, buf, 16);
    assert(recon_order(&o, &rep) == ERR_OK);

    assert(!recon_clean(&rep));
    assert(rep.count == 1);
    assert(rep.at[0].kind == RECON_NOTIONAL);
    assert(rep.at[0].logical_id == lid);
    assert(rep.at[0].phys_id == lo->legs[1].phys_id);

    /* 다리 하나가 통째로 빠지면 합이 안 맞는다 */
    o = copy_out(lo);
    o.leg_count = 1;
    recon_report_init(&rep, buf, 16);
    assert(recon_order(&o, &rep) == ERR_OK);
    assert(rep.count == 1);
    assert(rep.at[0].kind == RECON_LEG_SUM);
    assert(rep.at[0].expected == 731);
    assert(rep.at[0].actual == 437);

    omap_destroy(map);
}

/*
 * 물리 번호 인코딩이 시장별로 겹치지 않는다는 성질을 대사 쪽에서도 확인한다.
 * `omap`이 산술로 번호를 만들므로 겹칠 수가 없는데, **그 전제가 깨지면
 * 대사가 먼저 안다**는 것을 못 박아 둔다.
 */
static void test_live_phys_ids_do_not_collide(void)
{
    order_id_t   lid = 0;
    order_map_t *map = drive(&lid);

    const logical_order_t *lo = omap_get(map, lid);
    recon_order_t          o = copy_out(lo);

    recon_finding_t buf[16];
    recon_report_t  rep;
    recon_report_init(&rep, buf, 16);
    assert(recon_order(&o, &rep) == ERR_OK);
    assert(recon_clean(&rep));

    /* 겹치게 만들면 잡는다 */
    o.legs[1].phys_id = o.legs[0].phys_id;
    recon_report_init(&rep, buf, 16);
    assert(recon_order(&o, &rep) == ERR_OK);
    assert(rep.count == 1);
    assert(rep.at[0].kind == RECON_DUP_PHYS);

    omap_destroy(map);
}

int main(void)
{
    STEP(test_live_flow_is_clean);
    STEP(test_live_flow_catches_injected);
    STEP(test_live_phys_ids_do_not_collide);
    return 0;
}
