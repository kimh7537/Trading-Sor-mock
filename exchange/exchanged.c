/*
 * exchanged — 거래소 프로세스 (T12-01).
 *
 *   exchanged [포트] [--market krx|nxt] [--ref-price <원>]
 *             [--liquidity <건>] [--seed <수>] [--symbol <코드>]
 *
 * ===========================================================================
 * 왜 이 프로세스가 생겼나
 * ===========================================================================
 *
 * T6-03 "최소 연결"은 SOR과 매칭 엔진을 원장 프로세스 **안에** 넣었다. 띄우고
 * 확인하기 쉬운 쪽을 고른 판단이었고 지금도 그것이 기본이다.
 *
 * 그런데 그 구성에서는 `fep/`가 한 번도 돌지 않는다. 증권사 계층을 재현하는 것이
 * 이 프로젝트의 전제인데 **FEP가 실행 경로에 없으면 그 전제가 빈말이 된다.**
 * 그래서 거래소를 진짜 프로세스로 떼어 내고, 원장이 FEP 세션으로 그것과 이야기하는
 * 구성을 선택지로 둔다(`ledgerd --exchange`).
 *
 * ===========================================================================
 * 매칭 엔진 **하나**만 든다
 * ===========================================================================
 *
 * 한 프로세스가 KRX와 NXT를 둘 다 들면 그것은 거래소가 아니라 그냥 원장이다.
 * 실제로 두 시장은 서로 다른 회사이고 접속도 따로다. 그래서 프로세스 하나가
 * 시장 하나를 맡고, 두 개를 띄운다.
 *
 * ===========================================================================
 * 주문번호는 **여기서** 매긴다
 * ===========================================================================
 *
 * 원장이 보낸 번호(`cl_ord_id`)를 그대로 쓰지 않는다. 거래소 번호를 거래소가 정하는
 * 것이 이 계층이 존재하는 이유다 — 규칙도 범위도 원장이 모르고, 그래서 두 번호를
 * 잇는 표(`fep/ordmap.c`)가 필요하다. 원장이 정하게 두면 그 표가 산술이 되어
 * FEP의 절반이 사라진다.
 *
 * ===========================================================================
 * 체결 **금액**을 답에 싣는다
 * ===========================================================================
 *
 * `MSG_ORDER_ACK`은 평균가만 싣는다. 평균가 x 수량으로 금액을 되돌리면 나머지가
 * 새고, 그 누수를 T7-07에서 한 번 겪었다. 그래서 답은 `MSG_LEG_ACK`이고 금액을
 * i64로 그대로 싣는다.
 *
 * ===========================================================================
 * 시각을 읽지 않는다
 * ===========================================================================
 *
 * 매칭은 주문이 들고 온 논리 시각으로만 한다(`CLAUDE.md`의 결정성). 같은 주문
 * 시퀀스를 같은 시드의 거래소에 보내면 같은 체결이 나온다 — 소켓을 건너도 그렇다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "divergent.h"
#include "errors.h"
#include "listener.h"
#include "market_rules.h"
#include "match.h"
#include "msg.h"
#include "order_book.h"
#include "synthetic.h"
#include "tick_size.h"
#include "types.h"
#include "wire.h"

#define EXCHANGED_DEFAULT_PORT 9201
#define EXCHANGED_ORDER_CAP 65536

/* 거래소가 매기는 주문번호의 시작값. 원장 번호와 눈으로 구별되게 띄워 둔다. */
#define EXCHANGED_FIRST_ID 900000000ULL

typedef struct {
    match_engine_t *eng;
    divergent_t    *div;
    market_t        market;
    char            symbol[MSG_SYMBOL_LEN + 1];
    price_t         ref_price;

    /* 거래소 주문번호. 받은 주문마다 하나씩 올린다 */
    order_id_t next_id;

    /* 마지막 체결가와 누적 체결 수량. 호가 조회가 함께 답한다 */
    price_t last_price;
    int64_t traded_qty;

    /* 논리 시각. 주문이 들고 온 것 중 가장 큰 값을 따라간다 */
    ts_t clock;
} venue_t;

