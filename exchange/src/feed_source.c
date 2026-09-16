#include "feed_source.h"

#include <stdio.h>
#include <string.h>

#include "errors.h"

int feed_book_from_engine(const match_engine_t *eng, const char *symbol,
                          market_t market, int32_t depth, feed_book_t *out)
{
    if (eng == NULL || symbol == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (depth < 1 || depth > FEED_DEPTH_MAX) {
        return ERR_INVALID_ARG;
    }
    if (market < 0 || market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }

    const order_book_t *book = match_book(eng);
    if (book == NULL) {
        return ERR_NULL_PTR;
    }

    /*
     * **먼저 전부 0으로 만든다.** 호가창에 depth만큼 없을 때 남는 자리가
     * 0이어야 하는데, 그것을 뒤에서 따로 채우면 빠뜨릴 자리가 생긴다.
     */
    memset(out, 0, sizeof(*out));
    /*
     * 읽는 쪽과 쓰는 쪽을 둘 다 묶는다. `strncpy`는 쓰는 쪽만 묶어서
     * Release에서 `-Wstringop-truncation`으로 깨진다(T5-03에서 겪었다).
     */
    snprintf(out->symbol, sizeof(out->symbol), "%.*s",
             (int)(sizeof(out->symbol) - 1), symbol);
    out->market = market;
    out->depth = depth;

    level_view_t view[FEED_DEPTH_MAX];

    int n = book_snapshot(book, SIDE_BUY, depth, view);
    if (n < 0) {
        return n;
    }
    for (int i = 0; i < n && i < depth; i++) {
        out->bid[i].price = view[i].price;
        out->bid[i].qty = view[i].total_qty;
    }

    n = book_snapshot(book, SIDE_SELL, depth, view);
    if (n < 0) {
        return n;
    }
    for (int i = 0; i < n && i < depth; i++) {
        out->ask[i].price = view[i].price;
        out->ask[i].qty = view[i].total_qty;
    }

    return ERR_OK;
}

int feed_trade_from_fill(const fill_t *f, const char *symbol, market_t market,
                         side_t taker_side, feed_trade_t *out)
{
    if (f == NULL || symbol == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (market < 0 || market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }
    if (taker_side != SIDE_BUY && taker_side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    snprintf(out->symbol, sizeof(out->symbol), "%.*s",
             (int)(sizeof(out->symbol) - 1), symbol);
    out->market = market;
    out->side = taker_side;
    out->price = f->price;
    out->qty = f->qty;

    /*
     * 체결 식별자로 taker의 주문번호를 쓴다. 한 주문이 여러 레벨을 가로지르면
     * 같은 번호가 여러 번 나가는데, **그것이 맞다** — 같은 주문이 만든
     * 체결이라는 사실이 정보이고, 구분이 필요하면 머리의 seq가 이미 다르다.
     */
    out->exec_id = f->taker_id;

    return ERR_OK;
}
