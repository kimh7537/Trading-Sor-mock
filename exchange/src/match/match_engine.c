#include "match_internal.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include "errors.h"

match_engine_t *match_engine_create(price_t base_price, int32_t capacity)
{
    if (capacity <= 0) {
        return NULL;
    }

    match_engine_t *eng = calloc(1, sizeof(*eng));
    if (eng == NULL) {
        return NULL;
    }

    eng->capacity = capacity;
    eng->book = book_create(base_price);
    eng->pool = order_pool_create(capacity);
    eng->index = index_create(capacity);

    if (eng->book == NULL || eng->pool == NULL || eng->index == NULL) {
        match_engine_destroy(eng);
        return NULL;
    }
    return eng;
}

void match_engine_destroy(match_engine_t *eng)
{
    if (eng == NULL) {
        return;
    }
    index_destroy(eng->index);
    book_destroy(eng->book);
    order_pool_destroy(eng->pool);
    free(eng);
}

const order_book_t *match_book(const match_engine_t *eng)
{
    return eng != NULL ? eng->book : NULL;
}

void match_set_sink(match_engine_t *eng, const event_sink_t *sink)
{
    if (eng == NULL) {
        return;
    }
    if (sink != NULL) {
        eng->sink = *sink;
    } else {
        eng->sink.fn = NULL;
        eng->sink.ctx = NULL;
    }
}

void match_emit(const match_engine_t *eng, event_type_t type, ts_t ts,
                order_id_t order_id, market_t market, price_t price, qty_t qty,
                qty_t remaining, order_id_t counterparty, int reason)
{
    if (eng->sink.fn == NULL) {
        return; /* 싱크가 없으면 이벤트 구조체를 채우지도 않는다 */
    }

    order_event_t ev = {
        .type = type,
        .ts = ts,
        .order_id = order_id,
        .market = market,
        .price = price,
        .qty = qty,
        .remaining_qty = remaining,
        .counterparty_id = counterparty,
        .reason = reason,
    };
    event_emit(&eng->sink, &ev);
}

void match_result_init(exec_result_t *out, qty_t order_qty)
{
    out->fill_count = 0;
    out->truncated = false;
    out->filled_qty = 0;
    out->notional = 0;
    out->remaining_qty = order_qty;
    out->resting = false;
    out->status = STATUS_NEW;
}

int match_validate(const match_engine_t *eng, const order_t *req)
{
    if (req->id == ORDER_ID_INVALID) {
        return ERR_INVALID_ARG;
    }
    if (req->side != SIDE_BUY && req->side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }
    if (req->qty < QTY_MIN || req->qty > QTY_MAX || req->filled_qty != 0) {
        return ERR_INVALID_QTY;
    }
    /* 같은 주문번호가 이미 살아 있으면 취소·정정이 어느 쪽을 가리키는지 모른다. */
    if (index_get(eng->index, req->id) != NULL) {
        return ERR_DUPLICATE;
    }
    return ERR_OK;
}

/* 이 가격에서 더 체결해도 되는가. limit이 BOOK_PRICE_NONE이면 제한 없음(시장가). */
static bool crosses(side_t taker_side, price_t limit, price_t book_price)
{
    if (limit == BOOK_PRICE_NONE) {
        return true;
    }
    return taker_side == SIDE_BUY ? book_price <= limit : book_price >= limit;
}

static void record_fill(exec_result_t *out, const fill_t *fill)
{
    /* 집계는 목록이 잘려도 정확해야 한다 — 평균 체결 단가가 여기서 나온다. */
    out->filled_qty += fill->qty;
    out->notional += (int64_t)fill->price * (int64_t)fill->qty;

    if (out->fill_count < EXEC_FILLS_MAX) {
        out->fills[out->fill_count++] = *fill;
    } else {
        out->truncated = true;
    }
}