/* 다리 하나를 진짜 엔진에 넣는다. 응답을 지어내지 않는다. */
static void on_leg(venue_t *v, const uint8_t *body, size_t len,
                   msg_leg_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));

    msg_order_req_t req;
    if (msg_decode_order_req(body, len, &req) < 0) {
        ack->reason = ERR_INVALID_ARG;
        return;
    }
    ack->cl_ord_id = req.cl_ord_id;

    if (req.market != (uint8_t)v->market) {
        /* 남의 시장 주문이다. 받아 주면 어느 호가창에 들어갔는지 알 수 없게 된다 */
        ack->reason = ERR_INVALID_ARG;
        return;
    }
    if (strncmp(req.symbol, v->symbol, MSG_SYMBOL_LEN) != 0) {
        ack->reason = ERR_NOT_FOUND;
        return;
    }

    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = v->next_id++;
    o.side = (side_t)req.side;
    o.type = (order_type_t)req.type;
    o.market = v->market;
    o.price = req.price;
    o.qty = req.qty;
    o.ts = ++v->clock;

    exec_result_t res;
    int           rc = match_limit(v->eng, &o, &res);

    ack->order_id = o.id;
    ack->reason = rc;
    if (rc != ERR_OK) {
        return;
    }

    ack->filled_qty = res.filled_qty;
    ack->notional = res.notional;
    ack->resting = res.resting ? 1 : 0;

    if (res.filled_qty > 0) {
        /* 체결가는 마지막 체결의 가격이다. 목록이 잘려도 수량·금액은 정확하다 */
        if (res.fill_count > 0) {
            v->last_price = res.fills[res.fill_count - 1].price;
        }
        v->traded_qty += res.filled_qty;
    }
}

static void on_cancel(venue_t *v, const uint8_t *body, size_t len,
                      msg_cancel_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));

    msg_cancel_req_t req;
    if (msg_decode_cancel_req(body, len, &req) < 0) {
        ack->reason = ERR_INVALID_ARG;
        ack->status = STATUS_REJECTED;
        return;
    }
    ack->order_id = req.order_id;
    ack->cl_ord_id = req.cl_ord_id;

    /*
     * **거래소 번호로만 취소한다.** 원장이 우리 번호를 들고 오는 것은 아직 답을
     * 못 받았다는 뜻인데, 그때는 그 주문이 여기 있는지조차 확실하지 않다.
     * 없으면 없다고 답하고, 되보내기는 원장의 몫이다.
     */
    exec_result_t res;
    int           rc = match_cancel(v->eng, req.order_id, ++v->clock, &res);
    ack->reason = rc;
    /* 취소된 수량은 호가창에 살아 있던 잔량이다 */
    ack->canceled_qty = (rc == ERR_OK) ? res.remaining_qty : 0;
    ack->status = (rc == ERR_OK) ? STATUS_CANCELED : STATUS_REJECTED;
}

