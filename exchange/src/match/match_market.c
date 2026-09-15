#include <stddef.h>

#include "errors.h"
#include "match_internal.h"

/*
 * 시장가 매칭 (docs/SPEC.md 4.2).
 *
 * 가격 제한 없이 반대 호가를 최우선부터 소진한다. 지정가와 다른 점은 둘이다.
 *  - 가격 검증이 없다. req->price를 아예 보지 않는다
 *  - 잔량을 호가창에 등록하지 않는다. 미체결분은 취소된다
 *
 * 반대 호가가 전혀 없으면 아무것도 하지 않고 거부한다. 체결도 등록도 못 하는
 * 주문을 받아 두면 호출부가 "접수됐다"고 오해한다.
 */
int match_market(match_engine_t *eng, const order_t *req, exec_result_t *out)
{
    if (eng == NULL || req == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    match_result_init(out, req->qty);

    int rc = match_validate(eng, req);
    if (rc != ERR_OK) {
        out->status = STATUS_REJECTED;
        return rc;
    }

    price_t best = (req->side == SIDE_BUY) ? book_best_ask(eng->book)
                                           : book_best_bid(eng->book);
    if (best == BOOK_PRICE_NONE) {
        out->status = STATUS_REJECTED;
        return ERR_NO_LIQUIDITY;
    }

    /* BOOK_PRICE_NONE을 limit으로 넘기면 가격 제한 없이 소진한다. */
    qty_t remaining = match_sweep(eng, req, BOOK_PRICE_NONE, out);

    out->remaining_qty = remaining;
    out->resting = false; /* 잔량은 등록하지 않고 취소한다 */
    out->status = (remaining == 0) ? STATUS_FILLED : STATUS_PARTIAL;

    return ERR_OK;
}
