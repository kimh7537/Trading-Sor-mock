
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "match_internal.h"
#include "tick_size.h"

/* 거부 경로가 반복된다. req와 out이 이름 그대로 보이는 자리에서만 쓴다. */
#define REJECT(code)                                                       \
    match_reject(eng, req->ts, req->id, req->market, req->price, req->qty, \
                 (code), out)



/*
 * IOC / FOK (docs/SPEC.md 4.2).
 *
 * 둘 다 지정가처럼 가격을 가지고 들어오되 호가창에 남지 않는다. 차이는 부분 체결을
 * 받아들이느냐다 — IOC는 받고, FOK는 전량 아니면 아무것도 아니다.
 */

/* 가격 검증. 둘 다 지정가와 같은 기준을 쓴다. */
static int check_price(const match_engine_t *eng, const order_t *req,
                       exec_result_t *out)
{
    if (req->price < book_price_low(eng->book) ||
        req->price > book_price_high(eng->book)) {
        return REJECT(ERR_PRICE_LIMIT);
    }
    if (!is_valid_tick_in(book_tick_table(eng->book), req->price)) {
        return REJECT(ERR_INVALID_TICK);
    }
    return ERR_OK;
}

int match_ioc(match_engine_t *eng, const order_t *req, exec_result_t *out)
{
    if (eng == NULL || req == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    match_result_init(out, req->qty);

    int gate = match_gate_submit(eng, req->ts, req->type);
    if (gate != ERR_OK) {
        return REJECT(gate);
    }

    int rc = check_price(eng, req, out);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = match_validate(eng, req);
    if (rc != ERR_OK) {
        return REJECT(rc);
    }

    qty_t remaining = match_sweep(eng, req, req->price, out);

    out->remaining_qty = remaining;
    out->resting = false; /* 잔량은 등록하지 않고 취소한다 */

    if (out->filled_qty == 0) {
        /* 한 건도 못 붙었다. 시장가와 같은 이유로 거부로 알린다. */
        return REJECT(ERR_NO_LIQUIDITY);
    }
    out->status = (remaining == 0) ? STATUS_FILLED : STATUS_PARTIAL;

    if (remaining > 0) {
        match_emit(eng, EVENT_CANCELED, req->ts, req->id, req->market,
                   req->price, remaining, 0, ORDER_ID_INVALID, ERR_OK);
    }

    return ERR_OK;
}

int match_fok(match_engine_t *eng, const order_t *req, exec_result_t *out)
{
    if (eng == NULL || req == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    match_result_init(out, req->qty);

    int gate = match_gate_submit(eng, req->ts, req->type);
    if (gate != ERR_OK) {
        return REJECT(gate);
    }

    int rc = check_price(eng, req, out);
    if (rc != ERR_OK) {
        return rc;
    }
    rc = match_validate(eng, req);
    if (rc != ERR_OK) {
        return REJECT(rc);
    }

    /*
     * 먼저 세어 본다. 부분 체결 후 되돌리는 방식은 쓰지 않는다 —
     * 되돌리려면 이미 뗀 상대 주문을 제자리에 다시 넣어야 하는데,
     * 그러면 그 주문들의 시간 우선순위가 복원되지 않는다.
     */
    side_t maker_side = (req->side == SIDE_BUY) ? SIDE_SELL : SIDE_BUY;
    qty_t available =
        book_qty_up_to(eng->book, maker_side, req->price, req->qty);

    if (available < req->qty) {
        return REJECT(ERR_NO_LIQUIDITY);
    }

    qty_t remaining = match_sweep(eng, req, req->price, out);
    /* 세어 본 만큼은 반드시 붙는다. 안 붙었다면 세는 쪽과 먹는 쪽이 어긋난 것이다. */
    assert(remaining == 0);

    out->remaining_qty = remaining;
    out->resting = false;
    out->status = STATUS_FILLED;

    return ERR_OK;
}
