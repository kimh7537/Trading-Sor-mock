#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "match_internal.h"
#include "tick_size.h"

/*
 * 취소와 정정 (docs/SPEC.md 4.4).
 *
 * 전량 체결된 주문은 체결 시점에 이미 호가창과 인덱스에서 빠졌다. 따라서
 * "이미 체결 완료된 주문에 대한 취소·정정은 에러"가 별도 분기 없이 ERR_NOT_FOUND로
 * 자연히 나온다 — 상태 플래그를 따로 들고 다닐 필요가 없다.
 *
 * 주문을 못 찾은 거부는 이벤트를 내지 않는다. 시장도 가격도 모르는 이벤트는
 * 소비자에게 줄 정보가 없다. 에러 코드로만 알린다.
 */

/* 정정 거부. 주문을 찾은 뒤에만 쓴다 — 시장·가격을 알아야 이벤트가 의미가 있다. */
#define REJECT(code) \
    match_reject(eng, ts, id, order->market, order->price, new_qty, (code), out)

int match_cancel(match_engine_t *eng, order_id_t id, ts_t ts,
                 exec_result_t *out)
{
    if (eng == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    match_result_init(out, 0);

    int gate = match_gate_cancel(eng, ts);
    if (gate != ERR_OK) {
        out->status = STATUS_REJECTED;
        return gate;
    }

    order_t *order = index_get(eng->index, id);
    if (order == NULL) {
        out->status = STATUS_REJECTED;
        return ERR_NOT_FOUND;
    }

    qty_t canceled = order_remaining_qty(order);
    assert(canceled > 0); /* 잔량 없는 주문이 호가창에 남아 있을 수 없다 */

    /* 슬롯이 풀로 돌아가면 읽을 수 없다. 이벤트에 쓸 값을 먼저 뜬다. */
    price_t price = order->price;
    market_t market = order->market;

    int rc = book_remove(eng->book, order);
    if (rc != ERR_OK) {
        return rc;
    }
    (void)index_remove(eng->index, id);
    order_pool_release(eng->pool, order);

    out->remaining_qty = canceled; /* 취소된 잔량 */
    out->resting = false;
    out->status = STATUS_CANCELED;

    match_emit(eng, EVENT_CANCELED, ts, id, market, price, canceled, 0,
               ORDER_ID_INVALID, ERR_OK);

    return ERR_OK;
}

int match_modify(match_engine_t *eng, order_id_t id, price_t new_price,
                 qty_t new_qty, ts_t ts, exec_result_t *out)
{
    if (eng == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    match_result_init(out, new_qty);

    /* 정정은 신규 주문과 같은 관문을 지난다. 휴장 구간에서는 취소만 된다. */
    int gate = match_gate_submit(eng, ts, ORDER_LIMIT);
    if (gate != ERR_OK) {
        out->status = STATUS_REJECTED;
        return gate;
    }

    order_t *order = index_get(eng->index, id);
    if (order == NULL) {
        out->status = STATUS_REJECTED;
        return ERR_NOT_FOUND;
    }

    /* 검증을 전부 호가창에서 떼기 전에 한다. 떼고 나서 실패하면 주문이 공중에 뜬다. */
    if (new_price < book_price_low(eng->book) ||
        new_price > book_price_high(eng->book)) {
        return REJECT(ERR_PRICE_LIMIT);
    }
    if (!is_valid_tick(new_price)) {
        return REJECT(ERR_INVALID_TICK);
    }
    /* new_qty는 원 주문 수량이다. 기체결분보다 커야 잔량이 남는다. */
    if (new_qty <= order->filled_qty || new_qty > QTY_MAX) {
        return REJECT(ERR_INVALID_QTY);
    }

    bool same_price = (new_price == order->price);

    /* 같은 가격 + 수량 감소 — 시간 우선순위 유지 */
    if (same_price && new_qty < order->qty) {
        int rc = book_amend_qty(eng->book, order, new_qty);
        if (rc != ERR_OK) {
            return REJECT(rc);
        }
        out->remaining_qty = order_remaining_qty(order);
        out->resting = true;
        out->status = (order->filled_qty > 0) ? STATUS_PARTIAL : STATUS_NEW;
        match_emit(eng, EVENT_MODIFIED, ts, id, order->market, new_price,
                   new_qty, out->remaining_qty, ORDER_ID_INVALID, ERR_OK);
        return ERR_OK;
    }

    /* 아무것도 안 바뀌면 건드리지 않는다 — 괜히 떼면 우선순위만 잃는다 */
    if (same_price && new_qty == order->qty) {
        out->remaining_qty = order_remaining_qty(order);
        out->resting = true;
        out->status = (order->filled_qty > 0) ? STATUS_PARTIAL : STATUS_NEW;
        match_emit(eng, EVENT_MODIFIED, ts, id, order->market, new_price,
                   new_qty, out->remaining_qty, ORDER_ID_INVALID, ERR_OK);
        return ERR_OK;
    }

    /*
     * 가격 변경 또는 수량 증가. 정정된 가격이 반대편과 교차하면 받지 않는다.
     * 정정 시 매칭은 하지 않기 때문이다 — 교차한 채로 두면 호가창이 깨진다.
     */
    price_t opposite = (order->side == SIDE_BUY) ? book_best_ask(eng->book)
                                                 : book_best_bid(eng->book);
    if (opposite != BOOK_PRICE_NONE) {
        bool crossed = (order->side == SIDE_BUY) ? (new_price >= opposite)
                                                 : (new_price <= opposite);
        if (crossed) {
            return REJECT(ERR_INVALID_PRICE);
        }
    }

    /* 떼었다가 큐 뒤에 다시 붙인다 — 이것이 곧 시간 우선순위 상실이다. */
    int rc = book_remove(eng->book, order);
    if (rc != ERR_OK) {
        return REJECT(rc);
    }

    order->price = new_price;
    order->qty = new_qty;

    rc = book_insert(eng->book, order);
    if (rc != ERR_OK) {
        /* 가격은 이미 검증했으므로 여기 오면 용량 문제다. 주문을 버린다. */
        market_t market = order->market;
        (void)index_remove(eng->index, id);
        order_pool_release(eng->pool, order);
        out->status = STATUS_REJECTED;
        match_emit(eng, EVENT_REJECTED, ts, id, market, new_price, new_qty, 0,
                   ORDER_ID_INVALID, rc);
        return rc;
    }

    out->remaining_qty = order_remaining_qty(order);
    out->resting = true;
    out->status = (order->filled_qty > 0) ? STATUS_PARTIAL : STATUS_NEW;
    match_emit(eng, EVENT_MODIFIED, ts, id, order->market, new_price, new_qty,
               out->remaining_qty, ORDER_ID_INVALID, ERR_OK);

    return ERR_OK;
}
