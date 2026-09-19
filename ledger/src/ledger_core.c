#include "ledger_core.h"

#include <assert.h>
#include <stdbool.h>
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
#include "order.h"
#include "order_map.h"
#include "order_validate.h"
#include "routing_log.h"
#include "shm_segment.h"
#include "strategy.h"
#include "synthetic.h"
#include "tick_size.h"

/*
 * 사용자 논리 주문번호의 시작값.
 *
 * 물리 번호는 `논리 x 16 + 시장 + 1`이다(T2-09). 유동성 주문번호(KRX 1~, NXT 10억~)와
 * 겹치지 않도록 2억에서 시작한다 — 전략 비교 하네스가 쓰는 값과 같은 이유다
 * (`COMPARE_LOGICAL_ID_BASE`).
 */
#define LEDGER_LOGICAL_BASE ((order_id_t)200000000)

/*
 * 주입한 호가의 주문번호 시작값(T8-03).
 *
 * 유동성(KRX 1~, NXT 10억~)과 사용자 물리 번호(논리 2억 x 16 = 32억~) **사이의 빈
 * 구간**이다. 겹치면 취소가 엉뚱한 주문을 걷는다.
 */
#define LEDGER_FEED_ID_BASE ((order_id_t)2000000000)

/* 스냅샷을 맞출 때 훑는 단 수. 주입하는 10단보다 깊어질 수 있어 넉넉히 둔다. */
#define LEDGER_FEED_SCAN_DEPTH 32

/*
 * 가상 참가자의 기준가를 몇 틱마다 한 번 옮기는가(T8-08).
 *
 * **기준가를 고정해 두면 최우선호가가 붙박이가 된다.** 잔량만 출렁이고 가격은 한 번도
 * 움직이지 않는다 — 실측으로 확인했다(20초 동안 260,000 / 259,500 그대로). 그러면
 * "가격 차트"가 평평해서 시장이 죽은 것처럼 보인다.
 *
 * `ledgerd --live 40`이면 25ms마다 한 틱이므로 100틱은 약 2.5초다.
 *
 * **더 빨리 움직이면 호가가 못 따라온다.** 1초에 한 호가로 뒀더니 상승 구간에서 매도
 * 최우선이 위로 밀리며 스프레드가 5,000원까지 벌어졌다 — 기준가가 옮겨 간 자리를 새 주문이
 * 채우기 전에 다음 표류가 와서다. 유동성이 따라올 만큼 늦춘다. `--live` 값을 바꾸면 체감
 * 속도도 같이 바뀐다(틱 수로 세기 때문이다. 시스템 시각을 읽지 않는다).
 */
#define LEDGER_DRIFT_EVERY 100

/*
 * 방향을 몇 번에 한 번 뒤집는가.
 *
 * **매번 방향을 새로 뽑으면 제자리걸음만 한다.** 처음에 그렇게 짰더니 800틱을 돌려도
 * 최우선호가가 출발점으로 돌아왔다 — 랜덤워크는 평균이 0이라 추세가 생기지 않는다.
 * 방향을 이어 가다 가끔 뒤집으면 오르내리는 구간이 생겨 실제 시세처럼 보인다.
 */
#define LEDGER_DRIFT_FLIP 5

/*
 * 두 시장의 중심이 서로 몇 호가까지 벌어질 수 있는가.
 *
 * **시장마다 따로 표류시켰더니 3%까지 벌어졌다**(KRX 244,000 / NXT 252,000). 같은 종목을
 * 두 시장에서 거래하는데 그만큼 벌어질 수는 없다 — 실제로는 차익거래가 한두 호가 안으로
 * 묶는다. 그래서 **공통 중심 하나가 표류하고**, 각 시장은 그 둘레에서 이만큼만 어긋난다.
 * 어긋남이 0이면 두 시장이 늘 같아져 SOR이 고를 것이 없어진다.
 */
