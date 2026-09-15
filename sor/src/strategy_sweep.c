#include <stdbool.h>
#include <stddef.h>

#include "errors.h"
#include "strategy.h"

/*
 * SWEEP — 유리한 호가부터 차례로 쓸어 담는다 (docs/SPEC.md 5).
 *
 * 통합 호가창을 가격 순으로 훑으며 각 단의 잔량만큼 가져간다. 한 시장의 한 단을
 * 다 쓰면 다음으로 유리한 단으로 넘어가는데, 그 단이 같은 시장일 수도 다른 시장일
 * 수도 있다. 결과적으로 **양 시장을 하나의 호가창으로 합쳐 놓고 소진한 것과 같다.**
 *
 * ---
 *
 * **"유리한 시장"을 최선집행 평가(T2-03)가 아니라 가격으로 정한 이유.**
 *
 * 둘은 알갱이가 다르다. 평가는 "이 주문 전체를 어느 시장에 보낼 것인가"라는
 * **시장 단위** 판단이고, SWEEP이 매 단계에서 묻는 것은 "다음 한 호가를 어디서
 * 가져올 것인가"라는 **호가 단위** 판단이다.
 *
 * 평가 점수로 순서를 정하면 두 가지가 깨진다.
 *  1. 완료 조건인 "양 시장을 합쳐 가격 순으로 훑은 것과 결과가 같다"가 성립하지 않는다.
 *     점수에는 수수료와 스프레드가 섞여 있어 가격 순서를 뒤집을 수 있다
 *  2. 평가의 체결 가능성 항목은 주문 **전체**를 채울 수 있는지를 본다. 단을 하나씩
 *     소진하는 중에는 그 값이 매번 달라져, 같은 주문 안에서 순서 기준이 흔들린다
 *
 * 수수료 차이는 여기서 보지 않는다. 수수료로 시장을 고르는 것은 BEST_PRICE의 몫이고,
 * 두 전략을 나란히 놓고 비교하는 것이 이 Phase의 목적이다.
 *
 * ---
 *
 * 지정가를 넘는 단에서 멈춘다. 다 쓸어 담고도 수량이 남으면 그 잔량은 등록해야
 * 하는데, 어느 시장에 등록할지는 `plan_resting_market()`이 정한다.
 */

/*
 * 훑을 통합 호가 단수.
 * ponytail: 64단이면 현실적인 주문은 다 덮는다. 이보다 깊이 파고들어야 하는 주문은
 * 남는 잔량이 등록 시장으로 몰린다. 그 경우가 측정에서 유의미해지면
 * cons_snapshot에 시작 위치를 받는 형태를 더한다.
 */
#define SWEEP_DEPTH 64

static int sweep_plan(const exec_strategy_t *self, const exec_context_t *ctx,
                      const order_t *req, exec_plan_t *out)
{
    (void)self;

    if (ctx == NULL || req == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    plan_init(out);

    if (req->qty < QTY_MIN || req->qty > QTY_MAX) {
        out->reason = ERR_INVALID_QTY;
        return out->reason;
    }
    if (req->side != SIDE_BUY && req->side != SIDE_SELL) {
        out->reason = ERR_INVALID_ARG;
        return out->reason;
    }

    side_t maker_side = (req->side == SIDE_BUY) ? SIDE_SELL : SIDE_BUY;

    cons_level_view_t levels[SWEEP_DEPTH];
    int n = cons_snapshot(ctx->cons, maker_side, SWEEP_DEPTH, ctx->ts, levels);
    if (n < 0) {
        out->reason = n;
        return n;
    }

    /*
     * 시장별로 합산해 둔다. 한 시장의 여러 단을 가져가는 일이 흔한데, 단마다 다리를
     * 만들면 한 시장에 다리가 여럿 생겨 논리↔물리 매핑이 복잡해진다(T2-05의 규칙).
     */
    qty_t alloc[MARKET_COUNT] = {0};
    qty_t remaining = req->qty;

    for (int i = 0; i < n && remaining > 0; i++) {
        /* 지정가를 넘으면 더 볼 필요가 없다 — 뒤는 더 불리하다. */
        bool within = (req->side == SIDE_BUY) ? (levels[i].price <= req->price)
                                              : (levels[i].price >= req->price);
        if (!within) {
            break;
        }

        qty_t take =
            (remaining < levels[i].total_qty) ? remaining : levels[i].total_qty;
        alloc[levels[i].market] += take;
        remaining -= take;
    }

    /* 다 쓸어 담고도 남았다. 그 잔량은 어딘가에 등록되어야 한다. */
    if (remaining > 0) {
        market_t resting = MARKET_KRX;
        int rc = plan_resting_market(ctx, req, &resting);
        if (rc != ERR_OK) {
            out->reason = rc;
            return rc;
        }
        alloc[resting] += remaining;
        remaining = 0;
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        int rc = plan_add_leg(out, (market_t)m, alloc[m], req->price, req->type);
        if (rc != ERR_OK) {
            plan_init(out);
            out->reason = rc;
            return rc;
        }
    }

    int rc = plan_validate(out, req);
    if (rc != ERR_OK) {
        plan_init(out);
        out->reason = rc;
        return rc;
    }

    return ERR_OK;
}

const exec_strategy_t STRATEGY_SWEEP = {
    .name = "SWEEP",
    .plan = sweep_plan,
};
