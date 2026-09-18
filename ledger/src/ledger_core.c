#include "ledger_core.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "account.h"
#include "consolidated.h"
#include "errors.h"
#include "event.h"
#include "executor.h"
#include "market_rules.h"
#include "match.h"
#include "msg.h"
#include "order_map.h"
#include "order_validate.h"
#include "routing_log.h"
#include "shm_segment.h"
#include "strategy.h"
#include "synthetic.h"

/*
 * 사용자 논리 주문번호의 시작값.
 *
 * 물리 번호는 `논리 x 16 + 시장 + 1`이다(T2-09). 유동성 주문번호(KRX 1~, NXT 10억~)와
 * 겹치지 않도록 2억에서 시작한다 — 전략 비교 하네스가 쓰는 값과 같은 이유다
 * (`COMPARE_LOGICAL_ID_BASE`).
 */
#define LEDGER_LOGICAL_BASE ((order_id_t)200000000)

const ledger_core_config_t LEDGER_CORE_DEFAULT = {
    .account = "123456789012",
    .cash = 100000000, /* 1억 원 */
    .symbol = "005930",
    .ref_price = 70000,
    .scenario = SCENARIO_BALANCED,
    .seed = 20260917,
    .liquidity_per_market = 1000,
    .order_capacity = 65536,
};

struct ledger_core {
    ledger_core_config_t cfg;

    shm_segment_t    *seg;
    account_store_t   store;
    validate_config_t vcfg;

    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
    venues_t        venues;
    order_map_t    *map;

    /* 논리 주문번호 - 시작값 -> 그 주문을 낸 계좌 자리. 정산할 때 쓴다 */
    int32_t *acct_of;
    /* 같은 자리 -> 주문을 낸 쪽이 붙인 번호. 조회 응답에 돌려준다 */
    uint64_t  *cl_of;
    /* 같은 자리 -> 주문할 때 고른 시장(0, 1, MSG_MARKET_AUTO). 상세 응답에 돌려준다 */
    uint8_t   *market_of;
    order_id_t next_logical;

    /*
     * 원장의 논리 시각. **시스템 시각을 읽지 않는다.** 전문을 처리할 때마다 1씩
     * 올리므로 같은 전문 순서는 같은 시각 순서를 만든다(결정성).
     *
     * 지금은 **이 값을 읽는 곳이 없다** — 세션 규칙을 걸지 않았고 매칭 엔진은 도착
     * 순서로 시간 우선을 정한다. 그래서 늘리기를 빼도 테스트가 통과한다(변이 L18).
     * 남기는 이유는 주문의 시각이 데이터 모델의 일부이기 때문이다. 세션 규칙을 거는
     * 순간 이 값이 장 시간 판정에 쓰인다.
     */
    ts_t clock;

    /*
     * 지금 집행 중인 논리 주문. 체결 이벤트가 이 주문의 것이면 taker이고,
     * 매핑 반영은 집행기가 한다. 아니면 호가창에 있던 maker이므로 여기서 반영한다.
     */
    order_id_t submitting;

    /*
     * 가상 참가자(T8-01). 시드 유동성을 만든 그 생성기를 그대로 들고 있다가
     * `ledger_core_tick()`에서 이어 뽑는다. 유동성 0이면 NULL이다.
     *
     * `synth_first`는 시장별 첫 가상 주문번호다. 생성기가 번호를 1씩 올리므로
     * `synth_first + retired`가 곧 "가장 오래된, 아직 걷지 않은 주문"이다 —
     * 번호를 따로 배열에 쌓아 둘 필요가 없다.
     */
    divergent_t *div;
    order_id_t   synth_first[MARKET_COUNT];
    int64_t      synth_issued[MARKET_COUNT];
    int64_t      synth_retired[MARKET_COUNT];
};

/* --- 정산 --- */

