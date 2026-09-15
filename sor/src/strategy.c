#include "strategy.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"

/*
 * 전략들이 함께 쓰는 조각. 전략 자체는 파일을 따로 둔다 —
 * 한 파일에 몰면 "이 전략이 무엇을 보는가"가 섞여 읽기 어려워진다.
 */

void plan_init(exec_plan_t *plan)
{
    if (plan == NULL) {
        return;
    }
    memset(plan, 0, sizeof(*plan));
    plan->reason = ERR_OK;
}

int plan_add_leg(exec_plan_t *plan, market_t market, qty_t qty, price_t price,
                 order_type_t type)
{
    if (plan == NULL) {
        return ERR_NULL_PTR;
    }
    if ((int32_t)market < 0 || (int32_t)market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }

    /*
     * 0주짜리 다리는 만들지 않는다. 남기면 T2-09가 빈 물리 주문을 만들게 되고,
     * 그 주문의 체결·취소를 다루는 분기가 전부 군더더기가 된다.
     */
    if (qty <= 0) {
        return ERR_OK;
    }
    if (plan->leg_count >= PLAN_LEGS_MAX) {
        return ERR_BOOK_FULL;
    }

    plan_leg_t *leg = &plan->legs[plan->leg_count++];
    leg->market = market;
    leg->qty = qty;
    leg->limit_price = price;
    leg->type = type;

    plan->planned_qty += qty;
    return ERR_OK;
}

int plan_validate(const exec_plan_t *plan, const order_t *req)
{
    if (plan == NULL || req == NULL) {
        return ERR_NULL_PTR;
    }

    if (plan->leg_count == 0) {
        /* 빈 계획은 "집행하지 않는다"는 유효한 답이다. 수량 합도 0이어야 한다. */
        return (plan->planned_qty == 0) ? ERR_OK : ERR_INVALID_QTY;
    }
    if (plan->leg_count > PLAN_LEGS_MAX) {
        return ERR_INVALID_ARG;
    }

    qty_t sum = 0;
    bool  seen[MARKET_COUNT] = {false};

    for (int32_t i = 0; i < plan->leg_count; i++) {
        const plan_leg_t *leg = &plan->legs[i];

        if ((int32_t)leg->market < 0 || (int32_t)leg->market >= MARKET_COUNT) {
            return ERR_INVALID_ARG;
        }
        if (leg->qty <= 0) {
            return ERR_INVALID_QTY;
        }
        /*
         * 한 시장에 두 다리를 보내는 것은 금지한다. 논리↔물리 매핑만 복잡해지고
         * 얻는 것이 없다 — 같은 시장의 같은 가격이면 한 주문으로 보내면 된다.
         */
        if (seen[leg->market]) {
            return ERR_DUPLICATE;
        }
        seen[leg->market] = true;

        sum += leg->qty;
    }

    if (sum != plan->planned_qty) {
        return ERR_INVALID_QTY; /* 누적값이 다리와 어긋났다 */
    }
    /* 쪼개다가 한 주라도 새면 논리 주문의 잔량 계산이 전부 어긋난다. */
    if (sum != req->qty) {
        return ERR_INVALID_QTY;
    }

    return ERR_OK;
}

const char *strategy_name(const exec_strategy_t *strategy)
{
    if (strategy == NULL || strategy->name == NULL) {
        return "알 수 없는 전략";
    }
    return strategy->name;
}

int plan_resting_market(const exec_context_t *ctx, const order_t *req,
                         market_t *out_market)
{
    if (ctx == NULL || req == NULL || out_market == NULL) {
        return ERR_NULL_PTR;
    }

    /* 매수면 상대는 매도호가다. */
    side_t maker_side = (req->side == SIDE_BUY) ? SIDE_SELL : SIDE_BUY;

    int32_t pick = -1;
    price_t pick_quote = BOOK_PRICE_NONE;

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (!cons_is_open(ctx->cons, (market_t)m, ctx->ts)) {
            continue;
        }

        const order_book_t *book = ctx->cons->book[m];
        price_t quote = (maker_side == SIDE_SELL) ? book_best_ask(book)
                                                  : book_best_bid(book);

        if (pick < 0) {
            pick = m;
            pick_quote = quote;
            continue;
        }

        /* 호가가 있는 시장이 없는 시장을 이긴다. */
        if (pick_quote == BOOK_PRICE_NONE && quote != BOOK_PRICE_NONE) {
            pick = m;
            pick_quote = quote;
            continue;
        }
        if (quote == BOOK_PRICE_NONE) {
            continue;
        }
        /* 둘 다 호가가 있으면 유리한 쪽. 같으면 먼저 본 시장이 남는다. */
        bool better = (req->side == SIDE_BUY) ? (quote < pick_quote)
                                              : (quote > pick_quote);
        if (better) {
            pick = m;
            pick_quote = quote;
        }
    }

    if (pick < 0) {
        return ERR_MARKET_CLOSED;
    }
    *out_market = (market_t)pick;
    return ERR_OK;
}