#define LEDGER_DRIFT_SPREAD 2



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
    /* 주입한 호가에 붙일 다음 번호(T8-03). 시장을 가리지 않고 하나로 센다 */
    order_id_t   next_feed_id;

    /*
     * 기준가 표류의 난수 상태와 틱 세기(T8-08). **시장마다 따로 흐른다** — 같이 움직이면
     * 두 시장의 가격 차이가 생기지 않아 SOR이 고를 것이 없다.
     *
     * 시드에서 파생하고 시스템 시각을 읽지 않는다. 같은 시드에 같은 틱 횟수면 같은 표류다.
     */
    uint64_t     drift_state[MARKET_COUNT];
    int64_t      drift_ticks;
    /* 공통 중심이 향하는 쪽(-1 또는 +1). 0이면 아직 안 정했다 */
    int8_t       drift_dir;
    /*
     * 시장별 마지막 체결가와 누적 체결 수량(T8-09). 봉(OHLCV)을 만들려면 체결이 필요한데
     * 호가만으로는 알 수 없다. 여기 모아 두고 호가 응답에 실어 보낸다.
     *
     * **한 체결을 한 번만 센다.** 매칭 엔진은 사는 쪽과 파는 쪽 양쪽에 이벤트를 주므로
     * 그대로 더하면 거래량이 두 배가 된다.
     */
    price_t      last_price[MARKET_COUNT];
    int64_t      traded_qty[MARKET_COUNT];

    /* 두 시장이 함께 따르는 중심. 0이면 아직 시작 전이다 */
    price_t      drift_base;
    /* 그 중심에서 시장마다 몇 호가 어긋나 있는가 */
    int8_t       drift_off[MARKET_COUNT];
    /*
     * 이 시장이 바깥 시세를 받고 있는가(T8-03).
     *
     * 받는 동안은 **가상 참가자가 그 시장에서 손을 뗀다** — 실호가 위에 가짜 주문을 계속
     * 얹으면 그건 실시세도 시뮬도 아니다.
     *
     * **스스로 풀지 않는다.** 스냅샷이 잠시 안 오는 것(장 마감)과 피드가 끝난 것은 겉으로
     * 같아서, 시간으로 어림하면 장 마감에 가상 참가자가 슬그머니 돌아와 "실시세인 척하는
     * 시뮬"이 된다 — 실제로 그렇게 됐다. 보내는 쪽이 `MSG_FEED_END`로 끝을 알린다.
     */
    bool         fed[MARKET_COUNT];
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

    /*
     * 체결 테이프(T8-09). **가상 참가자끼리의 체결도 센다** — 그것이 이 시장의 거래량이다.
     *
     * 한 체결에 이벤트가 둘(사는 쪽·파는 쪽) 오므로 **번호가 작은 쪽에서만** 센다. 그러지
     * 않으면 거래량이 정확히 두 배가 된다. 상대가 없는 이벤트는 체결이 아니므로 건너뛴다.
     */
    if (ev->market >= 0 && ev->market < MARKET_COUNT &&
        ev->counterparty_id != ORDER_ID_INVALID &&
        ev->order_id < ev->counterparty_id) {
        c->last_price[ev->market] = ev->price;
        c->traded_qty[ev->market] += ev->qty;
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
    ack->last_price = c->last_price[req->market];
    ack->traded_qty = c->traded_qty[req->market];

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
    /*
     * 스냅샷을 심고 **심은 뒤의 호가창**을 돌려준다(T8-03). 심다가 실패해도 응답은
     * 호가창이다 — 보낸 쪽이 "그래서 지금 어떻게 됐나"를 한 번에 본다.
     */
    case MSG_BOOK_FEED: {
        msg_book_feed_t feed;
        if (msg_decode_book_feed(body, hdr->body_len, &feed) < 0) {
            return -1;
        }
        (void)ledger_core_apply_feed(c, &feed);

        msg_book_req_t q;
        memset(&q, 0, sizeof(q));
        memcpy(q.symbol, feed.symbol, sizeof(q.symbol));
        q.market = feed.market;
        msg_book_ack_t ack;
        query_book(c, &q, &ack);
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
    c->next_feed_id = LEDGER_FEED_ID_BASE;
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        /* 시장마다 다른 흐름. 0이면 xorshift가 굳으므로 반드시 0이 아니게 한다 */
        c->drift_state[m] =
            (cfg->seed ^ (UINT64_C(0x9E3779B97F4A7C15) * (uint64_t)(m + 1))) |
            UINT64_C(1);
    }
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

/* xorshift64. 전역 `rand()`를 쓰지 않는다(CLAUDE.md 결정성). */
static uint64_t drift_next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *state = x;
    return x * UINT64_C(2685821657736338717);
}

/* 기준가에서 n호가 떨어진 유효 호가. 구간이 바뀌는 자리를 넘어도 맞는 값이 나온다. */
static price_t step_price(price_t from, int steps)
{
    price_t p = from;
    for (int i = 0; i < steps; i++) {
        price_t t = tick_size_of(p);
        if (t <= 0) {
            return p;
        }
        p = round_to_tick(p + t, true);
    }
    for (int i = 0; i > steps; i--) {
        price_t t = tick_size_of(p);
        if (t <= 0) {
            return p;
        }
        p = round_to_tick(p - t, false);
    }
    return p;
}