static void settle_fill(ledger_core_t *c, const logical_order_t *lo,
                        price_t price, qty_t qty)
{
    order_id_t off = lo->logical_id - LEDGER_LOGICAL_BASE;
    assert(off < (order_id_t)c->cfg.order_capacity);
    int32_t acct = c->acct_of[off];

    int64_t amount = (int64_t)price * (int64_t)qty;
    int     rc;

    if (lo->side == SIDE_BUY) {
        rc = acct_settle(&c->store, acct, amount);
        assert(rc == ERR_OK); /* 묶어 둔 것보다 많이 풀면 원장 계산이 틀린 것이다 */

        int64_t improvement = ((int64_t)lo->limit_price - price) * (int64_t)qty;
        assert(improvement >= 0); /* 매수는 지정가보다 비싸게 체결되지 않는다 */
        if (improvement > 0) {
            rc = acct_release(&c->store, acct, improvement);
            assert(rc == ERR_OK);
        }
    } else {
        rc = acct_deposit(&c->store, acct, amount);
        assert(rc == ERR_OK);
    }
    (void)rc;
}

/*
 * 매칭 엔진의 체결 이벤트. **돈을 옮기는 곳은 여기 하나다.**
 * 이 콜백 안에서 매칭 엔진을 다시 부르지 않는다(event.h의 재진입 금지).
 */
static void on_event(const order_event_t *ev, void *ctx)
{
    ledger_core_t *c = ctx;

    if (ev->type != EVENT_EXECUTED && ev->type != EVENT_PARTIALLY_EXECUTED) {
        return;
    }
    if (omap_leg(c->map, ev->order_id) == NULL) {
        return; /* 미리 넣어 둔 유동성 주문이다. 원장 계좌의 주문이 아니다 */
    }

    const logical_order_t *lo = omap_get_by_phys(c->map, ev->order_id);
    assert(lo != NULL);

    if (lo->logical_id != c->submitting) {
        /* 예전에 걸어 둔 주문이 지금 체결됐다(maker). 매핑에도 반영한다 */
        int rc = omap_on_fill(c->map, ev->order_id, ev->qty, ev->price);
        assert(rc == ERR_OK);
        (void)rc;
    }

    settle_fill(c, lo, ev->price, ev->qty);
}

/* 매수라면 `지정가 x qty`만큼 묶음을 푼다. 매도는 묶은 것이 없다. */
static void release_unused(ledger_core_t *c, const msg_order_req_t *req,
                           int32_t acct, qty_t qty)
{
    if (req->side != SIDE_BUY || qty <= 0) {
        return;
    }
    int rc = acct_release(&c->store, acct, (int64_t)req->price * (int64_t)qty);
    assert(rc == ERR_OK);
    (void)rc;
}

/* --- 주문 --- */

