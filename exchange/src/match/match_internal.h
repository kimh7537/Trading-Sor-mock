#ifndef MINI_SOR_MATCH_INTERNAL_H
#define MINI_SOR_MATCH_INTERNAL_H

/*
 * 매칭 엔진 내부. 이 헤더는 exchange/src/match/ 안에서만 쓴다.
 * 공개 API는 exchange/include/match.h다.
 */

#include "match.h"
#include "order_index.h"

struct match_engine {
    order_book_t *book;
    order_pool_t *pool;
    order_index_t *index;
    int32_t capacity;
    event_sink_t sink;
};

/* 이벤트 하나를 내보낸다. 싱크가 없으면 아무 일도 하지 않는다. */
void match_emit(const match_engine_t *eng, event_type_t type, ts_t ts,
                order_id_t order_id, market_t market, price_t price, qty_t qty,
                qty_t remaining, order_id_t counterparty, int reason);

/*
 * 거부를 한 곳에서 처리한다. 결과 상태를 REJECTED로 놓고 REJECTED 이벤트를 내보낸 뒤
 * 받은 에러 코드를 그대로 돌려준다. 거부 경로가 여럿이라 빠뜨리기 쉬워 모아 뒀다.
 */
int match_reject(const match_engine_t *eng, ts_t ts, order_id_t id,
                 market_t market, price_t price, qty_t qty, int rc,
                 exec_result_t *out);

/* 결과 구조체를 빈 상태로 되돌린다. */
void match_result_init(exec_result_t *out, qty_t order_qty);

/*
 * 상대 호가를 소진한다.
 *
 * limit은 체결을 멈추는 가격이다 — 매수면 이 값보다 비싼 매도호가를 건드리지 않고,
 * 매도면 이 값보다 싼 매수호가를 건드리지 않는다. BOOK_PRICE_NONE을 넘기면
 * 가격 제한 없이 소진한다(시장가).
 *
 * taker는 보관하지 않는다 — 읽기만 하고 체결분을 out에 쌓는다.
 * 전량 체결된 상대 주문은 호가창·인덱스에서 빠지고 슬롯이 풀로 돌아간다.
 * 반환값은 체결되지 않고 남은 수량이다.
 */
qty_t match_sweep(match_engine_t *eng, const order_t *taker, price_t limit,
                  exec_result_t *out);

/* 잔량을 호가창에 등록한다. 성공하면 ERR_OK. */
int match_rest(match_engine_t *eng, const order_t *req, qty_t remaining,
               exec_result_t *out);

/* 접수 전 공통 검증. 호가창을 건드리기 전에 부른다. */
int match_validate(const match_engine_t *eng, const order_t *req);

#endif /* MINI_SOR_MATCH_INTERNAL_H */
