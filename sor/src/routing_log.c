#include "routing_log.h"

#include <stdio.h>
#include <string.h>

#include "errors.h"

void routing_emit(const routing_sink_t *sink, const routing_decision_t *d)
{
    if (sink == NULL || sink->fn == NULL || d == NULL) {
        return;
    }
    sink->fn(d, sink->ctx);
}

int routing_plan(const exec_strategy_t *strategy, const exec_context_t *ctx,
                 const order_t *req, const routing_sink_t *sink,
                 exec_plan_t *out)
{
    if (strategy == NULL || strategy->plan == NULL || ctx == NULL ||
        req == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    int rc = strategy->plan(strategy, ctx, req, out);

    if (sink == NULL || sink->fn == NULL) {
        return rc; /* 소비자가 없으면 근거를 만들 필요도 없다 */
    }

    routing_decision_t d;

    /*
     * 지정 초기화자로 만들면 패딩 바이트가 불특정이다. 결정 기록을 바이트 단위로
     * 비교하는 것이 순서 결정성 확인의 방법이므로(T1-19와 같은 이유) 패딩까지 민다.
     */
    memset(&d, 0, sizeof(d));

    d.logical_id = req->id;
    d.ts = req->ts;
    d.side = req->side;
    d.limit_price = req->price;
    d.order_qty = req->qty;
    d.strategy = strategy_name(strategy);

    /* 실제로 쓴 기준을 남긴다. ctx가 NULL을 줬으면 기본값이 쓰인 것이다. */
    d.weights = (ctx->weights != NULL) ? *ctx->weights : BE_WEIGHTS_DEFAULT;
    d.config = (ctx->config != NULL) ? *ctx->config : BE_CONFIG_DEFAULT;

    /*
     * 평가가 실패해도 결정은 남긴다. 점수가 0으로 남은 기록 자체가 "평가할 재료가
     * 없었다"는 근거다 — 기록을 빼 버리면 그 주문이 왜 그렇게 됐는지 알 수 없다.
     */
    (void)be_evaluate(ctx->cons, req, ctx->ts, &d.weights, &d.config, d.scores);

    for (int32_t i = 0; i < out->leg_count; i++) {
        d.alloc[out->legs[i].market] += out->legs[i].qty;
    }
    d.leg_count = out->leg_count;
    d.reason = (rc == ERR_OK) ? out->reason : rc;

    routing_emit(sink, &d);
    return rc;
}

static const char *side_str(side_t side)
{
    return (side == SIDE_BUY) ? "BUY" : "SELL";
}

int routing_format(const routing_decision_t *d, char *buf, size_t cap)
{
    static const char *MARKET_NAME[MARKET_COUNT] = {"KRX", "NXT"};

    if (d == NULL || buf == NULL || cap == 0) {
        return ERR_NULL_PTR;
    }

    int n = snprintf(
        buf, cap,
        "id=%llu ts=%lld side=%s limit=%d qty=%d strategy=%s w=%d/%d/%d/%d "
        "fee=%d/%d",
        (unsigned long long)d->logical_id, (long long)d->ts, side_str(d->side),
        d->limit_price, d->order_qty,
        (d->strategy != NULL) ? d->strategy : "?", d->weights.price,
        d->weights.fill, d->weights.cost, d->weights.state,
        d->config.fee_bp[MARKET_KRX], d->config.fee_bp[MARKET_NXT]);
    if (n < 0 || (size_t)n >= cap) {
        return ERR_INVALID_ARG;
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        const venue_score_t *v = &d->scores[m];

        int k = snprintf(
            buf + n, cap - (size_t)n,
            " %s[elig=%d quote=%d fill=%d spread=%d s=%d/%d/%d/%d total=%d "
            "alloc=%d]",
            MARKET_NAME[m], v->eligible ? 1 : 0, v->quote, v->fillable,
            v->spread, v->price_score, v->fill_score, v->cost_score,
            v->state_score, v->total, d->alloc[m]);
        if (k < 0 || (size_t)(n + k) >= cap) {
            return ERR_INVALID_ARG;
        }
        n += k;
    }

    int k = snprintf(buf + n, cap - (size_t)n, " legs=%d reason=%d",
                     d->leg_count, d->reason);
    if (k < 0 || (size_t)(n + k) >= cap) {
        return ERR_INVALID_ARG;
    }

    return n + k;
}