qty_t match_sweep(match_engine_t *eng, const order_t *taker, price_t limit,
                  exec_result_t *out)
{
    side_t maker_side = (taker->side == SIDE_BUY) ? SIDE_SELL : SIDE_BUY;
    qty_t remaining = taker->qty;

    while (remaining > 0) {
        /* 가격 우선: 매번 최우선호가부터 본다. 레벨이 비면 캐시가 다음으로 내려간다. */
        price_t best = (maker_side == SIDE_SELL) ? book_best_ask(eng->book)
                                                 : book_best_bid(eng->book);
        if (best == BOOK_PRICE_NONE || !crosses(taker->side, limit, best)) {
            break;
        }

        /* 시간 우선: 그 레벨의 맨 앞부터 소진한다. */
        order_t *maker = book_front(eng->book, maker_side, best);
        assert(maker != NULL); /* 최우선호가가 있는데 레벨이 비었을 수 없다 */

        qty_t maker_rem = order_remaining_qty(maker);
        qty_t fill_qty = (remaining < maker_rem) ? remaining : maker_rem;
        /* maker는 전량 체결되면 슬롯이 풀로 돌아간다. 이벤트에 쓸 값을 먼저 뜬다. */
        order_id_t maker_id = maker->id;
        market_t maker_market = maker->market;
        qty_t maker_rem_after = maker_rem - fill_qty;


        fill_t fill = {
            .price = best, /* 체결 가격은 먼저 있던 주문의 호가다 (SPEC 4.1) */
            .qty = fill_qty,
            .maker_id = maker_id,
            .taker_id = taker->id,
            .ts = taker->ts,
        };

        if (fill_qty == maker_rem) {
            /* 상대가 전량 체결됐다. 호가창·인덱스에서 빼고 슬롯을 돌려준다. */
            int rc = book_remove(eng->book, maker);
            assert(rc == ERR_OK);
            (void)rc;
            (void)index_remove(eng->index, maker->id);
            maker->filled_qty += fill_qty;
            order_pool_release(eng->pool, maker);
        } else {
            int rc = book_reduce_qty(eng->book, maker, fill_qty);
            assert(rc == ERR_OK);
            (void)rc;
        }

        record_fill(out, &fill);
        remaining -= fill_qty;

        /*
         * 순서 고정: 상대(maker)가 먼저, 들어온 쪽(taker)이 다음.
         * maker 슬롯은 위에서 이미 풀로 돌아갔을 수 있으므로 값을 미리 떠 뒀다.
         */
        match_emit(eng,
                   (maker_rem_after == 0) ? EVENT_EXECUTED
                                          : EVENT_PARTIALLY_EXECUTED,
                   taker->ts, maker_id, maker_market, best, fill_qty,
                   maker_rem_after, taker->id, ERR_OK);
        match_emit(eng,
                   (remaining == 0) ? EVENT_EXECUTED : EVENT_PARTIALLY_EXECUTED,
                   taker->ts, taker->id, taker->market, best, fill_qty,
                   remaining, maker_id, ERR_OK);
    }

    return remaining;
}

int match_rest(match_engine_t *eng, const order_t *req, qty_t remaining,
               exec_result_t *out)
{
    assert(remaining > 0 && remaining <= req->qty);

    order_t *resting = order_pool_acquire(eng->pool);
    if (resting == NULL) {
        return ERR_POOL_EXHAUSTED;
    }

    /* 링크는 풀이 이미 비워 뒀다. 그 위에 요청 내용을 얹는다. */
    resting->id = req->id;
    resting->client_order_id = req->client_order_id;
    resting->ts = req->ts;
    resting->price = req->price;
    resting->qty = req->qty;
    resting->filled_qty = req->qty - remaining; /* 원 수량은 보존한다 */
    resting->side = req->side;
    resting->type = req->type;
    resting->market = req->market;

    int rc = book_insert(eng->book, resting);
    if (rc != ERR_OK) {
        order_pool_release(eng->pool, resting);
        return rc;
    }

    rc = index_put(eng->index, resting->id, resting);
    if (rc != ERR_OK) {
        (void)book_remove(eng->book, resting);
        order_pool_release(eng->pool, resting);
        return rc;
    }

    out->resting = true;
    return ERR_OK;
}

int match_reject(const match_engine_t *eng, ts_t ts, order_id_t id,
                 market_t market, price_t price, qty_t qty, int rc,
                 exec_result_t *out)
{
    out->status = STATUS_REJECTED;
    match_emit(eng, EVENT_REJECTED, ts, id, market, price, qty, qty,
               ORDER_ID_INVALID, rc);
    return rc;
}
