#include <stddef.h>

#include "errors.h"
#include "strategy.h"

/*
 * KRX_ONLY — 기준선.
 *
 * KRX에만 전량 보낸다. NXT를 **아예 보지 않는다.** 통합 호가창을 읽지도 않는다.
 *
 * 2025년 3월 NXT 출범 이전의 집행이 이랬다. 그래서 이 전략은 "SOR을 안 했을 때"를
 * 뜻하고, 다른 세 전략의 개선폭은 전부 이 전략 대비로 말한다.
 * 기준선이 KRX인 이유는 NXT가 KRX 상장 종목 중 일부만 거래하기 때문이다 —
 * 모든 종목에 대해 항상 성립하는 선택지는 KRX뿐이다(SPEC 1).
 *
 * 거부하는 경우는 하나뿐이다: KRX가 닫혀 있을 때. NXT가 열려 있어도 거부한다.
 * 그게 이 전략의 정의이고, 그 손해가 곧 다른 전략이 만드는 차이다.
 */
static int krx_only_plan(const exec_strategy_t *self, const exec_context_t *ctx,
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

    /*
     * 호가가 있는지는 보지 않는다. 지정가 주문은 상대 호가가 없어도 등록되는 것이
     * 정상이고, "보낼 수 있는가"와 "지금 체결되는가"는 다른 질문이다.
     * 확인하는 것은 시장이 열려 있는가뿐이다.
     */
    if (!cons_is_open(ctx->cons, MARKET_KRX, ctx->ts)) {
        out->reason = ERR_MARKET_CLOSED;
        return out->reason;
    }

    int rc = plan_add_leg(out, MARKET_KRX, req->qty, req->price, req->type);
    if (rc != ERR_OK) {
        plan_init(out);
        out->reason = rc;
        return rc;
    }

    return ERR_OK;
}

const exec_strategy_t STRATEGY_KRX_ONLY = {
    .name = "KRX_ONLY",
    .plan = krx_only_plan,
};