static void process_order(ledger_core_t *c, const msg_order_req_t *req,
                          msg_order_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));
    ack->cl_ord_id = req->cl_ord_id;
    ack->status = STATUS_REJECTED;
    ack->price = req->price;

    bool automatic = (req->market == MSG_MARKET_AUTO);

    /*
     * 없는 시장 값은 따로 보지 않는다. 아래 `plan_add_leg()`가 이미 거절하고, 그때
     * 묶어 둔 증거금은 배분 실패 경로가 푼다. 처음엔 여기서 먼저 걸렀는데 변이 검사로
     * 보니 **두 검사가 서로를 가려** 배분 실패 경로의 해제가 한 번도 실행되지 않았다.
     * 대신 막아 주는 쪽이 우리 코드이므로 앞의 것을 지웠다(이 프로젝트의 규칙).
     */

    /*
     * **자리 검사는 지우면 안 된다.** 아래에서 `acct_of[번호 - 시작값]`에 쓰는데,
     * 이 검사가 없으면 배열 밖에 쓴다. 매핑 등록도 자리가 없어 실패하므로 응답 코드는
     * 같게 나와 일반 테스트로는 안 보인다 — ASan 빌드가 잡는다(변이 L13으로 확인).
     */
    if (c->next_logical - LEDGER_LOGICAL_BASE >=
        (order_id_t)c->cfg.order_capacity) {
        ack->reason = ERR_POOL_EXHAUSTED;
        return;
    }

    validate_result_t v;
    if (validate_order(&c->store, &c->vcfg, req, &v) != ERR_OK) {
        ack->reason = v.reason;
        return;
    }
    /* 여기부터 매수라면 `지정가 x 수량`이 묶여 있다. 어느 길로 나가든 정리한다 */

    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = c->next_logical++;
    o.side = (side_t)req->side;
    o.type = (order_type_t)req->type;
    o.price = req->price;
    o.qty = req->qty;
    o.ts = ++c->clock;
    o.market = automatic ? MARKET_KRX : (market_t)req->market;

    exec_plan_t plan;
    int         rc;
    if (automatic) {
        exec_context_t ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.cons = &c->cons;
        ctx.ts = o.ts;
        rc = routing_plan(&STRATEGY_BEST_PRICE, &ctx, &o, NULL, &plan);
    } else {
        plan_init(&plan);
        rc = plan_add_leg(&plan, o.market, o.qty, o.price, o.type);
    }
    if (rc != ERR_OK) {
        release_unused(c, req, v.account_index, req->qty);
        ack->reason = rc;
        return;
    }

    c->acct_of[o.id - LEDGER_LOGICAL_BASE] = v.account_index;
    c->cl_of[o.id - LEDGER_LOGICAL_BASE] = req->cl_ord_id;
    c->market_of[o.id - LEDGER_LOGICAL_BASE] = req->market;

    exec_report_t rep;
    c->submitting = o.id;
    rc = exec_submit(c->map, &c->venues, &o, &plan, &rep);
    c->submitting = ORDER_ID_INVALID;

    if (rc != ERR_OK) {
        /* 한 다리도 접수되지 않았다 — 체결도 없다(executor.h) */
        release_unused(c, req, v.account_index, req->qty);
        ack->order_id = o.id;
        ack->reason = rc;
        return;
    }

    /*
     * **체결되지 않을 수량의 증거금을 푼다.** 체결분은 콜백이 이미 정산했고, 남아야
     * 할 묶음은 호가창에 살아 있는 수량분뿐이다(헤더의 불변식 설명).
     */
    qty_t never = rep.order_qty - rep.filled_qty - rep.working_qty;
    assert(never >= 0);
    release_unused(c, req, v.account_index, never);

    ack->order_id = o.id;
    ack->status = (uint8_t)rep.status;
    ack->reason = ERR_OK;
    ack->filled_qty = rep.filled_qty;
    ack->price = (rep.filled_qty > 0)
                     ? (price_t)(rep.notional / (int64_t)rep.filled_qty)
                     : req->price;
}

/*
 * 주문 하나의 지금 상태. **호가창에 걸어 뒀다가 나중에 체결된 것까지** 반영된다 —
 * 그 체결은 콜백이 매핑에 적는다.
 *
 * 주문번호 0(전체 조회)은 응답을 여러 전문으로 나눠 보내야 해서 아직 받지 않는다.
 * 모르는 번호와 함께 `STATUS_REJECTED`, 수량 0으로 답한다. 조회 응답에는 사유 필드가
 * 없어서 "없음"을 이렇게 표시한다.
 */
static void query_order(ledger_core_t *c, const msg_query_req_t *req,
                        msg_query_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));
    ack->order_id = req->order_id;
    ack->status = STATUS_REJECTED;
    ack->last = 1;
    snprintf(ack->symbol, sizeof(ack->symbol), "%.*s",
             (int)(sizeof(ack->symbol) - 1), c->cfg.symbol);

    if (req->order_id < LEDGER_LOGICAL_BASE ||
        req->order_id - LEDGER_LOGICAL_BASE >=
            (order_id_t)c->cfg.order_capacity) {
        return;
    }
    const logical_order_t *lo = omap_get(c->map, req->order_id);
    if (lo == NULL) {
        return;
    }

    order_status_t st;
    if (exec_status(c->map, req->order_id, &st) != ERR_OK) {
        return;
    }
    ack->cl_ord_id = c->cl_of[req->order_id - LEDGER_LOGICAL_BASE];
    ack->status = (uint8_t)st;
    ack->price = lo->limit_price;
    ack->qty = lo->order_qty;
    ack->filled_qty = omap_filled_qty(c->map, req->order_id);
}

