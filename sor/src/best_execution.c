#include "best_execution.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "kv_config.h"

/*
 * 가격 40 / 체결 가능성 30 / 비용 20 / 시장 상태 10.
 * 근거는 docs/decisions/0005-최선집행-가중치.md.
 */
const be_weights_t BE_WEIGHTS_DEFAULT = {
    .price = 40,
    .fill = 30,
    .cost = 20,
    .state = 10,
};

/*
 * 수수료는 시장별 파라미터다. NXT가 KRX보다 낮다고 가정한다 —
 * 대체거래소가 가격 경쟁력으로 물량을 끌어오는 구조이기 때문이다.
 * 실제 수수료율이 아니라 **차이를 만들기 위한 값**이다. 실제와 다를 수 있다.
 */
const be_config_t BE_CONFIG_DEFAULT = {
    .fee_bp = {[MARKET_KRX] = 3, [MARKET_NXT] = 2},
};

/* 0 이상 BE_SCORE_MAX 이하로 자른다. */
static int32_t clamp_score(int64_t v)
{
    if (v < 0) {
        return 0;
    }
    if (v > BE_SCORE_MAX) {
        return BE_SCORE_MAX;
    }
    return (int32_t)v;
}

/*
 * base 대비 얼마나 불리한지를 bp로. base가 0 이하면 0.
 * 정수 나눗셈이므로 1bp 미만의 차이는 0으로 떨어진다 — 호가 단위가 있는 이상
 * 그보다 세밀한 구분은 의미가 없다.
 */
static int64_t disadvantage_bp(price_t worse, price_t better_price)
{
    if (better_price <= 0 || worse <= better_price) {
        return 0;
    }
    return (int64_t)(worse - better_price) * 10000 / (int64_t)better_price;
}

int be_evaluate(const cons_book_t *cons, const order_t *req, ts_t ts,
                const be_weights_t *w, const be_config_t *cfg,
                venue_score_t out[MARKET_COUNT])
{
    if (cons == NULL || req == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (req->side != SIDE_BUY && req->side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }
    if (req->qty < QTY_MIN) {
        return ERR_INVALID_QTY;
    }

    if (w == NULL) {
        w = &BE_WEIGHTS_DEFAULT;
    }
    if (cfg == NULL) {
        cfg = &BE_CONFIG_DEFAULT;
    }

    if (w->price < 0 || w->fill < 0 || w->cost < 0 || w->state < 0) {
        return ERR_INVALID_ARG;
    }
    int64_t wsum = (int64_t)w->price + w->fill + w->cost + w->state;
    if (wsum <= 0) {
        return ERR_INVALID_ARG; /* 가중치가 전부 0이면 순위를 매길 수 없다 */
    }
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (cfg->fee_bp[m] < 0) {
            return ERR_INVALID_ARG;
        }
    }

    memset(out, 0, sizeof(venue_score_t) * MARKET_COUNT);

    /* 매수면 상대는 매도호가다. */
    side_t maker_side = (req->side == SIDE_BUY) ? SIDE_SELL : SIDE_BUY;

    /*
     * 1단계 — 시장별 재료를 모은다. 가격 점수는 두 시장을 비교해야 매길 수 있으므로
     * 여기서는 호가만 뽑고, 최선 호가를 안 뒤에 2단계에서 점수를 준다.
     */
    price_t best_quote = BOOK_PRICE_NONE;

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        venue_score_t *v = &out[m];
        v->quote = BOOK_PRICE_NONE;

        if (!cons_is_open(cons, (market_t)m, ts)) {
            v->reason = ERR_MARKET_CLOSED;
            continue;
        }

        const order_book_t *book = cons->book[m];
        price_t quote =
            (maker_side == SIDE_SELL) ? book_best_ask(book) : book_best_bid(book);
        if (quote == BOOK_PRICE_NONE) {
            v->reason = ERR_NO_LIQUIDITY;
            continue;
        }
        v->quote = quote;

        /* 지정가 안에서 지금 채울 수 있는 수량 */
        v->fillable = book_qty_up_to(book, maker_side, req->price, req->qty);
        if (v->fillable <= 0) {
            /* 호가는 있지만 전부 지정가 밖이다 — 지금 이 주문으로는 못 쓴다. */
            v->reason = ERR_NO_LIQUIDITY;
            continue;
        }

        price_t bid = book_best_bid(book);
        price_t ask = book_best_ask(book);
        v->spread =
            (bid != BOOK_PRICE_NONE && ask != BOOK_PRICE_NONE) ? (ask - bid) : 0;

        v->eligible = true;
        v->reason = ERR_OK;

        /* 매수면 싼 매도호가가, 매도면 비싼 매수호가가 좋다. */
        if (best_quote == BOOK_PRICE_NONE ||
            (req->side == SIDE_BUY ? quote < best_quote : quote > best_quote)) {
            best_quote = quote;
        }
    }

    /* 2단계 — 점수를 매긴다. */
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        venue_score_t *v = &out[m];
        if (!v->eligible) {
            continue;
        }

        /* 가격: 최선 호가 대비 불리함(bp)만큼 깎는다. */
        int64_t bp = (req->side == SIDE_BUY)
                         ? disadvantage_bp(v->quote, best_quote)
                         : disadvantage_bp(best_quote, v->quote);
        v->price_score = clamp_score(BE_SCORE_MAX - bp * BE_BP_PENALTY);

        /* 체결 가능성: 요구 수량 중 지금 채울 수 있는 비율. */
        v->fill_score =
            clamp_score((int64_t)v->fillable * BE_SCORE_MAX / req->qty);

        /* 비용: 수수료도 bp이므로 가격과 같은 계수로 깎는다. */
        v->cost_score =
            clamp_score(BE_SCORE_MAX - (int64_t)cfg->fee_bp[m] * BE_BP_PENALTY);

        /*
         * 시장 상태: 열려 있는 것은 이미 확인했다. 남은 것은 스프레드다.
         * 스프레드가 넓다는 것은 이 시장의 호가가 성기다는 뜻이고, 지금 최우선호가가
         * 좋아 보여도 조금만 더 사면 금방 불리해진다는 신호다.
         */
        int64_t spread_bp =
            (v->quote > 0) ? (int64_t)v->spread * 10000 / (int64_t)v->quote : 0;
        v->state_score = clamp_score(BE_SCORE_MAX - spread_bp * BE_SPREAD_PENALTY);

        int64_t total = (int64_t)v->price_score * w->price +
                        (int64_t)v->fill_score * w->fill +
                        (int64_t)v->cost_score * w->cost +
                        (int64_t)v->state_score * w->state;
        v->total = clamp_score(total / wsum);
    }

    return ERR_OK;
}