static void on_book(venue_t *v, const uint8_t *body, size_t len,
                    msg_book_ack_t *ack)
{
    memset(ack, 0, sizeof(*ack));

    msg_book_req_t req;
    if (msg_decode_book_req(body, len, &req) < 0) {
        return;
    }
    memcpy(ack->symbol, req.symbol, sizeof(ack->symbol));
    ack->market = (uint8_t)v->market;

    if (strncmp(req.symbol, v->symbol, MSG_SYMBOL_LEN) != 0) {
        return;
    }
    ack->last_price = v->last_price;
    ack->traded_qty = v->traded_qty;

    const order_book_t *book = match_book(v->eng);
    level_view_t        view[MSG_BOOK_DEPTH];

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
 * 전문 하나를 처리한다. 리스너가 종별과 길이를 이미 검증했다.
 *
 * **모르는 종별이면 접속을 끊는다.** 조용히 무시하면 보낸 쪽이 영원히 기다린다.
 */
static int on_msg(const wire_header_t *hdr, const uint8_t *body, uint8_t *out,
                  size_t out_cap, void *ctx)
{
    venue_t *v = (venue_t *)ctx;
    if (hdr == NULL || v == NULL) {
        return -1;
    }

    uint8_t bodybuf[MSG_BOOK_ACK_LEN];
    int     m = -1;
    uint8_t reply = 0;

    switch (hdr->type) {
    case MSG_LEG_REQ: {
        msg_leg_ack_t ack;
        on_leg(v, body, hdr->body_len, &ack);
        m = msg_encode_leg_ack(&ack, bodybuf, sizeof(bodybuf));
        reply = MSG_LEG_ACK;
        break;
    }
    case MSG_CANCEL_REQ: {
        msg_cancel_ack_t ack;
        on_cancel(v, body, hdr->body_len, &ack);
        m = msg_encode_cancel_ack(&ack, bodybuf, sizeof(bodybuf));
        reply = MSG_CANCEL_ACK;
        break;
    }
    case MSG_BOOK_REQ: {
        msg_book_ack_t ack;
        on_book(v, body, hdr->body_len, &ack);
        m = msg_encode_book_ack(&ack, bodybuf, sizeof(bodybuf));
        reply = MSG_BOOK_ACK;
        break;
    }
    default:
        return -1;
    }

    if (m < 0) {
        return -1;
    }

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = reply;
    h.body_len = (uint32_t)m;
    h.seq = hdr->seq;
    h.ts = hdr->ts;

    int n = wire_encode_header(&h, out, out_cap);
    if (n < 0 || (size_t)(n + m) > out_cap) {
        return -1;
    }
    memcpy(out + n, bodybuf, (size_t)m);
    return n + m;
}

/* 호가창을 시드 유동성으로 채운다. 빈 호가창은 아무것도 체결하지 못한다. */
static int seed_book(venue_t *v, int32_t orders, uint64_t seedval)
{
    if (orders <= 0) {
        return ERR_OK;
    }

    divergent_config_t d;
    memset(&d, 0, sizeof(d));
    d.scenario = SCENARIO_BALANCED;
    d.seed = seedval;
    d.ref_price = v->ref_price;
    d.tick_table = TICK_TABLE_KRX;
    /* 가격 제한폭 +-30% (docs/SPEC.md 3.1) 안에서 만든다 */
    d.price_low = v->ref_price - v->ref_price * 3 / 10;
    d.price_high = v->ref_price + v->ref_price * 3 / 10;
    d.orders_per_market = orders;

    v->div = divergent_create(&d);
    if (v->div == NULL) {
        return ERR_INVALID_ARG;
    }

    synth_gen_t *gen = divergent_gen(v->div, v->market);
    for (int32_t i = 0; i < orders; i++) {
        order_t       o;
        exec_result_t res;
        if (synth_next(gen, &o) != ERR_OK) {
            break;
        }
        o.market = v->market;
        (void)match_limit(v->eng, &o, &res);
        if (o.ts > v->clock) {
            v->clock = o.ts;
        }
    }
    return ERR_OK;
}

static long take_num(const char *s, long lo, long hi, const char *what)
{
    char *end = NULL;
    long  v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < lo || v > hi) {
        fprintf(stderr, "%s는 %ld~%ld 사이여야 한다\n", what, lo, hi);
        exit(2);
    }
    return v;
}

