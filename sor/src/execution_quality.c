#include "execution_quality.h"

#include <stddef.h>
#include <string.h>

#include "errors.h"

int32_t eq_to_bp(int64_t value, int64_t base)
{
    if (base <= 0) {
        return 0; /* 나눌 기준이 없으면 비율도 없다 */
    }

    int64_t num = value * BP_SCALE;
    int64_t q = num / base;
    int64_t r = num % base;

    /*
     * 0에서 먼 쪽으로 반올림한다. 나머지는 피제수의 부호를 따르므로 절댓값으로
     * 비교하고, 올림도 피제수의 부호 쪽으로 한다 — 매수와 매도가 대칭이 된다.
     */
    int64_t ar = (r < 0) ? -r : r;
    if (ar * 2 >= base) {
        q += (num < 0) ? -1 : 1;
    }

    return (int32_t)q;
}

price_t eq_avg_price(int64_t notional, qty_t filled_qty)
{
    if (filled_qty <= 0) {
        return 0;
    }
    return (price_t)(notional / filled_qty);
}

int32_t eq_avg_diff_bp(int64_t notional_a, qty_t filled_a, int64_t notional_b,
                       qty_t filled_b)
{
    if (filled_a <= 0 || filled_b <= 0 || notional_a <= 0 || notional_b <= 0) {
        return 0; /* 어느 한쪽이라도 체결이 없으면 가격을 비교할 거리가 없다 */
    }

    /*
     * 평균을 1/EQ_AVG_SCALE원 단위 정수로 만든다. 원 단위로 버리면 1원 = 1bp가
     * 사라진다(T6-07). 체결 금액 x 10000은 체결 금액이 9.2 x 10^14원까지 넘치지
     * 않는다 — 이 시뮬레이터가 한 칸에서 다루는 규모보다 몇 자릿수 크다.
     */
    int64_t avg_a = notional_a * EQ_AVG_SCALE / filled_a;
    int64_t avg_b = notional_b * EQ_AVG_SCALE / filled_b;

    return eq_to_bp(avg_a - avg_b, avg_a);
}

int eq_benchmark(const cons_book_t *cons, const order_t *req, ts_t ts,
                 price_t *out_benchmark)
{
    if (cons == NULL || req == NULL || out_benchmark == NULL) {
        return ERR_NULL_PTR;
    }
    if (req->side != SIDE_BUY && req->side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }

    /* 매수의 상대는 매도호가다. 통합 호가창이 이미 두 시장을 합쳐 답을 안다. */
    price_t quote = (req->side == SIDE_BUY) ? cons_best_ask(cons, ts, NULL)
                                            : cons_best_bid(cons, ts, NULL);
    if (quote == BOOK_PRICE_NONE) {
        return ERR_NO_LIQUIDITY;
    }

    *out_benchmark = quote;
    return ERR_OK;
}

int eq_measure(side_t side, qty_t order_qty, qty_t filled_qty, int64_t notional,
               price_t benchmark, eq_metrics_t *out)
{
    if (out == NULL) {
        return ERR_NULL_PTR;
    }
    if (side != SIDE_BUY && side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }
    if (benchmark <= 0) {
        return ERR_INVALID_PRICE;
    }
    if (order_qty <= 0 || filled_qty < 0 || filled_qty > order_qty) {
        return ERR_INVALID_QTY;
    }
    /* 수량과 금액이 서로 다른 말을 하면 받지 않는다. */
    if (filled_qty == 0 ? (notional != 0) : (notional <= 0)) {
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->side = side;
    out->order_qty = order_qty;
    out->filled_qty = filled_qty;
    out->notional = notional;
    out->benchmark = benchmark;

    out->fill_rate_bp = eq_to_bp(filled_qty, order_qty);

    if (filled_qty == 0) {
        /* 체결이 없다. 0은 "슬리피지 없음"이 아니라 "잴 것이 없음"이다. */
        return ERR_OK;
    }

    out->avg_price = eq_avg_price(notional, filled_qty);

    /* 표시용 슬리피지(원). 불리할수록 양수. */
    out->slippage = (side == SIDE_BUY) ? (int64_t)out->avg_price - benchmark
                                       : (int64_t)benchmark - out->avg_price;

    /*
     * 측정용 슬리피지(bp)는 **평균 단가를 거치지 않는다.**
     *
     * 체결 금액과 "기준가로 다 체결됐다면 들었을 금액"을 직접 비교한다. 평균 단가는
     * 정수 버림이라 최대 1원 오차가 있고, 10,000원짜리에서 1원은 곧 1bp다 —
     * 재려는 크기와 같은 오차를 중간에 끼워 넣을 수 없다.
     */
    int64_t base = (int64_t)benchmark * (int64_t)filled_qty;
    int64_t diff = (side == SIDE_BUY) ? notional - base : base - notional;
    out->slippage_bp = eq_to_bp(diff, base);

    return ERR_OK;
}