/*
 * 한 시장의 호가 10단(T6-04).
 *
 * 없는 시장이나 다루지 않는 종목이면 **빈 호가창**으로 답한다. 조회 응답에는 사유
 * 필드가 없고, 화면 입장에서 "그 종목의 호가가 없다"와 뜻이 같다. 시장 값은 채널계가
 * 경계에서 이미 거른다.
 */
static void query_book(ledger_core_t *c, const msg_book_req_t *req,
                       msg_book_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));
    memcpy(ack->symbol, req->symbol, sizeof(ack->symbol));
    ack->market = req->market;

    if (req->market >= MARKET_COUNT ||
        strncmp(req->symbol, c->cfg.symbol, MSG_SYMBOL_LEN) != 0) {
        return;
    }
    const order_book_t *book = match_book(c->eng[req->market]);

    level_view_t view[MSG_BOOK_DEPTH];
    int n = book_snapshot(book, SIDE_BUY, MSG_BOOK_DEPTH, view);
    assert(n >= 0);
    for (int i = 0; i < n; i++) {
        ack->bid_price[i] = view[i].price;
        ack->bid_qty[i] = view[i].total_qty;
    }
    n = book_snapshot(book, SIDE_SELL, MSG_BOOK_DEPTH, view);
    assert(n >= 0);
    for (int i = 0; i < n; i++) {
        ack->ask_price[i] = view[i].price;
        ack->ask_qty[i] = view[i].total_qty;
    }
}

/*
 * 이 계좌가 낸 주문이면 그 논리 주문을, 아니면 NULL(T7-01).
 *
 * **남의 주문은 "없다"로 답한다.** "당신 것이 아니다"라고 답하면 주문번호를 하나씩
 * 넣어 보며 남의 주문이 있는지 알아낼 수 있다.
 */
static const logical_order_t *owned_order(ledger_core_t *c, const char *account,
                                          order_id_t id)
{
    if (id < LEDGER_LOGICAL_BASE || id >= c->next_logical) {
        return NULL;
    }
    int acct = acct_find(&c->store, account);
    if (acct < 0 || c->acct_of[id - LEDGER_LOGICAL_BASE] != acct) {
        return NULL;
    }
    return omap_get(c->map, id);
}

/*
 * 살아 있는 물리 주문을 모두 취소한다(T7-01).
 *
 * **매수라면 취소된 수량 x 지정가만큼 묶음을 푼다.** 묶음은 "지정가 x 아직 호가창에 살아
 * 있는 수량"이어야 하고(헤더의 불변식), 취소는 살아 있는 수량을 줄이기 때문이다.
 * 일부 체결 뒤에 취소해도 같은 식이 맞는다 — 체결분은 콜백이 이미 풀었다.
 *
 * 살아 있는 것이 없으면(이미 체결·취소로 끝남) `exec_cancel`이 ERR_NOT_FOUND를 준다.
 */
