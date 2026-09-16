#include "consolidated.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"

void cons_init(cons_book_t *cons)
{
    if (cons != NULL) {
        memset(cons, 0, sizeof(*cons));
    }
}

static bool valid_market(market_t market)
{
    return (int32_t)market >= 0 && (int32_t)market < MARKET_COUNT;
}

int cons_attach(cons_book_t *cons, market_t market, const order_book_t *book,
                const market_rules_t *rules)
{
    if (cons == NULL) {
        return ERR_NULL_PTR;
    }
    if (!valid_market(market) || book == NULL) {
        return ERR_INVALID_ARG;
    }
    cons->book[market] = book;
    cons->rules[market] = rules;
    return ERR_OK;
}

bool cons_is_open(const cons_book_t *cons, market_t market, ts_t ts)
{
    if (cons == NULL || !valid_market(market) || cons->book[market] == NULL) {
        return false;
    }
    if (cons->rules[market] == NULL) {
        return true; /* 규칙을 안 붙였으면 세션 판정을 하지 않는다 */
    }
    market_session_t session = SESSION_CLOSED;
    return cons->rules[market]->is_open(ts, &session);
}

/*
 * 동률 판정을 한 곳에 모은다. 최우선호가와 스냅샷이 같은 순서를 써야 하므로
 * 규칙이 두 군데로 갈리면 안 된다.
 *
 * a가 b보다 앞서면 true. 가격이 같으면 잔량이 많은 쪽, 잔량도 같으면 시장 열거 순서.
 */
static bool better(side_t side, price_t pa, qty_t qa, market_t ma, price_t pb,
                   qty_t qb, market_t mb)
{
    if (pa != pb) {
        /* 매수는 비싼 쪽이, 매도는 싼 쪽이 앞선다 */
        return (side == SIDE_BUY) ? (pa > pb) : (pa < pb);
    }
    if (qa != qb) {
        return qa > qb;
    }
    return (int32_t)ma < (int32_t)mb;
}

static price_t best_of(const cons_book_t *cons, side_t side, ts_t ts,
                       market_t *out_market)
{
    price_t  best = BOOK_PRICE_NONE;
    qty_t    best_qty = 0;
    market_t best_market = MARKET_KRX;
    bool     found = false;

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (!cons_is_open(cons, (market_t)m, ts)) {
            continue;
        }
        price_t p = (side == SIDE_BUY) ? book_best_bid(cons->book[m])
                                       : book_best_ask(cons->book[m]);
        if (p == BOOK_PRICE_NONE) {
            continue;
        }
        qty_t q = book_qty_at(cons->book[m], side, p);

        if (!found ||
            better(side, p, q, (market_t)m, best, best_qty, best_market)) {
            best = p;
            best_qty = q;
            best_market = (market_t)m;
            found = true;
        }
    }

    if (found && out_market != NULL) {
        *out_market = best_market;
    }
    return found ? best : BOOK_PRICE_NONE;
}

price_t cons_best_bid(const cons_book_t *cons, ts_t ts, market_t *out_market)
{
    return (cons != NULL) ? best_of(cons, SIDE_BUY, ts, out_market)
                          : BOOK_PRICE_NONE;
}

price_t cons_best_ask(const cons_book_t *cons, ts_t ts, market_t *out_market)
{
    return (cons != NULL) ? best_of(cons, SIDE_SELL, ts, out_market)
                          : BOOK_PRICE_NONE;
}

qty_t cons_qty_at(const cons_book_t *cons, side_t side, price_t price, ts_t ts)
{
    if (cons == NULL) {
        return 0;
    }

    qty_t sum = 0;
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (cons_is_open(cons, (market_t)m, ts)) {
            sum += book_qty_at(cons->book[m], side, price);
        }
    }
    return sum;
}

int cons_snapshot(const cons_book_t *cons, side_t side, int depth, ts_t ts,
                  cons_level_view_t *out)
{
    if (cons == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (depth <= 0 || (side != SIDE_BUY && side != SIDE_SELL)) {
        return ERR_INVALID_ARG;
    }

    /*
     * 시장별로 depth단씩 떠 놓고 병합한다. 각 시장에서 depth단이면 통합 depth단을
     * 채우기에 충분하다 — 한 시장이 통합 상위를 전부 차지하는 것이 최악이다.
     */
    enum { PER_MARKET_MAX = 64 };
    if (depth > PER_MARKET_MAX) {
        depth = PER_MARKET_MAX;
    }

    level_view_t per[MARKET_COUNT][PER_MARKET_MAX];
    int          n[MARKET_COUNT];
    int          idx[MARKET_COUNT];

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        idx[m] = 0;
        if (!cons_is_open(cons, (market_t)m, ts)) {
            n[m] = 0;
            continue;
        }
        int rc = book_snapshot(cons->book[m], side, depth, per[m]);
        n[m] = (rc > 0) ? rc : 0;
    }

    int filled = 0;
    while (filled < depth) {
        int32_t pick = -1;

        for (int32_t m = 0; m < MARKET_COUNT; m++) {
            if (idx[m] >= n[m]) {
                continue;
            }
            const level_view_t *cand = &per[m][idx[m]];
            if (pick < 0) {
                pick = m;
                continue;
            }
            const level_view_t *cur = &per[pick][idx[pick]];
            if (better(side, cand->price, cand->total_qty, (market_t)m,
                       cur->price, cur->total_qty, (market_t)pick)) {
                pick = m;
            }
        }

        if (pick < 0) {
            break; /* 양쪽 다 바닥났다 */
        }

        const level_view_t *src = &per[pick][idx[pick]++];
        out[filled].price = src->price;
        out[filled].total_qty = src->total_qty;
        out[filled].order_count = src->order_count;
        out[filled].market = (market_t)pick;
        filled++;
    }

    return filled;
}