int main(int argc, char **argv)
{
    uint16_t    port = EXCHANGED_DEFAULT_PORT;
    market_t    market = MARKET_KRX;
    long        ref_price = 70000;
    long        liquidity = 1000;
    long        seedval = 20260917;
    const char *symbol = "005930";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--market") == 0 && i + 1 < argc) {
            if (strcmp(argv[i + 1], "krx") == 0) {
                market = MARKET_KRX;
            } else if (strcmp(argv[i + 1], "nxt") == 0) {
                market = MARKET_NXT;
            } else {
                fprintf(stderr, "--market은 krx 또는 nxt여야 한다\n");
                return 2;
            }
            i++;
            continue;
        }
        if (strcmp(argv[i], "--ref-price") == 0 && i + 1 < argc) {
            ref_price = take_num(argv[++i], 100, 100000000, "--ref-price");
            continue;
        }
        if (strcmp(argv[i], "--liquidity") == 0 && i + 1 < argc) {
            liquidity = take_num(argv[++i], 0, 1000000, "--liquidity");
            continue;
        }
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seedval = take_num(argv[++i], 1, 2000000000, "--seed");
            continue;
        }
        if (strcmp(argv[i], "--symbol") == 0 && i + 1 < argc) {
            symbol = argv[++i];
            continue;
        }
        port = (uint16_t)take_num(argv[i], 0, 65535, "포트");
    }

    if (listener_install_signals() != ERR_OK) {
        fprintf(stderr, "시그널 핸들러를 걸 수 없다\n");
        return 1;
    }

    venue_t v;
    memset(&v, 0, sizeof(v));
    v.market = market;
    v.ref_price = (price_t)ref_price;
    v.next_id = EXCHANGED_FIRST_ID;
    snprintf(v.symbol, sizeof(v.symbol), "%s", symbol);

    v.eng = match_engine_create((price_t)ref_price,
                                (int32_t)liquidity + EXCHANGED_ORDER_CAP);
    if (v.eng == NULL) {
        fprintf(stderr, "매칭 엔진을 만들 수 없다\n");
        return 1;
    }
    /*
     * **세션 규칙을 걸지 않는다.** `ledger_core`가 같은 판단을 한 자리다 — 규칙을
     * 걸면 실제 시계가 아니라 **논리 시각**으로 장이 열고 닫히는데, 논리 시각은
     * 주문이 들고 온 값이라 데모에서는 늘 장 밖이다. 걸어 봤더니 시드 유동성까지
     * `-7 장이 열려 있지 않음`으로 거절돼 호가창이 통째로 비었다.
     *
     * 더 중요한 이유가 있다. **원격 경로가 in-process 경로와 같은 답을 내야 한다** —
     * 여기만 규칙을 걸면 `--exchange`로 띄웠을 때와 아닐 때 체결이 달라진다.
     * 세션 규칙 자체는 T1-13~15가 따로 검증한다.
     */
    (void)KRX_RULES;
    (void)NXT_RULES;

    if (seed_book(&v, (int32_t)liquidity, (uint64_t)seedval) != ERR_OK) {
        fprintf(stderr, "유동성을 넣을 수 없다\n");
        match_engine_destroy(v.eng);
        return 1;
    }

    listener_t *ln = listener_open(port, 64);
    if (ln == NULL) {
        fprintf(stderr, "포트 %u 를 열 수 없다\n", (unsigned)port);
        divergent_destroy(v.div);
        match_engine_destroy(v.eng);
        return 1;
    }

    printf("exchanged %s 포트 %u 에서 대기 (SIGTERM/SIGINT로 종료)\n",
           market == MARKET_KRX ? "KRX" : "NXT", (unsigned)listener_port(ln));
    printf("  종목 %s, 기준가 %ld원, 시드 유동성 %ld건 (시드 %ld)\n", v.symbol,
           ref_price, liquidity, seedval);
    printf("  주문번호는 %llu 부터 거래소가 매긴다\n",
           (unsigned long long)EXCHANGED_FIRST_ID);
    fflush(stdout);

    (void)listener_run(ln, on_msg, &v);

    listener_close(ln);
    divergent_destroy(v.div);
    match_engine_destroy(v.eng);
    printf("exchanged 종료\n");
    return 0;
}