static void cancel_order(ledger_core_t *c, const msg_cancel_req_t *req,
                         msg_cancel_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));
    ack->order_id = req->order_id;
    ack->cl_ord_id = req->cl_ord_id;
    ack->status = STATUS_REJECTED;
    ack->reason = ERR_NOT_FOUND;

    const logical_order_t *lo = owned_order(c, req->account, req->order_id);
    if (lo == NULL) {
        return;
    }

    cancel_report_t rep;
    int rc = exec_cancel(c->map, &c->venues, req->order_id, ++c->clock, &rep);

    if (rep.canceled_qty > 0 && lo->side == SIDE_BUY) {
        int32_t acct = c->acct_of[req->order_id - LEDGER_LOGICAL_BASE];
        int     rrc = acct_release(&c->store, acct,
                                   (int64_t)lo->limit_price * rep.canceled_qty);
        assert(rrc == ERR_OK); /* 살아 있던 수량만큼은 반드시 묶여 있었다 */
        (void)rrc;
    }

    ack->reason = rc;
    if (rc == ERR_NOT_FOUND && rep.canceled_qty == 0) {
        return; /* 취소할 것이 없었다 */
    }
    /*
     * 한쪽만 취소되고 한쪽이 실패해도(집행기는 되돌리지 않는다, executor.h) 취소된
     * 수량과 지금 상태를 그대로 알린다. 사유 칸이 실패를 말한다.
     * ponytail: 한 프로세스 안에서는 매칭 엔진 취소가 실패할 경로가 없어 시험하지 못한다.
     */
    ack->status = (uint8_t)rep.status;
    ack->canceled_qty = rep.canceled_qty;
}

/*
 * 주문 하나의 상세(T7-02). 다리를 시장별로 더해 싣는다 — 화면의 "논리 → 물리"가 이것이다.
 * 남의 주문·없는 주문은 취소와 같은 규칙으로 "없음"이다.
 */
static void detail_order(ledger_core_t *c, const msg_detail_req_t *req,
                         msg_detail_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));
    ack->order_id = req->order_id;
    ack->status = STATUS_REJECTED;
    ack->reason = ERR_NOT_FOUND;

    const logical_order_t *lo = owned_order(c, req->account, req->order_id);
    if (lo == NULL) {
        return;
    }
    order_id_t     off = req->order_id - LEDGER_LOGICAL_BASE;
    order_status_t st;
    int            rc = exec_status(c->map, req->order_id, &st);
    assert(rc == ERR_OK); /* owned_order가 찾은 주문이다 */
    (void)rc;

    ack->reason = ERR_OK;
    ack->cl_ord_id = c->cl_of[off];
    ack->side = (uint8_t)lo->side;
    ack->status = (uint8_t)st;
    ack->market = c->market_of[off];
    ack->price = lo->limit_price;
    ack->qty = lo->order_qty;
    ack->filled = omap_filled_qty(c->map, req->order_id);
    ack->canceled = omap_canceled_qty(c->map, req->order_id);
    ack->working = omap_remaining(c->map, req->order_id);
    ack->notional = omap_notional(c->map, req->order_id);

    for (int32_t i = 0; i < lo->leg_count; i++) {
        const phys_leg_t *leg = &lo->legs[i];
        assert(leg->market >= 0 && leg->market < MSG_LEG_SLOTS);
        ack->leg_sent[leg->market] += leg->sent_qty;
        ack->leg_filled[leg->market] += leg->filled_qty;
        ack->leg_canceled[leg->market] += leg->canceled_qty;
        ack->leg_notional[leg->market] += leg->notional;
    }
}

/*
 * 계좌 잔고(T7-02). 없는 계좌면 ERR_NOT_FOUND와 금액 0 — `ledger_core_balance`는 없는 계좌에
 * 출력을 건드리지 않으므로 앞의 memset이 0을 보장한다(따로 지우던 줄은 변이 검사로 중복임을 확인해 뺐다).
 */
static void balance_of(ledger_core_t *c, const msg_balance_req_t *req,
                       msg_balance_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));
    memcpy(ack->account, req->account, sizeof(ack->account));
    ack->reason = ledger_core_balance(c, req->account, &ack->cash, &ack->reserved);
}

/* --- 전문 --- */