/*
 * 두 시장이 함께 따르는 중심을 한 호가 옮기고, 시장별 어긋남을 조금씩 바꾼다.
 *
 * **방향을 이어 간다.** 매번 새로 뽑으면 제자리걸음만 하고 추세가 생기지 않는다.
 * `LEDGER_DRIFT_FLIP`번에 한 번꼴로 뒤집어 오르내리는 구간을 만든다.
 *
 * **중심은 하나다.** 시장마다 따로 흘리면 같은 종목인데도 몇 %씩 벌어진다 — 실제로는
 * 차익거래가 한두 호가 안으로 묶는다. 시장은 중심 둘레에서만 어긋나고, 그 어긋남이
 * SOR이 고를 거리를 만든다.
 */
static void drift_ref_price(ledger_core_t *c)
{
    uint64_t r = drift_next(&c->drift_state[0]);
    if (c->drift_dir == 0) {
        c->drift_dir = (r & 1u) ? (int8_t)1 : (int8_t)-1;
    } else if ((r % LEDGER_DRIFT_FLIP) == 0) {
        c->drift_dir = (int8_t)-c->drift_dir;
    }

    c->drift_base = step_price(c->drift_base, c->drift_dir);

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        synth_gen_t *gen = divergent_gen(c->div, (market_t)m);
        if (gen == NULL || c->fed[m]) {
            continue;
        }
        /* 어긋남을 가끔 한 칸 옮긴다. 범위를 벗어나면 되돌린다 */
        uint64_t rm = drift_next(&c->drift_state[m]);
        if ((rm % 3) == 0) {
            int8_t off = (int8_t)(c->drift_off[m] + ((rm & 2u) ? 1 : -1));
            if (off > LEDGER_DRIFT_SPREAD) {
                off = LEDGER_DRIFT_SPREAD;
            }
            if (off < -LEDGER_DRIFT_SPREAD) {
                off = -LEDGER_DRIFT_SPREAD;
            }
            c->drift_off[m] = off;
        }
        (void)synth_set_ref_price(gen, step_price(c->drift_base, c->drift_off[m]));
    }
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

    /*
     * 몇 틱에 한 번 중심을 옮긴다. 그래야 가격이 움직이고, 옮긴 자리에서 묵은 호가와
     * 교차해 체결도 난다. 시장을 다 돌기 전에 한 번만 판단한다 — 중심은 하나다.
     */
    if (c->div != NULL && ++c->drift_ticks >= LEDGER_DRIFT_EVERY) {
        c->drift_ticks = 0;
        if (c->drift_base <= 0) {
            c->drift_base = c->cfg.ref_price;
        }
        drift_ref_price(c);
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        synth_gen_t *gen = divergent_gen(c->div, (market_t)m);
        if (gen == NULL || c->fed[m]) {
            continue; /* 바깥 시세를 받는 시장에는 가상 참가자를 넣지 않는다 */
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

/* --- 호가 스냅샷 주입 (T8-03) --- */

/* 이 주문이 원장 사용자의 것인가. 아니면 갈아끼울 상대 호가다. */
static bool is_user_order(const ledger_core_t *c, order_id_t id)
{
    return omap_leg(c->map, id) != NULL;
}

/* 이 가격에 남아 있는 **상대 호가**의 잔량 합. 내 미체결은 빼고 센다. */
static qty_t foreign_qty_at(ledger_core_t *c, market_t m, side_t side,
                            price_t p)
{
    qty_t sum = 0;
    for (order_t *o = book_front(match_book_mut(c->eng[m]), side, p); o != NULL;
         o = o->next) {
        if (!is_user_order(c, o->id)) {
            sum += order_remaining_qty(o);
        }
    }
    return sum;
}

/*
 * 이 가격의 상대 호가를 drop만큼 **앞에서부터** 걷는다.
 *
 * 앞에서부터인 이유는 실제 장에서도 내 앞의 줄이 먼저 빠지기 때문이다. 다 걷지 못할
 * 주문은 수량 감소 정정으로 줄인다 — 같은 가격의 수량 감소는 제자리를 지키므로
 * 내 주문의 차례가 흐트러지지 않는다.
 */
static void feed_shrink(ledger_core_t *c, market_t m, side_t side, price_t p,
                        qty_t drop, ts_t ts)
{
    order_t *o = book_front(match_book_mut(c->eng[m]), side, p);
    while (o != NULL && drop > 0) {
        /* 취소하면 o가 리스트에서 빠지므로 다음을 먼저 잡아 둔다. */
        order_t *nx = o->next;
        if (!is_user_order(c, o->id)) {
            qty_t         rem = order_remaining_qty(o);
            exec_result_t res;
            if (rem <= drop) {
                if (match_cancel(c->eng[m], o->id, ts, &res) == ERR_OK) {
                    drop -= rem;
                }
            } else if (match_modify(c->eng[m], o->id, p, o->qty - drop, ts,
                                    &res) == ERR_OK) {
                drop = 0;
            }
        }
        o = nx;
    }
}

/* 이 가격에 상대 호가 q주를 새로 넣는다. 큐 뒤에 붙는다 — 내 주문 뒤다. */
static void feed_insert(ledger_core_t *c, market_t m, side_t side, price_t p,
                        qty_t q, ts_t ts)
{
    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = c->next_feed_id++;
    o.ts = ts;
    o.price = p;
    o.qty = q;
    o.side = side;
    o.type = ORDER_LIMIT;
    o.market = m;

    exec_result_t res;
    /* 제한폭 밖·호가 단위 불일치는 그 단을 버린다. 바깥 시세를 되받을 수는 없다. */
    (void)match_limit(c->eng[m], &o, &res);
}

/* 스냅샷이 말하는 이 가격의 목표 잔량. 없는 가격이면 0. */
static qty_t feed_target(const price_t *price, const qty_t *qty, price_t p)
{
    for (int i = 0; i < MSG_BOOK_DEPTH; i++) {
        if (price[i] == p) {
            return qty[i] > 0 ? qty[i] : 0;
        }
    }
    return 0;
}

int ledger_core_apply_feed(ledger_core_t *c, const msg_book_feed_t *f)
{
    if (c == NULL || f == NULL) {
        return ERR_NULL_PTR;
    }
    if (f->market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }
    if (strncmp(f->symbol, c->cfg.symbol, MSG_SYMBOL_LEN) != 0) {
        return ERR_NOT_FOUND;
    }

    market_t m = (market_t)f->market;

    /*
     * 피드가 끝났다는 신호. 호가창은 **그대로 두고** 가상 참가자만 돌려보낸다 — 마지막
     * 실호가를 지워 버리면 시뮬로 돌아간 화면이 텅 빈 호가창을 잠깐 보게 된다.
     */
    if ((f->flags & MSG_FEED_END) != 0) {
        c->fed[m] = false;
        return ERR_OK;
    }
    c->fed[m] = true; /* 받는 중. 틱은 이 시장을 건너뛴다 */
    /* 스냅샷의 시각을 쓰되 뒤로 가지 않게 한다. 시스템 시각은 읽지 않는다. */
    ts_t ts = (f->feed_ts > c->clock) ? f->feed_ts : c->clock + 1;
    c->clock = ts;

    const price_t *price[2] = {f->bid_price, f->ask_price};
    const qty_t   *qty[2] = {f->bid_qty, f->ask_qty};

    /*
     * 1단계 — 줄인다. 넣기 전에 끝내야 묵은 호가와 새 호가가 교차하지 않는다.
     *
     * 호가창을 한 번에 다 훑을 수는 없다(`book_snapshot`은 늘 최우선부터 준다). 그래서
     * **한 바퀴 걷고 다시 훑기를 되풀이한다** — 걷힌 단이 사라지면 그다음 단이 최우선으로
     * 올라온다. 훑은 단이 상한보다 적거나(그 방향이 끝났다) 이번 바퀴에 아무것도 걷지
     * 못하면(남은 것이 전부 내 주문이다) 멈춘다. 상한을 믿고 한 바퀴만 돌면 깊은 곳에
     * 묵은 호가가 남고, 나중에 실호가가 그쪽으로 내려오면 있지도 않은 체결이 난다.
     */
    for (int32_t s = 0; s < 2; s++) {
        for (;;) {
            level_view_t view[LEDGER_FEED_SCAN_DEPTH];
            int          n = book_snapshot(match_book(c->eng[m]), (side_t)s,
                                           LEDGER_FEED_SCAN_DEPTH, view);
            bool         removed = false;
            for (int i = 0; i < n; i++) {
                qty_t have = foreign_qty_at(c, m, (side_t)s, view[i].price);
                qty_t want = feed_target(price[s], qty[s], view[i].price);
                if (have > want) {
                    feed_shrink(c, m, (side_t)s, view[i].price, have - want, ts);
                    removed = true;
                }
            }
            if (n < LEDGER_FEED_SCAN_DEPTH || !removed) {
                break;
            }
        }
    }

    /* 2단계 — 모자란 만큼 넣는다. 빈 단(가격이나 잔량이 0)은 건너뛴다. */
    for (int32_t s = 0; s < 2; s++) {
        for (int i = 0; i < MSG_BOOK_DEPTH; i++) {
            price_t p = price[s][i];
            qty_t   q = qty[s][i];
            if (p <= 0 || q <= 0) {
                continue;
            }
            qty_t have = foreign_qty_at(c, m, (side_t)s, p);
            if (q > have) {
                feed_insert(c, m, (side_t)s, p, q - have, ts);
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
