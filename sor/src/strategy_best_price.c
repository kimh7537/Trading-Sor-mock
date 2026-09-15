#include <stddef.h>

#include "errors.h"
#include "strategy.h"

/*
 * BEST_PRICE — 최선집행 평가(T2-03)에서 이긴 시장 하나에 전량 보낸다.
 *
 * **쪼개지 않는다.** 이긴 시장의 잔량이 요구 수량보다 적어도 그대로 전량 보낸다.
 * 못 채운 잔량은 그 시장에 등록된다.
 *
 * 쪼개지 않는 것이 이 전략의 정의다. SPLIT·SWEEP과 나란히 놓고 봐야
 * "쪼개는 것이 실제로 이득인가"를 숫자로 말할 수 있다. 여기서 미리 쪼개 버리면
 * 세 전략이 같은 것을 하게 되고 비교할 것이 없어진다.
 *
 * 평가가 후보를 하나도 못 찾으면(어느 시장에서도 지금 체결이 안 되면) 거부하지 않고
 * 등록할 시장을 따로 정한다. 근거는 plan_resting_market()의 주석에 있다.
 */
static int best_price_plan(const exec_strategy_t *self,
                           const exec_context_t *ctx, const order_t *req,
                           exec_plan_t *out)
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

    venue_score_t scores[MARKET_COUNT];
    int rc =
        be_evaluate(ctx->cons, req, ctx->ts, ctx->weights, ctx->config, scores);
    if (rc != ERR_OK) {
        out->reason = rc;
        return rc;
    }

    market_t pick = MARKET_KRX;
    rc = be_pick(scores, &pick);

    if (rc == ERR_NO_LIQUIDITY) {
        /* 지금 체결되는 시장이 없다. 그래도 주문은 어딘가에 놓여야 한다. */
        rc = plan_resting_market(ctx, req, &pick);
    }
    if (rc != ERR_OK) {
        out->reason = rc;
        return rc;
    }

    rc = plan_add_leg(out, pick, req->qty, req->price, req->type);
    if (rc != ERR_OK) {
        plan_init(out);
        out->reason = rc;
        return rc;
    }

    return ERR_OK;
}

const exec_strategy_t STRATEGY_BEST_PRICE = {
    .name = "BEST_PRICE",
    .plan = best_price_plan,
};