int ledger_core_handle(const wire_header_t *hdr, const uint8_t *body,
                       uint8_t *out, size_t out_cap, void *ctx)
{
    ledger_core_t *c = ctx;
    if (hdr == NULL || out == NULL || c == NULL) {
        return -1;
    }

    msg_type_t reply = msg_reply_type(hdr->type);
    if (reply == MSG_UNKNOWN) {
        return 0; /* 응답이 없는 종별 */
    }

    /*
     * 응답의 시퀀스·시각은 요청이 들고 온 것을 그대로 쓴다. 원장이 자기 시각을
     * 만들어 넣으면 같은 요청에 다른 응답 바이트가 나온다.
     */
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = (uint8_t)reply;
    h.body_len = (uint32_t)msg_body_len((uint8_t)reply);
    h.seq = hdr->seq;
    h.ts = hdr->ts;

    int n = wire_encode_header(&h, out, out_cap);
    if (n < 0) {
        return -1;
    }
    uint8_t *b = out + n;
    size_t   cap = out_cap - (size_t)n;
    int      m = -1;

    switch (hdr->type) {
    case MSG_ORDER_REQ: {
        msg_order_req_t req;
        if (msg_decode_order_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_order_ack_t ack;
        process_order(c, &req, &ack);
        m = msg_encode_order_ack(&ack, b, cap);
        break;
    }
    case MSG_CANCEL_REQ: {
        msg_cancel_req_t req;
        if (msg_decode_cancel_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_cancel_ack_t ack;
        cancel_order(c, &req, &ack);
        m = msg_encode_cancel_ack(&ack, b, cap);
        break;
    }
    case MSG_MODIFY_REQ: {
        msg_modify_req_t req;
        if (msg_decode_modify_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_modify_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.order_id = req.order_id;
        ack.cl_ord_id = req.cl_ord_id;
        ack.status = STATUS_REJECTED;
        ack.reason = ERR_NOT_SUPPORTED;
        m = msg_encode_modify_ack(&ack, b, cap);
        break;
    }
    case MSG_QUERY_REQ: {
        msg_query_req_t req;
        if (msg_decode_query_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_query_ack_t ack;
        query_order(c, &req, &ack);
        m = msg_encode_query_ack(&ack, b, cap);
        break;
    }
    case MSG_DETAIL_REQ: {
        msg_detail_req_t req;
        if (msg_decode_detail_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_detail_ack_t ack;
        detail_order(c, &req, &ack);
        m = msg_encode_detail_ack(&ack, b, cap);
        break;
    }
    case MSG_BALANCE_REQ: {
        msg_balance_req_t req;
        if (msg_decode_balance_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_balance_ack_t ack;
        balance_of(c, &req, &ack);
        m = msg_encode_balance_ack(&ack, b, cap);
        break;
    }
    case MSG_BOOK_REQ: {
        msg_book_req_t req;
        if (msg_decode_book_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_book_ack_t ack;
        query_book(c, &req, &ack);
        m = msg_encode_book_ack(&ack, b, cap);
        break;
    }
    default:
        return 0;
    }

    if (m < 0) {
        return -1;
    }
    return n + m;
}

/* --- 만들고 부수기 --- */

static int seed_liquidity(ledger_core_t *c)
{
    const ledger_core_config_t *cfg = &c->cfg;

    divergent_config_t d;
    memset(&d, 0, sizeof(d));
    d.scenario = cfg->scenario;
    d.seed = cfg->seed;
    d.ref_price = cfg->ref_price;
    /* 가격 제한폭 ±30%(docs/SPEC.md 3.1) 안에서 만든다 */
    d.price_low = cfg->ref_price - cfg->ref_price * 3 / 10;
    d.price_high = cfg->ref_price + cfg->ref_price * 3 / 10;
    d.start_ts = TOD_NS(9, 0, 0);
    d.orders_per_market = cfg->liquidity_per_market;

    /*
     * 유동성이 0이면 생성기를 만들지 않는다 — 생성기는 0건 설정을 거절한다.
     * 빈 호가창은 테스트가 체결 상대를 직접 만들 때 쓴다.
     */
    divergent_t *div = NULL;
    if (cfg->liquidity_per_market > 0) {
        div = divergent_create(&d);
        if (div == NULL) {
            return ERR_INVALID_ARG;
        }
    }

    int32_t cap = cfg->liquidity_per_market +
                  cfg->order_capacity * PLAN_LEGS_MAX + 64;

    cons_init(&c->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        price_t base = (div != NULL) ? divergent_ref_price(div, (market_t)m)
                                     : cfg->ref_price;
        c->eng[m] = match_engine_create(base, cap);
        if (c->eng[m] == NULL) {
            divergent_destroy(div);
            return ERR_POOL_EXHAUSTED;
        }
        /*
         * 세션 규칙은 걸지 않는다. 걸면 화면을 켠 시각(실제 시계)이 아니라 논리
         * 시각으로 장이 열리고 닫히는데, 데모에서 그 둘이 어긋나면 "장이 닫혀서
         * 거절"이 이유 없이 보인다. 세션 규칙은 T1-13~15가 따로 검증한다.
         */
        if (cons_attach(&c->cons, (market_t)m, match_book(c->eng[m]), NULL) !=
            ERR_OK) {
            divergent_destroy(div);
            return ERR_INVALID_ARG;
        }
        c->venues.eng[m] = c->eng[m];

        if (div == NULL) {
            continue;
        }
        synth_gen_t *gen = divergent_gen(div, (market_t)m);
        for (int32_t i = 0; i < cfg->liquidity_per_market; i++) {
            order_t       o;
            exec_result_t res;
            if (synth_next(gen, &o) != ERR_OK) {
                break;
            }
            o.market = (market_t)m;
            if (i == 0) {
                c->synth_first[m] = o.id;
            }
            c->synth_issued[m]++;
            (void)match_limit(c->eng[m], &o, &res);
            if (o.ts > c->clock) {
                c->clock = o.ts; /* 사용자 주문은 유동성보다 뒤에 온 것으로 둔다 */
            }
        }
    }
    /*
     * **생성기를 살려 둔다**(T8-01). 실시세 모드에서 여기서부터 이어 뽑아야
     * 호가창이 계속 움직인다. 틱을 부르지 않으면 이 뒤로 아무 일도 없다.
     */
    c->div = div;

    /*
     * **유동성을 다 넣은 뒤에 콜백을 건다.** 먼저 걸어도 유동성끼리의 체결은
     * 사용자 주문이 아니라 걸러지지만, 준비 단계와 운영 단계를 섞지 않는다.
     */
    event_sink_t sink = {.fn = on_event, .ctx = c};
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_set_sink(c->eng[m], &sink);
    }
    return ERR_OK;
}

ledger_core_t *ledger_core_create(const ledger_core_config_t *cfg)
{
    if (cfg == NULL) {
        cfg = &LEDGER_CORE_DEFAULT;
    }
    if (cfg->account == NULL || cfg->symbol == NULL || cfg->cash < 0 ||
        cfg->ref_price <= 0 || cfg->liquidity_per_market < 0 ||
        cfg->order_capacity <= 0) {
        return NULL;
    }

    ledger_core_t *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->cfg = *cfg;
    c->next_logical = LEDGER_LOGICAL_BASE;
    c->submitting = ORDER_ID_INVALID;

    const int32_t rec_count[SHM_REGION_COUNT] = {4, 4};
    const size_t  rec_size[SHM_REGION_COUNT] = {sizeof(account_t), 64};
    c->seg = shm_create(rec_count, rec_size);
    if (c->seg == NULL || acct_store_init(&c->store, c->seg) != ERR_OK) {
        ledger_core_destroy(c);
        return NULL;
    }

    int acct = acct_open(&c->store, cfg->account);
    if (acct < 0 ||
        (cfg->cash > 0 && acct_deposit(&c->store, acct, cfg->cash) != ERR_OK)) {
        ledger_core_destroy(c);
        return NULL;
    }

    vcfg_init(&c->vcfg);
    if (vcfg_add_symbol(&c->vcfg, cfg->symbol, MARGIN_BP_FULL, true) != ERR_OK) {
        ledger_core_destroy(c);
        return NULL;
    }

    c->map = omap_create(cfg->order_capacity);
    c->acct_of = calloc((size_t)cfg->order_capacity, sizeof(int32_t));
    c->cl_of = calloc((size_t)cfg->order_capacity, sizeof(uint64_t));
    c->market_of = calloc((size_t)cfg->order_capacity, sizeof(uint8_t));
    if (c->map == NULL || c->acct_of == NULL || c->cl_of == NULL || c->market_of == NULL ||
        seed_liquidity(c) != ERR_OK) {
        ledger_core_destroy(c);
        return NULL;
    }
    return c;
}

void ledger_core_destroy(ledger_core_t *c)
{
    if (c == NULL) {
        return;
    }
    divergent_destroy(c->div);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(c->eng[m]);
    }
    omap_destroy(c->map);
    free(c->acct_of);
    free(c->cl_of);
    free(c->market_of);
    if (c->seg != NULL) {
        acct_store_destroy(&c->store);
        shm_destroy(c->seg);
    }
    free(c);
}

int ledger_core_tick(ledger_core_t *c, int32_t n)
{
    if (c == NULL) {
        return ERR_NULL_PTR;
    }
    if (n <= 0) {
        return ERR_INVALID_ARG;
    }
    if (c->div == NULL) {
        return ERR_NOT_SUPPORTED; /* 유동성 0으로 만든 코어 — 생성기가 없다 */
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        synth_gen_t *gen = divergent_gen(c->div, (market_t)m);
        if (gen == NULL) {
            continue;
        }
        for (int32_t i = 0; i < n; i++) {
            order_t       o;
            exec_result_t res;
            if (synth_next(gen, &o) != ERR_OK) {
                break;
            }
            o.market = (market_t)m;
            c->synth_issued[m]++;

            /*
             * 살아 있는 가상 주문 수를 시장당 유동성 수로 묶는다. 넘으면 가장
             * 오래된 번호를 걷는다. 이미 체결돼 없어진 번호면 ERR_NOT_FOUND가
             * 오는데, 그것도 "더 이상 살아 있지 않다"는 뜻이라 그냥 넘긴다.
             */
            while (c->synth_issued[m] - c->synth_retired[m] >
                   c->cfg.liquidity_per_market) {
                exec_result_t cr;
                order_id_t    old_id =
                    c->synth_first[m] + (order_id_t)c->synth_retired[m];
                (void)match_cancel(c->eng[m], old_id, o.ts, &cr);
                c->synth_retired[m]++;
            }

            (void)match_limit(c->eng[m], &o, &res);
            if (o.ts > c->clock) {
                c->clock = o.ts;
            }
        }
    }
    return ERR_OK;
}

/* --- 보기 --- */

const order_book_t *ledger_core_book(const ledger_core_t *c, market_t market)
{
    if (c == NULL || market < 0 || market >= MARKET_COUNT) {
        return NULL;
    }
    return match_book(c->eng[market]);
}

int ledger_core_open_account(ledger_core_t *c, const char *account,
                             int64_t cash)
{
    if (c == NULL || account == NULL) {
        return ERR_NULL_PTR;
    }
    if (cash < 0) {
        return ERR_INVALID_ARG;
    }
    if (acct_find(&c->store, account) >= 0) {
        return ERR_DUPLICATE; /* 이미 있는 계좌에 몰래 입금하지 않는다 */
    }
    int idx = acct_open(&c->store, account);
    if (idx < 0) {
        return idx;
    }
    return (cash > 0) ? acct_deposit(&c->store, idx, cash) : ERR_OK;
}

int ledger_core_balance(ledger_core_t *c, const char *account,
                        int64_t *out_cash, int64_t *out_reserved)
{
    if (c == NULL || account == NULL || out_cash == NULL ||
        out_reserved == NULL) {
        return ERR_NULL_PTR;
    }
    int idx = acct_find(&c->store, account);
    if (idx < 0) {
        return ERR_NOT_FOUND;
    }
    return acct_snapshot(&c->store, idx, out_cash, out_reserved);
}
