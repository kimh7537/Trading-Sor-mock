#include <stddef.h>

#include "errors.h"
#include "match_internal.h"
#include "tick_size.h"

/*
 * 지정가 매칭 (docs/SPEC.md 4.2).
 *
 * 매수는 최우선매도호가가 지정가 이하인 동안 체결하고, 매도는 반대다.
 * 체결 가격은 지정가가 아니라 먼저 호가창에 있던 주문의 호가다 — 지정가는
 * "이보다 불리하게는 안 사겠다"는 상한일 뿐이다.
 */
int match_limit(match_engine_t *eng, const order_t *req, exec_result_t *out)
{
    if (eng == NULL || req == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    match_result_init(out, req->qty);

    /*
     * 잔량이 호가창에 등록될 수 있으므로 가격이 제한폭과 호가 단위를 만족해야 한다.
     * 호가창을 건드리기 전에 본다 — 거부될 주문이 반쯤 체결되고 나서 실패하면 안 된다.
     */
    if (req->price < book_price_low(eng->book) ||
        req->price > book_price_high(eng->book)) {
        out->status = STATUS_REJECTED;
        return ERR_PRICE_LIMIT;
    }
    if (!is_valid_tick(req->price)) {
        out->status = STATUS_REJECTED;
        return ERR_INVALID_TICK;
    }

    int rc = match_validate(eng, req);
    if (rc != ERR_OK) {
        out->status = STATUS_REJECTED;
        return rc;
    }

    qty_t remaining = match_sweep(eng, req, req->price, out);
    out->remaining_qty = remaining;

    if (remaining > 0) {
        rc = match_rest(eng, req, remaining, out);
        if (rc != ERR_OK) {
            /*
             * 체결분은 이미 일어난 일이라 되돌리지 않는다. 잔량만 등록되지 못했다.
             * out에 담긴 체결 목록은 그대로 유효하다.
             */
            out->status = (out->filled_qty > 0) ? STATUS_PARTIAL : STATUS_REJECTED;
            return rc;
        }
        out->status = (out->filled_qty > 0) ? STATUS_PARTIAL : STATUS_NEW;
    } else {
        out->status = STATUS_FILLED;
    }

    return ERR_OK;
}
