#include <stdbool.h>
#include <stddef.h>

#include "errors.h"
#include "strategy.h"

/*
 * SPLIT — 양 시장의 즉시 체결 가능 잔량에 **비례해서** 나눠 보낸다 (docs/SPEC.md 5).
 *
 * 비례의 기준을 "호가창에 쌓인 전체 물량"이 아니라 **"이 주문의 지정가 안에서 지금
 * 채울 수 있는 수량"**으로 잡았다. 지정가 밖의 물량은 이 주문에게는 없는 것과 같고,
 * 그걸 비례 기준에 넣으면 못 쓰는 물량 쪽으로 주문이 쏠린다.
 *
 * ---
 *
 * **단수 처리 — 최대 나머지 방식(Hare quota).**
 *
 * qty x fill[m] / total 은 정수 나눗셈이라 내림이 생기고, 내림의 합은 원 주문보다
 * 최대 (시장 수 - 1)주 모자란다. 그 남는 주를 **나머지가 큰 시장부터** 한 주씩 준다.
 * 나머지가 같으면 잔량이 많은 시장, 그것도 같으면 시장 열거 순서.
 *
 * 다른 방법(항상 KRX에 몰아주기, 잔량 많은 쪽에 몰아주기)도 결정적이기는 하지만,
 * 최대 나머지는 **비례 관계를 가장 적게 왜곡한다**는 성질이 있다. 1주 차이가 작아
 * 보여도 소액 주문에서는 비율이 크게 흔들린다 — 3주를 47:53으로 나누는 경우를
 * 생각하면 단수 하나가 전체 배분을 33%씩 움직인다.
 *
 * 최소 수량 제약은 1주다. 국내 주식은 2014년 이후 1주 단위로 거래되므로 별도의
 * 최소 매매 단위가 없다. 비례 몫이 0주로 떨어진 시장은 다리를 만들지 않는다.
 *
 * ---
 *
 * 불변조건: 다리 수량의 합이 원 주문 수량과 **정확히** 같다.
 * 한 주라도 새면 논리 주문의 잔량 계산이 전부 어긋난다. `plan_validate()`가 검사한다.
 */

typedef struct {
    bool    open;
    qty_t   fillable;
    qty_t   base;      /* 비례 몫의 내림 */
    int64_t remainder; /* 나눗셈의 나머지. 단수 배분의 우선순위 */
    bool    got_extra;
} split_share_t;

/* a가 b보다 단수를 먼저 받을 자격이 있는가. */
static bool prefers(const split_share_t *a, market_t ma, const split_share_t *b,
                    market_t mb)
{
    if (a->remainder != b->remainder) {
        return a->remainder > b->remainder;
    }
    if (a->fillable != b->fillable) {
        return a->fillable > b->fillable;
    }
    return (int32_t)ma < (int32_t)mb;
}

static int split_plan(const exec_strategy_t *self, const exec_context_t *ctx,
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

    split_share_t share[MARKET_COUNT] = {0};
    int32_t       open_count = 0;
    int32_t       only_open = -1;
    int64_t       total = 0;

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        share[m].open = cons_is_open(ctx->cons, (market_t)m, ctx->ts);
        if (!share[m].open) {
            continue;
        }
        open_count++;
        only_open = m;
        /*
         * want에 req->qty가 아니라 QTY_MAX를 넘긴다.
         *
         * book_qty_up_to()는 요구 수량에 도달하면 세기를 멈춘다. 그대로 쓰면 양쪽 다
         * 주문보다 물량이 많을 때 두 잔량이 모두 req->qty로 잘려 **비율이 1:1로
         * 뭉개진다.** 10,000주 대 20,000주인 시장에 100주를 내면 50:50이 되는데,
         * 그건 "잔량에 비례"가 아니다. 지정가 안의 전체 물량을 세야 비율이 산다.
         */
        share[m].fillable =
            book_qty_up_to(ctx->cons->book[m], maker_side, req->price, QTY_MAX);
        total += share[m].fillable;
    }

    if (open_count == 0) {
        out->reason = ERR_MARKET_CLOSED;
        return out->reason;
    }

    /*
     * 한쪽만 열려 있으면 나눌 것이 없다. 열린 쪽에 전량.
     * 잔량이 0이어도 마찬가지다 — 등록은 되어야 한다.
     */
    if (open_count == 1) {
        int rc =
            plan_add_leg(out, (market_t)only_open, req->qty, req->price, req->type);
        if (rc != ERR_OK) {
            plan_init(out);
            out->reason = rc;
        }
        return out->reason;
    }

    /*
     * 양쪽 다 열려 있는데 지금 채울 수 있는 물량이 어디에도 없다.
     * 비례의 기준이 0이라 나눌 수 없으므로 등록할 시장을 정해 한 다리로 보낸다.
     */
    if (total == 0) {
        market_t resting = MARKET_KRX;
        int rc = plan_resting_market(ctx, req, &resting);
        if (rc == ERR_OK) {
            rc = plan_add_leg(out, resting, req->qty, req->price, req->type);
        }
        if (rc != ERR_OK) {
            plan_init(out);
            out->reason = rc;
        }
        return out->reason;
    }

    /* 비례 몫의 내림과 나머지를 구한다. */
    qty_t assigned = 0;
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (!share[m].open) {
            continue;
        }
        int64_t numerator = (int64_t)req->qty * (int64_t)share[m].fillable;
        share[m].base = (qty_t)(numerator / total);
        share[m].remainder = numerator % total;
        assigned += share[m].base;
    }

    /*
     * 내림 때문에 모자란 만큼을 나머지가 큰 시장부터 한 주씩 준다.
     * 모자란 수는 (열린 시장 수 - 1) 이하이므로 한 시장이 두 번 받을 일은 없다.
     */
    qty_t leftover = req->qty - assigned;
    while (leftover > 0) {
        int32_t pick = -1;
        for (int32_t m = 0; m < MARKET_COUNT; m++) {
            if (!share[m].open || share[m].got_extra) {
                continue;
            }
            if (pick < 0 ||
                prefers(&share[m], (market_t)m, &share[pick], (market_t)pick)) {
                pick = m;
            }
        }
        if (pick < 0) {
            break; /* 줄 곳이 없다. 아래 불변조건 검사가 잡는다 */
        }
        share[pick].base++;
        share[pick].got_extra = true;
        leftover--;
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (!share[m].open) {
            continue;
        }
        int rc =
            plan_add_leg(out, (market_t)m, share[m].base, req->price, req->type);
        if (rc != ERR_OK) {
            plan_init(out);
            out->reason = rc;
            return rc;
        }
    }

    /*
     * 합이 어긋났다면 위 계산에 버그가 있는 것이다. 조용히 넘기면 논리 주문의
     * 잔량이 영원히 안 맞는다 — 여기서 잡는다.
     */
    int rc = plan_validate(out, req);
    if (rc != ERR_OK) {
        plan_init(out);
        out->reason = rc;
        return rc;
    }

    return ERR_OK;
}

const exec_strategy_t STRATEGY_SPLIT = {
    .name = "SPLIT",
    .plan = split_plan,
};