int be_pick(const venue_score_t scores[MARKET_COUNT], market_t *out_market)
{
    if (scores == NULL || out_market == NULL) {
        return ERR_NULL_PTR;
    }

    int32_t pick = -1;
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (!scores[m].eligible) {
            continue;
        }
        if (pick < 0) {
            pick = m;
            continue;
        }
        /* 총점 -> 즉시 체결 가능 수량 -> 시장 열거 순서. */
        if (scores[m].total > scores[pick].total) {
            pick = m;
        } else if (scores[m].total == scores[pick].total &&
                   scores[m].fillable > scores[pick].fillable) {
            pick = m;
        }
        /* 셋 다 같으면 먼저 본 시장(열거 순서가 앞선 쪽)이 남는다. */
    }

    if (pick < 0) {
        return ERR_NO_LIQUIDITY;
    }
    *out_market = (market_t)pick;
    return ERR_OK;
}

/* --- 설정 파일 --- */

int be_load_config(const char *path, be_weights_t *w, be_config_t *cfg)
{
    if (path == NULL) {
        return ERR_NULL_PTR;
    }

    /* 적지 않은 키는 기본값을 쓴다. */
    be_weights_t weights = BE_WEIGHTS_DEFAULT;
    be_config_t  config = BE_CONFIG_DEFAULT;

    const kv_entry_t table[] = {
        {"weight_price", kv_parse_i32, &weights.price},
        {"weight_fill", kv_parse_i32, &weights.fill},
        {"weight_cost", kv_parse_i32, &weights.cost},
        {"weight_state", kv_parse_i32, &weights.state},
        {"fee_krx_bp", kv_parse_i32, &config.fee_bp[MARKET_KRX]},
        {"fee_nxt_bp", kv_parse_i32, &config.fee_bp[MARKET_NXT]},
    };
    int rc = kv_config_load(path, table, sizeof(table) / sizeof(table[0]));
    if (rc != ERR_OK) {
        return rc;
    }

    /* 읽은 값이 쓸 수 있는 값인지는 여기서 한 번에 본다. */
    if (weights.price < 0 || weights.fill < 0 || weights.cost < 0 ||
        weights.state < 0) {
        return ERR_INVALID_ARG;
    }
    if ((int64_t)weights.price + weights.fill + weights.cost + weights.state <=
        0) {
        return ERR_INVALID_ARG; /* 전부 0이면 순위를 매길 수 없다 */
    }
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (config.fee_bp[m] < 0) {
            return ERR_INVALID_ARG;
        }
    }

    if (w != NULL) {
        *w = weights;
    }
    if (cfg != NULL) {
        *cfg = config;
    }
    return ERR_OK;
}
