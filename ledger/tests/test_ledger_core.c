/*
 * T6-03 원장 코어 — 전문 하나가 검증·증거금·배분·체결·정산을 지나가는가.
 *
 * 소켓을 쓰지 않는다. 전문 바이트를 만들어 처리 훅에 직접 넣고, 돌아온 응답 바이트와
 * 계좌 잔고·호가창을 본다. 리스너(T3-03)는 따로 시험됐다.
 *
 * **돈이 맞는지는 매 단계 따로 본다.** 이 데모는 계좌가 하나라 자기 주문끼리
 * 체결되면 매수의 출금과 매도의 입금이 상쇄되어 합계가 0이 된다. 합계만 보면
 * "매수를 지정가로 잘못 정산하고 매도도 같은 값으로 입금"한 버그가 통과한다.
 * 그래서 유동성과 체결하는 경우(상쇄 없음)와 묶인 금액을 따로 본다.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "ledger_core.h"
#include "msg.h"
#include "tick_size.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

#define ACCT "123456789012"
#define CASH ((int64_t)100000000)

/* 유동성 없는 빈 호가창 — 체결 상대를 테스트가 직접 만든다. */
/*
 * 보유를 미리 실어 둔다(T11-01).
 *
 * 매도 주문은 이제 **없는 주식을 팔 수 없다.** 여기 있는 시험 대부분은 매칭을 보는
 * 것이지 보유를 보는 것이 아니므로, 계좌에 넉넉히 실어 두고 예전처럼 팔게 한다.
 * 보유 자체를 보는 시험은 각자 실어서 확인한다.
 */
#define TEST_POS_QTY 1000000
#define TEST_POS_COST (70000LL * TEST_POS_QTY)

static void seed_shares(ledger_core_t *c)
{
    assert(ledger_core_seed_position(c, ACCT, TEST_POS_QTY, TEST_POS_COST, 0) ==
           ERR_OK);
}

static ledger_core_t *empty_core(int32_t capacity)
{
    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT;
    cfg.liquidity_per_market = 0;
    cfg.order_capacity = capacity;
    ledger_core_t *c = ledger_core_create(&cfg);
    assert(c != NULL);
    seed_shares(c);
    return c;
}

/* 기본 설정(유동성 있음). 시장당 유동성을 줄여 빠르게 돈다. */
static ledger_core_t *liquid_core(void)
{
    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT;
    cfg.liquidity_per_market = 300;
    cfg.order_capacity = 256;
    ledger_core_t *c = ledger_core_create(&cfg);
    assert(c != NULL);
    seed_shares(c);
    return c;
}

/*
 * 주문 전문을 만들어 넣고 응답 전문을 풀어 돌려준다. 응답 바이트는 raw에 남긴다.
 */
static int send_order_ex(ledger_core_t *c, const char *account, uint8_t side,
                         uint8_t type, uint8_t market, price_t price, qty_t qty,
                         uint64_t cl, msg_order_ack_t *ack, uint8_t *raw,
                         int *raw_len)
{
    msg_order_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", account);
    snprintf(req.symbol, sizeof(req.symbol), "%s", "005930");
    req.cl_ord_id = cl;
    req.side = side;
    req.type = type;
    req.market = market;
    req.price = price;
    req.qty = qty;

    uint8_t body[MSG_ORDER_REQ_LEN];
    assert(msg_encode_order_req(&req, body, sizeof(body)) ==
           (int)MSG_ORDER_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_ORDER_REQ;
    h.body_len = MSG_ORDER_REQ_LEN;
    h.seq = cl;
    h.ts = 7;

    uint8_t out[256];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_ORDER_ACK_LEN));

    wire_header_t rh;
    assert(wire_decode_header(out, (size_t)n, &rh) == (int)WIRE_HEADER_LEN);
    assert(rh.type == MSG_ORDER_ACK);
    assert(rh.seq == cl); /* 요청의 시퀀스를 그대로 돌려준다 */
    assert(msg_decode_order_ack(out + WIRE_HEADER_LEN, MSG_ORDER_ACK_LEN, ack) >=
           0);
    assert(ack->cl_ord_id == cl);

    if (raw != NULL) {
        memcpy(raw, out, (size_t)n);
        *raw_len = n;
    }
    return ack->reason;
}

static int send_order(ledger_core_t *c, uint8_t side, uint8_t market,
                      price_t price, qty_t qty, uint64_t cl,
                      msg_order_ack_t *ack)
{
    return send_order_ex(c, ACCT, side, ORDER_LIMIT, market, price, qty, cl,
                         ack, NULL, NULL);
}

static void balance(ledger_core_t *c, int64_t *cash, int64_t *reserved)
{
    assert(ledger_core_balance(c, ACCT, cash, reserved) == ERR_OK);
}

/* --- 1. 유동성과 체결하는 매수 --- */

/*
 * **매수는 매도호가를 먹는다.** 방향 값이 C의 정의(`SIDE_BUY=0`)대로 해석되는지를
 * C 쪽에서도 못 박는다 — T6-01에서 화면의 "매수"가 매도로 읽혔다.
 *
 * 유동성과 체결하므로 입금으로 상쇄되지 않는다. 예수금이 정확히 `매도호가 x 수량`만큼
 * 줄어야 한다.
 */
static void test_buy_takes_liquidity(void)
{
    ledger_core_t      *c = liquid_core();
    const order_book_t *krx = ledger_core_book(c, MARKET_KRX);
    const order_book_t *nxt = ledger_core_book(c, MARKET_NXT);

    price_t ask = book_best_ask(krx);
    assert(ask != BOOK_PRICE_NONE);
    qty_t level = book_qty_at(krx, SIDE_SELL, ask);
    qty_t qty = (level < 37) ? level : 37;
    assert(qty > 0);

    price_t nxt_ask = book_best_ask(nxt);
    qty_t   nxt_level = book_qty_at(nxt, SIDE_SELL, nxt_ask);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MARKET_KRX, ask, qty, 1, &ack) == ERR_OK);

    assert(ack.filled_qty == qty);
    assert(ack.status == STATUS_FILLED);
    assert(ack.price == ask);
    assert(ack.order_id != 0);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(cash == CASH - (int64_t)ask * qty); /* 정확히 체결 대금만큼 */
    assert(reserved == 0);                      /* 다 체결됐으니 묶인 돈이 없다 */

    /* 매도호가가 줄었다 — 매수가 매도로 읽혔다면 매수호가가 늘었을 것이다 */
    assert(book_qty_at(krx, SIDE_SELL, ask) == level - qty);
    /* 지정한 시장만 건드렸다 */
    assert(book_best_ask(nxt) == nxt_ask);
    assert(book_qty_at(nxt, SIDE_SELL, nxt_ask) == nxt_level);

    ledger_core_destroy(c);
}

/* 매도는 매수호가를 먹고 **대금이 입금된다.** 매도는 증거금을 묶지 않는다 */
static void test_sell_takes_liquidity(void)
{
    ledger_core_t      *c = liquid_core();
    const order_book_t *krx = ledger_core_book(c, MARKET_KRX);

    price_t bid = book_best_bid(krx);
    assert(bid != BOOK_PRICE_NONE);
    qty_t level = book_qty_at(krx, SIDE_BUY, bid);
    qty_t qty = (level < 23) ? level : 23;

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_SELL, MARKET_KRX, bid, qty, 2, &ack) == ERR_OK);
    assert(ack.filled_qty == qty);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(cash == CASH + (int64_t)bid * qty);
    assert(reserved == 0);
    assert(book_qty_at(krx, SIDE_BUY, bid) == level - qty);

    ledger_core_destroy(c);
}

/* --- 2. 걸어 두기와 나중 체결(maker) --- */

/*
 * **호가창에 남은 주문은 증거금이 묶인 채로 있고, 나중에 체결되면 그때 정산된다.**
 *
 * 집행기 보고서는 "지금 들어온 주문"의 체결만 알려 준다. 예전에 걸어 둔 주문이
 * 체결되는 경우를 원장이 놓치면 **묶인 돈이 영영 안 풀린다.** 콜백이 그것을 잡는지 본다.
 */
static void test_resting_then_maker_fill(void)
{
    ledger_core_t      *c = empty_core(64);
    const order_book_t *krx = ledger_core_book(c, MARKET_KRX);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 20, 10, &ack) == ERR_OK);
    assert(ack.filled_qty == 0);
    assert(ack.status == STATUS_NEW);
    assert(book_qty_at(krx, SIDE_BUY, 70000) == 20);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(cash == CASH);
    assert(reserved == (int64_t)70000 * 20); /* 걸어 둔 만큼 묶였다 */

    /* 더 싼 매도가 들어와 걸어 둔 매수를 12주 친다. 체결은 maker 가격 70,000 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 69900, 12, 11, &ack) == ERR_OK);
    assert(ack.filled_qty == 12);
    assert(ack.price == 70000);

    balance(c, &cash, &reserved);
    /*
     * maker 매수: 예수금 -840,000, 묶음 -840,000
     * taker 매도: 예수금 +840,000
     * 같은 계좌라 예수금은 제자리다. **묶음이 8주분으로 줄었는지가 핵심이다.**
     */
    assert(reserved == (int64_t)70000 * 8);
    assert(cash == CASH);

    /* 나머지 8주도 친다 — 묶음이 0이 되어야 한다 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 8, 12, &ack) == ERR_OK);
    assert(ack.filled_qty == 8);
    balance(c, &cash, &reserved);
    assert(reserved == 0);
    assert(cash == CASH);
    assert(book_qty_at(krx, SIDE_BUY, 70000) == 0);

    ledger_core_destroy(c);
}

/*
 * **매수가 더 싸게 체결되면 남는 증거금을 푼다.**
 * 69,000에 걸린 매도를 70,000 지정가 매수가 치면 1,000원 x 10주가 남는다.
 */
static void test_price_improvement_released(void)
{
    ledger_core_t *c = empty_core(64);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_SELL, MARKET_NXT, 69000, 10, 20, &ack) == ERR_OK);
    assert(ack.filled_qty == 0);

    assert(send_order(c, SIDE_BUY, MARKET_NXT, 70000, 10, 21, &ack) == ERR_OK);
    assert(ack.filled_qty == 10);
    assert(ack.price == 69000); /* maker 가격에 체결 */

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(reserved == 0); /* 700,000을 묶었고 690,000 정산 + 10,000 해제 */
    assert(cash == CASH);  /* 같은 계좌: -690,000 + 690,000 */

    ledger_core_destroy(c);
}

/*
 * **체결되지 않을 수량의 증거금을 푼다** — 호가창에 남지 않는 IOC.
 *
 * 5주밖에 없는 곳에 IOC 12주를 내면 7주는 취소된다. 그 7주분이 계속 묶여 있으면
 * 돈이 조금씩 사라진다.
 */
static void test_ioc_remainder_released(void)
{
    ledger_core_t *c = empty_core(64);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 5, 30, &ack) == ERR_OK);

    assert(send_order_ex(c, ACCT, SIDE_BUY, ORDER_IOC, MARKET_KRX, 70000, 12, 31,
                         &ack, NULL, NULL) == ERR_OK);
    assert(ack.filled_qty == 5);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(reserved == 0);
    assert(cash == CASH);

    ledger_core_destroy(c);
}

/*
 * **NXT를 지정하면 NXT 호가창에 간다.**
 *
 * 처음엔 시장을 늘 KRX로 보내는 변이(L12)가 살아남았다. NXT를 쓰는 테스트가 빈
 * 호가창에서 자기 주문끼리 체결시키는 것뿐이라, 둘 다 KRX로 가도 똑같이 체결됐다.
 * 유동성이 있는 호가창에서 **어느 시장의 호가가 줄었는지**로 본다.
 */
static void test_explicit_nxt_goes_to_nxt(void)
{
    ledger_core_t      *c = liquid_core();
    const order_book_t *krx = ledger_core_book(c, MARKET_KRX);
    const order_book_t *nxt = ledger_core_book(c, MARKET_NXT);

    price_t na = book_best_ask(nxt);
    qty_t   nl = book_qty_at(nxt, SIDE_SELL, na);
    qty_t   qty = (nl < 11) ? nl : 11;
    price_t ka = book_best_ask(krx);
    qty_t   kl = book_qty_at(krx, SIDE_SELL, ka);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MARKET_NXT, na, qty, 45, &ack) == ERR_OK);
    assert(ack.filled_qty == qty);

    assert(book_qty_at(nxt, SIDE_SELL, na) == nl - qty); /* NXT가 줄었다 */
    assert(book_best_ask(krx) == ka);                    /* KRX는 그대로 */
    assert(book_qty_at(krx, SIDE_SELL, ka) == kl);

    ledger_core_destroy(c);
}

/* 조회 전문을 만들어 넣고 응답을 푼다 */
static void query(ledger_core_t *c, order_id_t id, msg_query_ack_t *ack)
{
    msg_query_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", ACCT);
    req.order_id = id;

    uint8_t body[MSG_QUERY_REQ_LEN];
    assert(msg_encode_query_req(&req, body, sizeof(body)) ==
           (int)MSG_QUERY_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_QUERY_REQ;
    h.body_len = MSG_QUERY_REQ_LEN;
    h.seq = 90;

    uint8_t out[256];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_QUERY_ACK_LEN));
    assert(msg_decode_query_ack(out + WIRE_HEADER_LEN, MSG_QUERY_ACK_LEN, ack) >=
           0);
    assert(ack->order_id == id);
    assert(ack->last == 1);
}

/*
 * **호가창에 걸어 뒀던 주문의 나중 체결이 조회에 보인다.**
 *
 * 돈은 콜백이 맞게 정산해도, 그 주문의 체결 수량을 매핑에 적지 않으면 미체결 화면이
 * 영영 "0주 체결"로 남는다. 처음엔 그 반영을 빼도 테스트가 통과했다(변이 L4) — 매핑을
 * 읽는 곳이 없었기 때문이다. 조회를 구현해 관측할 수 있게 했다.
 */
static void test_query_reflects_maker_fill(void)
{
    ledger_core_t  *c = empty_core(64);
    msg_order_ack_t ack;

    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 20, 555, &ack) == ERR_OK);
    order_id_t resting = ack.order_id;

    msg_query_ack_t q;
    query(c, resting, &q);
    assert(q.status == STATUS_NEW);
    assert(q.cl_ord_id == 555); /* 주문을 낸 쪽의 번호를 돌려준다 */
    assert(q.qty == 20 && q.filled_qty == 0);
    assert(q.price == 70000);
    assert(strcmp(q.symbol, "005930") == 0);

    /* 매도 12주가 걸어 둔 매수를 친다 — 걸어 둔 쪽은 maker */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 12, 556, &ack) == ERR_OK);

    query(c, resting, &q);
    assert(q.filled_qty == 12);
    assert(q.status == STATUS_PARTIAL);

    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 8, 557, &ack) == ERR_OK);
    query(c, resting, &q);
    assert(q.filled_qty == 20);
    assert(q.status == STATUS_FILLED);

    /* 모르는 번호, 전체 조회(0)는 "없음"으로 답한다 */
    query(c, resting + 1000, &q);
    assert(q.status == STATUS_REJECTED && q.qty == 0);
    query(c, 0, &q);
    assert(q.status == STATUS_REJECTED && q.qty == 0);

    ledger_core_destroy(c);
}

/* 호가 조회 전문을 만들어 넣고 응답을 푼다 */
static void book(ledger_core_t *c, const char *symbol, uint8_t market,
                 msg_book_ack_t *ack)
{
    msg_book_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.symbol, sizeof(req.symbol), "%s", symbol);
    req.market = market;

    uint8_t body[MSG_BOOK_REQ_LEN];
    assert(msg_encode_book_req(&req, body, sizeof(body)) ==
           (int)MSG_BOOK_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_BOOK_REQ;
    h.body_len = MSG_BOOK_REQ_LEN;
    h.seq = 91;

    uint8_t out[WIRE_HEADER_LEN + MSG_BOOK_ACK_LEN];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_BOOK_ACK_LEN));

    wire_header_t rh;
    assert(wire_decode_header(out, (size_t)n, &rh) == (int)WIRE_HEADER_LEN);
    assert(rh.type == MSG_BOOK_ACK && rh.seq == 91);
    assert(msg_decode_book_ack(out + WIRE_HEADER_LEN, MSG_BOOK_ACK_LEN, ack) >=
           0);
    assert(ack->market == market);
}

/*
 * T6-04 — **화면이 보는 호가가 원장 안의 호가창 그대로다.** 시장이 섞이지 않고,
 * 체결되면 바로 줄어든다.
 */
static void test_book_query(void)
{
    ledger_core_t  *c = empty_core(64);
    msg_order_ack_t ack;
    msg_book_ack_t  b;

    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 20, 600, &ack) == ERR_OK);
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 69900, 5, 601, &ack) == ERR_OK);
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70100, 7, 602, &ack) == ERR_OK);
    assert(send_order(c, SIDE_BUY, MARKET_NXT, 69800, 3, 603, &ack) == ERR_OK);

    book(c, "005930", MARKET_KRX, &b);
    assert(strcmp(b.symbol, "005930") == 0);
    assert(b.bid_price[0] == 70000 && b.bid_qty[0] == 20); /* 높은 가격부터 */
    assert(b.bid_price[1] == 69900 && b.bid_qty[1] == 5);
    assert(b.bid_price[2] == 0 && b.bid_qty[2] == 0); /* 없는 단은 0 */
    assert(b.ask_price[0] == 70100 && b.ask_qty[0] == 7);
    assert(b.ask_price[1] == 0);

    book(c, "005930", MARKET_NXT, &b);
    assert(b.bid_price[0] == 69800 && b.bid_qty[0] == 3);
    assert(b.bid_price[1] == 0 && b.ask_price[0] == 0);

    /* 체결되면 줄어든다 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 12, 604, &ack) == ERR_OK);
    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[0] == 70000 && b.bid_qty[0] == 8);

    /* 다루지 않는 종목, 없는 시장은 빈 호가창 */
    book(c, "000660", MARKET_KRX, &b);
    assert(b.bid_price[0] == 0 && b.ask_price[0] == 0);
    book(c, "005930", 7, &b);
    assert(b.bid_price[0] == 0 && b.ask_price[0] == 0);

    ledger_core_destroy(c);
}

/*
 * 유동성이 있는 호가창을 **직접 읽은 것과 조회 응답이 같다.** 채워진 단 뒤는 0이다.
 * (유동성 300건으로는 10단이 다 차지 않을 수 있어 단 수를 가정하지 않는다.)
 */
static void compare_side(const order_book_t *ob, side_t side,
                         const price_t *price, const qty_t *qty)
{
    level_view_t view[MSG_BOOK_DEPTH];
    int          n = book_snapshot(ob, side, MSG_BOOK_DEPTH, view);
    assert(n > 0);
    for (int i = 0; i < MSG_BOOK_DEPTH; i++) {
        assert(price[i] == (i < n ? view[i].price : 0));
        assert(qty[i] == (i < n ? view[i].total_qty : 0));
    }
}

static void test_book_query_matches_book(void)
{
    ledger_core_t *c = liquid_core();
    for (uint8_t m = 0; m < MARKET_COUNT; m++) {
        msg_book_ack_t b;
        book(c, "005930", m, &b);
        const order_book_t *ob = ledger_core_book(c, (market_t)m);
        compare_side(ob, SIDE_BUY, b.bid_price, b.bid_qty);
        compare_side(ob, SIDE_SELL, b.ask_price, b.ask_qty);
    }
    ledger_core_destroy(c);
}

/* --- 3. SOR --- */

/*
 * **시장을 자동으로 두면 더 싼 쪽으로 간다.**
 * `market = MSG_MARKET_AUTO`면 BEST_PRICE 전략이 통합 호가창을 보고 고른다.
 */
static void test_auto_routes_to_cheaper_market(void)
{
    ledger_core_t      *c = liquid_core();
    const order_book_t *krx = ledger_core_book(c, MARKET_KRX);
    const order_book_t *nxt = ledger_core_book(c, MARKET_NXT);

    price_t ak = book_best_ask(krx);
    price_t an = book_best_ask(nxt);
    assert(ak != BOOK_PRICE_NONE && an != BOOK_PRICE_NONE);

    const order_book_t *cheap = (ak <= an) ? krx : nxt;
    const order_book_t *other = (ak <= an) ? nxt : krx;
    price_t             best = (ak <= an) ? ak : an;
    price_t             other_ask = (ak <= an) ? an : ak;

    qty_t level = book_qty_at(cheap, SIDE_SELL, best);
    qty_t qty = (level < 9) ? level : 9;
    qty_t other_level = book_qty_at(other, SIDE_SELL, other_ask);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MSG_MARKET_AUTO, best, qty, 40, &ack) ==
           ERR_OK);
    assert(ack.filled_qty == qty);
    assert(ack.price == best);

    assert(book_qty_at(cheap, SIDE_SELL, best) == level - qty);
    if (ak != an) {
        /* 두 시장 호가가 다르면 비싼 쪽은 건드리지 않았어야 한다 */
        assert(book_qty_at(other, SIDE_SELL, other_ask) == other_level);
    }

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(cash == CASH - (int64_t)best * qty);
    assert(reserved == 0);

    ledger_core_destroy(c);
}

/* --- 4. 거부 --- */

/*
 * **거부는 이유를 말하고 돈을 건드리지 않는다.** 예전 껍데기는 무엇이 와도 성공이라고
 * 답했다.
 */
static void test_rejects_leave_money_alone(void)
{
    ledger_core_t  *c = empty_core(64);
    msg_order_ack_t ack;
    int64_t         cash, reserved;

    /* 호가 단위 — 70,000원대는 100원 단위다 */
    assert(tick_size_of(70000) == 100);
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70050, 10, 50, &ack) ==
           ERR_INVALID_TICK);
    assert(ack.status == STATUS_REJECTED);

    /* 없는 계좌 */
    assert(send_order_ex(c, "999999999999", SIDE_BUY, ORDER_LIMIT, MARKET_KRX,
                         70000, 10, 51, &ack, NULL, NULL) == ERR_NOT_FOUND);

    /* 증거금 부족 — 7억 원어치 */
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 10000, 52, &ack) ==
           ERR_NO_MARGIN);

    /*
     * 없는 시장. 배분 단계(`plan_add_leg`)가 거절하고, **그 전에 묶은 증거금을
     * 푼다.** 이 경로는 처음에 앞쪽의 중복 검사에 가려 한 번도 실행되지 않았다.
     */
    assert(send_order(c, SIDE_BUY, 7, 70000, 10, 53, &ack) == ERR_INVALID_ARG);
    balance(c, &cash, &reserved);
    assert(reserved == 0);

    /* 옛 번호 체계의 "매도"(2)는 없는 방향이다 — T6-01 */
    assert(send_order(c, 2, MARKET_KRX, 70000, 10, 54, &ack) == ERR_INVALID_ARG);

    balance(c, &cash, &reserved);
    assert(cash == CASH);
    assert(reserved == 0);
    assert(book_best_bid(ledger_core_book(c, MARKET_KRX)) == BOOK_PRICE_NONE);

    ledger_core_destroy(c);
}

/* 자리가 차면 거절한다. 묶은 돈이 새지 않는다 */
static void test_capacity(void)
{
    ledger_core_t  *c = empty_core(2);
    msg_order_ack_t ack;

    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 1, 60, &ack) == ERR_OK);
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 1, 61, &ack) == ERR_OK);
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 1, 62, &ack) ==
           ERR_POOL_EXHAUSTED);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(reserved == (int64_t)70000 * 2); /* 세 번째는 묶지 않았다 */

    ledger_core_destroy(c);
}

/* --- 5. 취소와 정정 --- */

/* 취소 전문을 만들어 넣고 응답을 푼다. 응답의 reason을 돌려준다 */
static int cancel(ledger_core_t *c, const char *account, order_id_t id,
                  uint64_t cl, msg_cancel_ack_t *ack)
{
    msg_cancel_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", account);
    req.order_id = id;
    req.cl_ord_id = cl;

    uint8_t body[MSG_CANCEL_REQ_LEN];
    assert(msg_encode_cancel_req(&req, body, sizeof(body)) ==
           (int)MSG_CANCEL_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_CANCEL_REQ;
    h.body_len = MSG_CANCEL_REQ_LEN;
    h.seq = cl;

    uint8_t out[256];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_CANCEL_ACK_LEN));

    wire_header_t rh;
    assert(wire_decode_header(out, (size_t)n, &rh) == (int)WIRE_HEADER_LEN);
    assert(rh.type == MSG_CANCEL_ACK && rh.seq == cl);
    assert(msg_decode_cancel_ack(out + WIRE_HEADER_LEN, MSG_CANCEL_ACK_LEN,
                                 ack) >= 0);
    assert(ack->order_id == id);
    assert(ack->cl_ord_id == cl);
    return ack->reason;
}

/* --- 5-1. 주문 상세·잔고 조회 (T7-02) --- */

static int detail(ledger_core_t *c, const char *account, order_id_t id,
                  msg_detail_ack_t *ack)
{
    msg_detail_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", account);
    req.order_id = id;

    uint8_t body[MSG_DETAIL_REQ_LEN];
    assert(msg_encode_detail_req(&req, body, sizeof(body)) ==
           (int)MSG_DETAIL_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_DETAIL_REQ;
    h.body_len = MSG_DETAIL_REQ_LEN;
    h.seq = 93;

    uint8_t out[WIRE_HEADER_LEN + MSG_DETAIL_ACK_LEN];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_DETAIL_ACK_LEN));

    wire_header_t rh;
    assert(wire_decode_header(out, (size_t)n, &rh) == (int)WIRE_HEADER_LEN);
    assert(rh.type == MSG_DETAIL_ACK && rh.seq == 93);
    assert(msg_decode_detail_ack(out + WIRE_HEADER_LEN, MSG_DETAIL_ACK_LEN, ack) >=
           0);
    assert(ack->order_id == id);
    return ack->reason;
}

static int balance_msg(ledger_core_t *c, const char *account,
                       msg_balance_ack_t *ack)
{
    msg_balance_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", account);

    uint8_t body[MSG_BALANCE_REQ_LEN];
    assert(msg_encode_balance_req(&req, body, sizeof(body)) ==
           (int)MSG_BALANCE_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_BALANCE_REQ;
    h.body_len = MSG_BALANCE_REQ_LEN;
    h.seq = 94;

    uint8_t out[WIRE_HEADER_LEN + MSG_BALANCE_ACK_LEN];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_BALANCE_ACK_LEN));

    wire_header_t rh;
    assert(wire_decode_header(out, (size_t)n, &rh) == (int)WIRE_HEADER_LEN);
    assert(rh.type == MSG_BALANCE_ACK && rh.seq == 94);
    assert(msg_decode_balance_ack(out + WIRE_HEADER_LEN, MSG_BALANCE_ACK_LEN,
                                  ack) >= 0);
    assert(strcmp(ack->account, account) == 0);
    return ack->reason;
}

/*
 * **SOR이 어느 시장으로 보냈는지가 상세에 그대로 보인다(논리 → 물리).**
 * 싼 쪽으로 간 매수의 다리는 그 시장에만 있고, 체결 금액이 시장별로 맞아떨어진다.
 */
static void test_detail_shows_legs(void)
{
    ledger_core_t      *c = liquid_core();
    const order_book_t *krx = ledger_core_book(c, MARKET_KRX);
    const order_book_t *nxt = ledger_core_book(c, MARKET_NXT);

    price_t  ak = book_best_ask(krx);
    price_t  an = book_best_ask(nxt);
    market_t cheap = (ak <= an) ? MARKET_KRX : MARKET_NXT;
    market_t other = (cheap == MARKET_KRX) ? MARKET_NXT : MARKET_KRX;
    price_t  best = (ak <= an) ? ak : an;
    qty_t    level = book_qty_at(ledger_core_book(c, cheap), SIDE_SELL, best);
    qty_t    qty = (level < 7) ? level : 7;

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MSG_MARKET_AUTO, best, qty, 800, &ack) == ERR_OK);
    assert(ack.filled_qty == qty);

    msg_detail_ack_t d;
    assert(detail(c, ACCT, ack.order_id, &d) == ERR_OK);
    assert(d.cl_ord_id == 800);
    assert(d.side == SIDE_BUY);
    assert(d.market == MSG_MARKET_AUTO); /* 사용자가 고른 것은 "자동" */
    assert(d.status == STATUS_FILLED);
    assert(d.price == best && d.qty == qty);
    assert(d.filled == qty && d.canceled == 0 && d.working == 0);
    assert(d.notional == (int64_t)best * qty);

    assert(d.leg_sent[cheap] == qty && d.leg_filled[cheap] == qty);
    assert(d.leg_notional[cheap] == (int64_t)best * qty);
    assert(d.leg_sent[other] == 0 && d.leg_filled[other] == 0);
    assert(d.leg_notional[other] == 0);

    ledger_core_destroy(c);
}

/*
 * **걸어 둔 주문의 나중 체결과 취소가 상세에 반영된다.** 화면의 미체결 목록이 이것을 읽는다.
 */
static void test_detail_tracks_later_events(void)
{
    ledger_core_t   *c = empty_core(16);
    msg_order_ack_t  ack;
    msg_detail_ack_t d;

    assert(send_order(c, SIDE_BUY, MARKET_NXT, 70000, 20, 810, &ack) == ERR_OK);
    order_id_t id = ack.order_id;

    assert(detail(c, ACCT, id, &d) == ERR_OK);
    assert(d.status == STATUS_NEW && d.working == 20 && d.market == MARKET_NXT);
    assert(d.leg_sent[MARKET_NXT] == 20 && d.leg_sent[MARKET_KRX] == 0);

    /* 12주가 maker로 체결 */
    assert(send_order(c, SIDE_SELL, MARKET_NXT, 69900, 12, 811, &ack) == ERR_OK);
    /* 매도 쪽 상세: 방향이 매도(1)로 실린다 — 매수는 0이라 방향을 안 실어도 매수 시험은 통과한다 */
    msg_detail_ack_t sd;
    assert(detail(c, ACCT, ack.order_id, &sd) == ERR_OK);
    assert(sd.side == SIDE_SELL && sd.filled == 12 && sd.price == 69900);
    assert(detail(c, ACCT, id, &d) == ERR_OK);
    assert(d.status == STATUS_PARTIAL);
    assert(d.filled == 12 && d.working == 8);
    assert(d.notional == (int64_t)70000 * 12);
    assert(d.leg_filled[MARKET_NXT] == 12);
    assert(d.leg_notional[MARKET_NXT] == (int64_t)70000 * 12);

    /* 나머지 취소 */
    msg_cancel_ack_t ca;
    assert(cancel(c, ACCT, id, 812, &ca) == ERR_OK);
    assert(detail(c, ACCT, id, &d) == ERR_OK);
    assert(d.canceled == 8 && d.working == 0);
    assert(d.leg_canceled[MARKET_NXT] == 8);
    assert(d.status == STATUS_PARTIAL);

    ledger_core_destroy(c);
}

/* **남의 주문, 없는 주문은 "없음"** — 취소와 같은 규칙이다 */
static void test_detail_rejections(void)
{
    ledger_core_t   *c = empty_core(16);
    msg_order_ack_t  ack;
    msg_detail_ack_t d;

    assert(ledger_core_open_account(c, "210987654321", 1000000) == ERR_OK);
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 3, 820, &ack) == ERR_OK);

    assert(detail(c, "210987654321", ack.order_id, &d) == ERR_NOT_FOUND);
    assert(d.status == STATUS_REJECTED && d.qty == 0 && d.leg_sent[MARKET_KRX] == 0);
    assert(detail(c, ACCT, ack.order_id + 99, &d) == ERR_NOT_FOUND);
    assert(detail(c, ACCT, 0, &d) == ERR_NOT_FOUND);
    assert(detail(c, "999999999999", ack.order_id, &d) == ERR_NOT_FOUND);

    ledger_core_destroy(c);
}

/* **잔고 조회 전문이 원장 장부와 같은 값을 준다.** 주문에 따라 움직인다 */
static void test_balance_message(void)
{
    ledger_core_t    *c = empty_core(16);
    msg_balance_ack_t b;
    msg_order_ack_t   ack;

    assert(balance_msg(c, ACCT, &b) == ERR_OK);
    assert(b.cash == CASH && b.reserved == 0);

    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 10, 830, &ack) == ERR_OK);
    assert(balance_msg(c, ACCT, &b) == ERR_OK);
    assert(b.cash == CASH && b.reserved == (int64_t)70000 * 10);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(b.cash == cash && b.reserved == reserved);

    assert(balance_msg(c, "999999999999", &b) == ERR_NOT_FOUND);
    assert(b.cash == 0 && b.reserved == 0);

    ledger_core_destroy(c);
}

/*
 * T7-01 — **걸어 둔 매수를 취소하면 호가창에서 빠지고 묶인 돈이 전부 풀린다.**
 * 예수금은 그대로다(체결이 없었으므로).
 */
static void test_cancel_releases_margin(void)
{
    ledger_core_t      *c = empty_core(16);
    const order_book_t *krx = ledger_core_book(c, MARKET_KRX);
    msg_order_ack_t     ack;

    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 20, 700, &ack) == ERR_OK);
    order_id_t id = ack.order_id;

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(reserved == (int64_t)70000 * 20);

    msg_cancel_ack_t ca;
    assert(cancel(c, ACCT, id, 701, &ca) == ERR_OK);
    assert(ca.canceled_qty == 20);
    assert(ca.status == STATUS_CANCELED);

    balance(c, &cash, &reserved);
    assert(reserved == 0);
    assert(cash == CASH);
    assert(book_qty_at(krx, SIDE_BUY, 70000) == 0);

    /* 두 번 취소하면 살아 있는 것이 없다 — 거절하고 돈을 건드리지 않는다 */
    assert(cancel(c, ACCT, id, 702, &ca) == ERR_NOT_FOUND);
    assert(ca.status == STATUS_REJECTED && ca.canceled_qty == 0);
    balance(c, &cash, &reserved);
    assert(reserved == 0 && cash == CASH);

    ledger_core_destroy(c);
}

/*
 * **일부 체결된 뒤 취소하면 남은 수량만큼만 푼다.** 20주 중 12주가 maker로 체결되면
 * 묶음은 8주분이고, 취소로 그 8주분이 풀려야 한다. 상태는 체결이 있었으므로 부분 체결.
 */
static void test_cancel_after_partial_fill(void)
{
    ledger_core_t  *c = empty_core(16);
    msg_order_ack_t ack;

    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 20, 710, &ack) == ERR_OK);
    order_id_t id = ack.order_id;
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 12, 711, &ack) == ERR_OK);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(reserved == (int64_t)70000 * 8);

    msg_cancel_ack_t ca;
    assert(cancel(c, ACCT, id, 712, &ca) == ERR_OK);
    assert(ca.canceled_qty == 8);
    assert(ca.status == STATUS_PARTIAL);

    balance(c, &cash, &reserved);
    assert(reserved == 0);
    assert(cash == CASH); /* 같은 계좌끼리 체결이라 예수금은 제자리 */

    ledger_core_destroy(c);
}

/* **매도 취소는 돈을 건드리지 않는다.** 매도는 증거금을 묶지 않았다 */
static void test_cancel_sell_keeps_cash(void)
{
    ledger_core_t  *c = empty_core(16);
    msg_order_ack_t ack;

    assert(send_order(c, SIDE_SELL, MARKET_NXT, 71000, 5, 720, &ack) == ERR_OK);
    msg_cancel_ack_t ca;
    assert(cancel(c, ACCT, ack.order_id, 721, &ca) == ERR_OK);
    assert(ca.canceled_qty == 5);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(cash == CASH && reserved == 0);
    assert(book_qty_at(ledger_core_book(c, MARKET_NXT), SIDE_SELL, 71000) == 0);

    ledger_core_destroy(c);
}

/*
 * **남의 주문, 없는 주문, 이미 끝난 주문은 거절한다.** 남의 주문을 취소할 수 있으면
 * 주문번호만 알면 누구든 남의 주문을 지운다. 어느 경우든 호가창과 돈은 그대로다.
 */
static void test_cancel_rejections(void)
{
    ledger_core_t      *c = empty_core(16);
    const order_book_t *krx = ledger_core_book(c, MARKET_KRX);
    msg_order_ack_t     ack;
    msg_cancel_ack_t    ca;

    const char *OTHER = "210987654321";
    assert(ledger_core_open_account(c, OTHER, 5000000) == ERR_OK);
    /* 이미 있는 계좌를 다시 열어 몰래 입금하지 않는다 */
    assert(ledger_core_open_account(c, ACCT, 1) == ERR_DUPLICATE);
    int64_t oc, orsv;
    assert(ledger_core_balance(c, OTHER, &oc, &orsv) == ERR_OK && oc == 5000000);

    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 10, 730, &ack) == ERR_OK);
    order_id_t mine = ack.order_id;

    /* 다른 계좌 이름으로 취소 */
    assert(cancel(c, OTHER, mine, 731, &ca) == ERR_NOT_FOUND);
    assert(ca.status == STATUS_REJECTED && ca.canceled_qty == 0);
    assert(book_qty_at(krx, SIDE_BUY, 70000) == 10);

    /* 없는 계좌, 없는 주문번호 */
    assert(cancel(c, "999999999999", mine, 732, &ca) == ERR_NOT_FOUND);
    assert(cancel(c, ACCT, mine + 1000, 733, &ca) == ERR_NOT_FOUND);
    assert(cancel(c, ACCT, 0, 734, &ca) == ERR_NOT_FOUND);

    /* 전량 체결로 끝난 주문 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 10, 735, &ack) == ERR_OK);
    assert(cancel(c, ACCT, mine, 736, &ca) == ERR_NOT_FOUND);

    int64_t cash, reserved;
    balance(c, &cash, &reserved);
    assert(cash == CASH && reserved == 0);

    ledger_core_destroy(c);
}

/* **정정은 아직 안 된다고 말한다** — 무조건 성공이라고 답하던 옛 껍데기로 돌아가지 않는다 */
static void test_modify_is_not_faked(void)
{
    ledger_core_t *c = empty_core(8);

    msg_modify_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", ACCT);
    req.order_id = 200000000;
    req.cl_ord_id = 70;
    req.new_price = 70100;
    req.new_qty = 1;

    uint8_t body[MSG_MODIFY_REQ_LEN];
    assert(msg_encode_modify_req(&req, body, sizeof(body)) ==
           (int)MSG_MODIFY_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_MODIFY_REQ;
    h.body_len = MSG_MODIFY_REQ_LEN;
    h.seq = 70;

    uint8_t out[256];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_MODIFY_ACK_LEN));

    msg_modify_ack_t ack;
    assert(msg_decode_modify_ack(out + WIRE_HEADER_LEN, MSG_MODIFY_ACK_LEN,
                                 &ack) >= 0);
    assert(ack.reason == ERR_NOT_SUPPORTED);
    assert(ack.status == STATUS_REJECTED);

    ledger_core_destroy(c);
}

/* 바디 길이가 규격과 다르면 접속을 끊으라고(음수) 답한다 */
static void test_bad_body_drops(void)
{
    ledger_core_t *c = empty_core(8);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_ORDER_REQ;
    h.body_len = MSG_ORDER_REQ_LEN - 1;

    uint8_t body[MSG_ORDER_REQ_LEN];
    memset(body, 0, sizeof(body));
    uint8_t out[256];
    assert(ledger_core_handle(&h, body, out, sizeof(out), c) < 0);

    /* 인자 */
    assert(ledger_core_handle(NULL, body, out, sizeof(out), c) < 0);
    assert(ledger_core_handle(&h, body, out, sizeof(out), NULL) < 0);
    assert(ledger_core_book(c, (market_t)MARKET_COUNT) == NULL);

    int64_t x, y;
    assert(ledger_core_balance(c, "nobody000000", &x, &y) == ERR_NOT_FOUND);

    ledger_core_destroy(c);
    ledger_core_destroy(NULL);
}

/* --- 6. 결정성 --- */

/*
 * **같은 전문 순서는 같은 응답 바이트를 만든다.** 원장이 시스템 시각이나 전역
 * 난수를 읽으면 여기서 깨진다.
 */
static void test_deterministic(void)
{
    ledger_core_t *a = liquid_core();
    ledger_core_t *b = liquid_core();

    const uint8_t sides[] = {SIDE_BUY, SIDE_SELL, SIDE_BUY, SIDE_BUY, SIDE_SELL};
    const uint8_t mkts[] = {MARKET_KRX, MARKET_NXT, MSG_MARKET_AUTO, MARKET_NXT,
                            MSG_MARKET_AUTO};
    const price_t prices[] = {70300, 69700, 70200, 69500, 70100};
    const qty_t   qtys[] = {41, 17, 29, 13, 31};

    for (int i = 0; i < 5; i++) {
        msg_order_ack_t aa, bb;
        uint8_t         ra[256], rb[256];
        int             la = 0, lb = 0;
        send_order_ex(a, ACCT, sides[i], ORDER_LIMIT, mkts[i], prices[i],
                      qtys[i], (uint64_t)(80 + i), &aa, ra, &la);
        send_order_ex(b, ACCT, sides[i], ORDER_LIMIT, mkts[i], prices[i],
                      qtys[i], (uint64_t)(80 + i), &bb, rb, &lb);
        assert(la == lb);
        assert(memcmp(ra, rb, (size_t)la) == 0);
    }

    int64_t ca, qa, cb, qb;
    balance(a, &ca, &qa);
    balance(b, &cb, &qb);
    assert(ca == cb && qa == qb);

    ledger_core_destroy(a);
    ledger_core_destroy(b);
}

/*
 * T8-01 — 틱을 치면 호가창이 움직이고, 치지 않으면 예전 그대로다.
 */
static void snapshot_book(ledger_core_t *c, price_t *bid, price_t *ask,
                          qty_t *bid_qty, qty_t *ask_qty)
{
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        const order_book_t *ob = ledger_core_book(c, (market_t)m);
        assert(ob != NULL);
        bid[m] = book_best_bid(ob);
        ask[m] = book_best_ask(ob);
        bid_qty[m] = book_qty_at(ob, SIDE_BUY, bid[m]);
        ask_qty[m] = book_qty_at(ob, SIDE_SELL, ask[m]);
    }
}

static void test_tick_moves_book(void)
{
    ledger_core_t *c = liquid_core();

    price_t b0[MARKET_COUNT], a0[MARKET_COUNT];
    qty_t   bq0[MARKET_COUNT], aq0[MARKET_COUNT];
    snapshot_book(c, b0, a0, bq0, aq0);

    for (int i = 0; i < 20; i++) {
        assert(ledger_core_tick(c, 5) == ERR_OK);
    }

    price_t b1[MARKET_COUNT], a1[MARKET_COUNT];
    qty_t   bq1[MARKET_COUNT], aq1[MARKET_COUNT];
    snapshot_book(c, b1, a1, bq1, aq1);

    /* 시장마다 최우선호가나 그 잔량 중 하나는 달라져야 한다 */
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        assert(b1[m] != b0[m] || a1[m] != a0[m] || bq1[m] != bq0[m] ||
               aq1[m] != aq0[m]);
    }

    ledger_core_destroy(c);
}

/* 같은 시드에 같은 틱 횟수면 같은 호가창이다 — 결정성은 그대로다. */
static void test_tick_is_deterministic(void)
{
    ledger_core_t *a = liquid_core();
    ledger_core_t *b = liquid_core();

    for (int i = 0; i < 10; i++) {
        assert(ledger_core_tick(a, 3) == ERR_OK);
        assert(ledger_core_tick(b, 3) == ERR_OK);
    }

    price_t ba[MARKET_COUNT], aa[MARKET_COUNT], bb[MARKET_COUNT],
        ab[MARKET_COUNT];
    qty_t bqa[MARKET_COUNT], aqa[MARKET_COUNT], bqb[MARKET_COUNT],
        aqb[MARKET_COUNT];
    snapshot_book(a, ba, aa, bqa, aqa);
    snapshot_book(b, bb, ab, bqb, aqb);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        assert(ba[m] == bb[m] && aa[m] == ab[m]);
        assert(bqa[m] == bqb[m] && aqa[m] == aqb[m]);
    }

    ledger_core_destroy(a);
    ledger_core_destroy(b);
}

/* 틱을 치지 않은 코어는 시드 유동성 그대로다 — 기존 테스트와 bench의 전제. */
static void test_no_tick_keeps_book(void)
{
    ledger_core_t *a = liquid_core();
    ledger_core_t *b = liquid_core();
    for (int i = 0; i < 10; i++) {
        assert(ledger_core_tick(b, 3) == ERR_OK);
    }
    ledger_core_destroy(b);

    /* b를 아무리 틱쳐도 a는 처음 그대로여야 한다 */
    ledger_core_t *fresh = liquid_core();
    price_t        ba[MARKET_COUNT], aa[MARKET_COUNT], bf[MARKET_COUNT],
        af[MARKET_COUNT];
    qty_t bqa[MARKET_COUNT], aqa[MARKET_COUNT], bqf[MARKET_COUNT],
        aqf[MARKET_COUNT];
    snapshot_book(a, ba, aa, bqa, aqa);
    snapshot_book(fresh, bf, af, bqf, aqf);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        assert(ba[m] == bf[m] && aa[m] == af[m]);
        assert(bqa[m] == bqf[m] && aqa[m] == aqf[m]);
    }

    ledger_core_destroy(a);
    ledger_core_destroy(fresh);
}

/* 오래 돌아도 주문 풀이 마르지 않는다 — 가장 오래된 가상 호가부터 걷는다. */
static void test_tick_retires_old_orders(void)
{
    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT;
    cfg.liquidity_per_market = 50;
    cfg.order_capacity = 16;
    ledger_core_t *c = ledger_core_create(&cfg);
    assert(c != NULL);

    /* 유동성 수의 40배를 낸다. 걷지 않으면 풀(=50 + 16*다리 + 64)이 넘친다 */
    for (int i = 0; i < 200; i++) {
        assert(ledger_core_tick(c, 10) == ERR_OK);
    }

    /* 여전히 양쪽 호가가 살아 있어야 한다 */
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        const order_book_t *ob = ledger_core_book(c, (market_t)m);
        assert(book_best_bid(ob) > 0);
        assert(book_best_ask(ob) > 0);
    }

    ledger_core_destroy(c);
}

/* 유동성 0으로 만든 코어는 생성기가 없다. */
static void test_tick_without_generator(void)
{
    ledger_core_t *c = empty_core(16);
    assert(ledger_core_tick(c, 1) == ERR_NOT_SUPPORTED);
    ledger_core_destroy(c);
}

/*
 * 틱이 사용자 미체결을 체결시키면 조회와 정산에 반영된다.
 *
 * 가상 참가자는 **기준가를 넘지 않는다** — 매수는 기준가 아래, 매도는 위에 놓는다
 * (`synthetic.c`의 price_at_offset). 그래서 기준가에 걸어 둔 매수가 기준가로
 * 내려온 가상 매도에 맞는 경우가 이 계층에서 tick이 만드는 maker 체결이다.
 * 실호가를 심는 T8-03에서는 교차하는 호가도 들어온다.
 */
static void test_tick_fills_resting_user_order(void)
{
    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT;
    cfg.liquidity_per_market = 20; /* 앞에 선 가상 잔량을 적게 둔다 */
    cfg.order_capacity = 64;
    ledger_core_t *c = ledger_core_create(&cfg);
    assert(c != NULL);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MARKET_KRX, cfg.ref_price, 10, 900, &ack) ==
           ERR_OK);
    assert(ack.status == STATUS_NEW); /* 바로 체결되지 않고 걸렸다 */
    order_id_t resting = ack.order_id;

    int64_t cash0, res0;
    balance(c, &cash0, &res0);
    assert(res0 == (int64_t)cfg.ref_price * 10); /* 지정가 x 수량이 묶였다 */

    for (int i = 0; i < 2000; i++) {
        assert(ledger_core_tick(c, 2) == ERR_OK);

        msg_query_ack_t q;
        query(c, resting, &q);
        if (q.filled_qty > 0) {
            int64_t cash, res;
            balance(c, &cash, &res);
            /* 체결분만큼 예수금이 나가고 묶인 돈이 풀렸다 */
            assert(cash == cash0 - (int64_t)cfg.ref_price * q.filled_qty);
            assert(res == res0 - (int64_t)cfg.ref_price * q.filled_qty);
            ledger_core_destroy(c);
            return;
        }
    }
    assert(0 && "가상 매도가 2000틱 안에 기준가까지 내려오지 않았다");
}

/* --- 8. 호가 스냅샷 주입 (T8-03) --- */

/* {가격, 잔량} 쌍을 스냅샷 한 면에 채운다. 남는 단은 0으로 둔다. */
static void fill_side(price_t *price, qty_t *qty, const price_t *p,
                      const qty_t *q, int n)
{
    for (int i = 0; i < n && i < MSG_BOOK_DEPTH; i++) {
        price[i] = p[i];
        qty[i] = q[i];
    }
}

static void feed_init(msg_book_feed_t *f, uint8_t market, ts_t ts)
{
    memset(f, 0, sizeof(*f));
    snprintf(f->symbol, sizeof(f->symbol), "%s", "005930");
    f->market = market;
    f->feed_ts = ts;
}

/*
 * **종목을 바꾸는 방법은 원장을 다시 여는 것뿐이다**(T8-10).
 *
 * 호가창은 만들 때 정한 기준가 ±30%만 펼쳐 둔다. 260,000원짜리 종목의 실호가를
 * 70,000원 기준가로 만든 호가창에 심으면 통째로 버려진다 — 화면에서는 아무 일도
 * 일어나지 않는다. 실제로 그렇게 됐다(T8-05). 이 테스트가 그 전제를 고정한다.
 *
 * `ledgerd`의 종목 전환(`rebase_symbol`)이 하는 일이 정확히 이것이다.
 */
static void test_rebase_changes_band_and_symbol(void)
{
    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT;
    cfg.liquidity_per_market = 0;
    cfg.order_capacity = 16;

    ledger_core_t *c = ledger_core_create(&cfg);
    assert(c != NULL);

    /* 70,000원 기준가의 호가창은 260,000원을 받지 못한다 */
    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 1000);
    f.bid_price[0] = 260000;
    f.bid_qty[0] = 10;
    assert(ledger_core_apply_feed(c, &f) == ERR_OK); /* 전문 자체는 받는다 */

    msg_book_ack_t b;
    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[0] == 0); /* 가격대 밖이라 들어가지 않았다 */

    /* 종목과 기준가를 바꿔 다시 연다 */
    ledger_core_destroy(c);
    cfg.symbol = "000660";
    cfg.ref_price = 260000;
    c = ledger_core_create(&cfg);
    assert(c != NULL);

    /* 이제는 앞 종목의 스냅샷을 거절한다 */
    assert(ledger_core_apply_feed(c, &f) == ERR_NOT_FOUND);

    snprintf(f.symbol, sizeof(f.symbol), "%s", "000660");
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    book(c, "000660", MARKET_KRX, &b);
    assert(b.bid_price[0] == 260000 && b.bid_qty[0] == 10);

    /* 앞 종목으로는 이제 조회되지 않는다 */
    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[0] == 0);

    ledger_core_destroy(c);
}

/*
 * **상대 호가만 갈아끼우고 내 미체결은 그대로 둔다.**
 *
 * 스냅샷을 두 번 넣는 동안 내 주문이 살아 있어야 하고, 같은 가격의 잔량은
 * "스냅샷이 말한 잔량 + 내 주문"이어야 한다.
 */
static void test_feed_keeps_my_order(void)
{
    ledger_core_t *c = empty_core(64);
    msg_book_ack_t b;

    /* 1) 첫 스냅샷 — 내 주문이 아직 없다 */
    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 1000);
    const price_t bp[] = {69900, 69800};
    const qty_t   bq[] = {100, 50};
    const price_t ap[] = {70000};
    const qty_t   aq[] = {80};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 2);
    fill_side(f.ask_price, f.ask_qty, ap, aq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[0] == 69900 && b.bid_qty[0] == 100);
    assert(b.bid_price[1] == 69800 && b.bid_qty[1] == 50);
    assert(b.ask_price[0] == 70000 && b.ask_qty[0] == 80);

    /* 2) 내 주문을 같은 가격에 건다 — 기존 잔량 뒤에 선다 */
    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 69900, 10, 800, &ack) == ERR_OK);
    assert(ack.status == STATUS_NEW);
    order_id_t mine = ack.order_id;

    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[0] == 69900 && b.bid_qty[0] == 110); /* 100 + 내 10 */

    /* 3) 두 번째 스냅샷 — 69,800이 사라지고 69,900은 그대로다 */
    feed_init(&f, MARKET_KRX, 2000);
    const price_t bp2[] = {69900};
    const qty_t   bq2[] = {100};
    fill_side(f.bid_price, f.bid_qty, bp2, bq2, 1);
    fill_side(f.ask_price, f.ask_qty, ap, aq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[0] == 69900 && b.bid_qty[0] == 110); /* 내 주문 보존 */
    assert(b.bid_price[1] == 0);                            /* 69,800은 걷혔다 */

    msg_detail_ack_t d;
    assert(detail(c, ACCT, mine, &d) == ERR_OK);
    assert(d.working == 10 && d.filled == 0);

    ledger_core_destroy(c);
}

/*
 * **큐 위치가 유지된다.** 내 앞의 100주가 먼저 체결되고, 그것이 빠진 뒤에야
 * 내 주문이 체결된다. 지웠다 다시 넣는 방식이면 내가 맨 앞에 서서 첫 매도
 * 100주가 내 10주를 먼저 먹는다 — 그것이 체결률을 부풀리는 지점이다.
 */
static void test_feed_preserves_queue_position(void)
{
    ledger_core_t *c = empty_core(64);

    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 1000);
    const price_t bp[] = {69900};
    const qty_t   bq[] = {100};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 69900, 10, 810, &ack) == ERR_OK);
    order_id_t mine = ack.order_id;

    /* 같은 스냅샷을 한 번 더 — 잔량이 같으므로 아무것도 건드리지 않는다 */
    feed_init(&f, MARKET_KRX, 2000);
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    /* 앞 100주를 걷어 가는 매도 — 내 주문은 한 주도 체결되지 않아야 한다 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 69900, 100, 811, &ack) ==
           ERR_OK);
    msg_detail_ack_t d;
    assert(detail(c, ACCT, mine, &d) == ERR_OK);
    assert(d.filled == 0 && d.working == 10);

    msg_book_ack_t b;
    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[0] == 69900 && b.bid_qty[0] == 10); /* 내 것만 남았다 */

    /* 이제 내 차례다 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 69900, 10, 812, &ack) == ERR_OK);
    assert(detail(c, ACCT, mine, &d) == ERR_OK);
    assert(d.filled == 10 && d.working == 0);

    ledger_core_destroy(c);
}

/* 실호가가 내 미체결과 교차하면 그 자리에서 체결된다. */
static void test_feed_crosses_my_order(void)
{
    ledger_core_t *c = empty_core(64);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 10, 820, &ack) == ERR_OK);
    assert(ack.status == STATUS_NEW);
    order_id_t mine = ack.order_id;

    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 3000);
    const price_t bp[] = {70000};
    const qty_t   bq[] = {30};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    msg_detail_ack_t d;
    assert(detail(c, ACCT, mine, &d) == ERR_OK);
    assert(d.filled == 10 && d.working == 0);

    msg_book_ack_t b;
    book(c, "005930", MARKET_KRX, &b);
    assert(b.ask_price[0] == 0);                          /* 내 매도는 다 나갔다 */
    assert(b.bid_price[0] == 70000 && b.bid_qty[0] == 20); /* 남은 20주가 선다 */

    ledger_core_destroy(c);
}

/*
 * **빈 스냅샷은 상대 호가를 전부 걷는다.** 단수가 10단 고정이 아니라는 것을
 * 다루는 경계다 — 토스 호가 스키마에는 단수 상한이 없고 예시가 3단·1단이다.
 * 내 주문은 그래도 남는다.
 */
static void test_feed_empty_clears_only_theirs(void)
{
    ledger_core_t *c = empty_core(64);

    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 1000);
    const price_t bp[] = {69900};
    const qty_t   bq[] = {100};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 69800, 7, 830, &ack) == ERR_OK);

    feed_init(&f, MARKET_KRX, 2000); /* 전부 0 */
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    msg_book_ack_t b;
    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[0] == 69800 && b.bid_qty[0] == 7);
    assert(b.bid_price[1] == 0 && b.ask_price[0] == 0);

    ledger_core_destroy(c);
}

/* 시드 유동성도 상대 호가다 — 스냅샷이 그 자리를 넘겨받는다. */
static void test_feed_replaces_seeded_liquidity(void)
{
    ledger_core_t *c = liquid_core();

    msg_book_feed_t f;
    feed_init(&f, MARKET_NXT, 5000);
    const price_t bp[] = {69000};
    const qty_t   bq[] = {11};
    const price_t ap[] = {71000};
    const qty_t   aq[] = {13};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    fill_side(f.ask_price, f.ask_qty, ap, aq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    msg_book_ack_t b;
    book(c, "005930", MARKET_NXT, &b);
    assert(b.bid_price[0] == 69000 && b.bid_qty[0] == 11);
    assert(b.bid_price[1] == 0);
    assert(b.ask_price[0] == 71000 && b.ask_qty[0] == 13);
    assert(b.ask_price[1] == 0);

    /* 다른 시장은 건드리지 않는다 */
    book(c, "005930", MARKET_KRX, &b);
    assert(b.bid_price[1] != 0 || b.ask_price[1] != 0);

    ledger_core_destroy(c);
}

/* 같은 스냅샷 묶음은 같은 호가창을 만든다 — 리플레이(T8-06)의 전제다. */
static void test_feed_is_deterministic(void)
{
    ledger_core_t *a = empty_core(64);
    ledger_core_t *c = empty_core(64);

    for (int round = 0; round < 3; round++) {
        msg_book_feed_t f;
        feed_init(&f, MARKET_KRX, 1000 + round);
        const price_t bp[] = {69900 - round * 100, 69800 - round * 100};
        const qty_t   bq[] = {100 + round, 50};
        fill_side(f.bid_price, f.bid_qty, bp, bq, 2);
        assert(ledger_core_apply_feed(a, &f) == ERR_OK);
        assert(ledger_core_apply_feed(c, &f) == ERR_OK);
    }

    msg_book_ack_t ba, bc;
    book(a, "005930", MARKET_KRX, &ba);
    book(c, "005930", MARKET_KRX, &bc);
    assert(memcmp(&ba, &bc, sizeof(ba)) == 0);

    ledger_core_destroy(a);
    ledger_core_destroy(c);
}

/* 모르는 시장·다루지 않는 종목은 거절한다. */
static void test_feed_rejections(void)
{
    ledger_core_t *c = empty_core(64);

    msg_book_feed_t f;
    feed_init(&f, 7, 1000);
    assert(ledger_core_apply_feed(c, &f) == ERR_INVALID_ARG);

    feed_init(&f, MARKET_KRX, 1000);
    snprintf(f.symbol, sizeof(f.symbol), "%s", "000660");
    assert(ledger_core_apply_feed(c, &f) == ERR_NOT_FOUND);

    assert(ledger_core_apply_feed(NULL, &f) == ERR_NULL_PTR);
    assert(ledger_core_apply_feed(c, NULL) == ERR_NULL_PTR);

    ledger_core_destroy(c);
}

/*
 * **바깥 시세를 받은 시장에는 가상 참가자가 더 끼어들지 않는다.**
 *
 * 실호가 위에 가짜 주문을 계속 얹으면 그건 실시세도 시뮬도 아니다. 스냅샷이 말한 잔량이
 * 틱을 아무리 쳐도 그대로여야 하고, 스냅샷을 받지 않은 시장은 계속 움직여야 한다.
 */
static void test_feed_stops_ticks_on_that_market(void)
{
    ledger_core_t *c = liquid_core();

    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 1000);
    const price_t bp[] = {69000};
    const qty_t   bq[] = {11};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    msg_book_ack_t krx0, nxt0;
    book(c, "005930", MARKET_KRX, &krx0);
    book(c, "005930", MARKET_NXT, &nxt0);

    for (int i = 0; i < 20; i++) {
        assert(ledger_core_tick(c, 5) == ERR_OK);
    }

    msg_book_ack_t krx1, nxt1;
    book(c, "005930", MARKET_KRX, &krx1);
    book(c, "005930", MARKET_NXT, &nxt1);

    assert(memcmp(&krx0, &krx1, sizeof(krx0)) == 0); /* 실호가는 그대로 */
    assert(memcmp(&nxt0, &nxt1, sizeof(nxt0)) != 0); /* 안 받은 시장은 움직인다 */

    ledger_core_destroy(c);
}

/*
 * **체결을 한 번만 센다**(T8-09).
 *
 * 매칭 엔진은 한 체결에 사는 쪽·파는 쪽 양쪽으로 이벤트를 준다. 그대로 더하면 거래량이
 * 정확히 두 배가 되고, 그 위에 만든 봉은 전부 틀린다.
 */
static void test_tape_counts_each_trade_once(void)
{
    ledger_core_t *c = empty_core(64);
    msg_order_ack_t ack;
    msg_book_ack_t  b;

    book(c, "005930", MARKET_KRX, &b);
    assert(b.last_price == 0 && b.traded_qty == 0); /* 아직 한 건도 없다 */

    /* 매도를 걸어 두고 매수로 20주를 먹는다 — 체결은 20주 한 번이다 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 70000, 20, 900, &ack) == ERR_OK);
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 70000, 20, 901, &ack) == ERR_OK);

    book(c, "005930", MARKET_KRX, &b);
    assert(b.last_price == 70000);
    assert(b.traded_qty == 20); /* 40이면 양쪽을 다 센 것이다 */

    /* 다른 시장은 따로 센다 */
    msg_book_ack_t n;
    book(c, "005930", MARKET_NXT, &n);
    assert(n.traded_qty == 0);

    /* 더 체결하면 누적된다 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 69900, 5, 902, &ack) == ERR_OK);
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 69900, 5, 903, &ack) == ERR_OK);
    book(c, "005930", MARKET_KRX, &b);
    assert(b.traded_qty == 25 && b.last_price == 69900);

    ledger_core_destroy(c);
}

/*
 * **가상 참가자끼리의 체결도 거래량이다.** 그것이 이 시장에서 실제로 일어난 거래다.
 */
static void test_tape_counts_synthetic_trades(void)
{
    ledger_core_t *c = liquid_core();

    msg_book_ack_t before;
    book(c, "005930", MARKET_KRX, &before);

    for (int i = 0; i < 600; i++) {
        assert(ledger_core_tick(c, 2) == ERR_OK);
    }

    msg_book_ack_t after;
    book(c, "005930", MARKET_KRX, &after);
    assert(after.traded_qty > before.traded_qty);
    assert(after.last_price > 0);

    ledger_core_destroy(c);
}

/*
 * **가상 참가자의 기준가가 표류해 최우선호가가 움직인다**(T8-08).
 *
 * 기준가를 고정해 두면 잔량만 출렁이고 가격은 붙박이가 된다 — 실측으로 확인했다
 * (20초 동안 260,000 / 259,500에서 한 번도 안 움직였다). 그러면 "가격 차트"가 평평해서
 * 시장이 죽은 것처럼 보인다. 몇 틱에 한 번 중심을 한 호가 단위 옮긴다.
 */
static void test_tick_drifts_price(void)
{
    ledger_core_t *c = liquid_core();

    msg_book_ack_t before;
    book(c, "005930", MARKET_KRX, &before);

    /* 표류는 몇 틱에 한 번이므로 넉넉히 돌린다 */
    for (int i = 0; i < 400; i++) {
        assert(ledger_core_tick(c, 2) == ERR_OK);
    }

    msg_book_ack_t after;
    book(c, "005930", MARKET_KRX, &after);

    /* **가격**이 움직여야 한다. 잔량만 바뀌는 것으로는 부족하다 */
    assert(after.bid_price[0] != before.bid_price[0] ||
           after.ask_price[0] != before.ask_price[0]);

    ledger_core_destroy(c);
}

/* 표류해도 결정적이다 — 같은 시드에 같은 틱 횟수면 같은 호가창이다. */
static void test_drift_is_deterministic(void)
{
    ledger_core_t *a = liquid_core();
    ledger_core_t *b = liquid_core();

    for (int i = 0; i < 300; i++) {
        assert(ledger_core_tick(a, 2) == ERR_OK);
        assert(ledger_core_tick(b, 2) == ERR_OK);
    }

    for (uint8_t m = 0; m < MARKET_COUNT; m++) {
        msg_book_ack_t x, y;
        book(a, "005930", m, &x);
        book(b, "005930", m, &y);
        assert(memcmp(&x, &y, sizeof(x)) == 0);
    }

    ledger_core_destroy(a);
    ledger_core_destroy(b);
}

/*
 * **두 시장이 따로 표류한다.** 같이 움직이면 가격 차이가 생기지 않아 SOR이 고를 것이 없다.
 */
static void test_markets_drift_apart(void)
{
    ledger_core_t *c = liquid_core();

    bool differed = false;
    for (int i = 0; i < 400 && !differed; i++) {
        assert(ledger_core_tick(c, 2) == ERR_OK);
        msg_book_ack_t krx, nxt;
        book(c, "005930", MARKET_KRX, &krx);
        book(c, "005930", MARKET_NXT, &nxt);
        if (krx.bid_price[0] != nxt.bid_price[0] ||
            krx.ask_price[0] != nxt.ask_price[0]) {
            differed = true;
        }
    }
    assert(differed);

    ledger_core_destroy(c);
}

/*
 * **가상 참가자는 `MSG_FEED_END`를 받아야 돌아온다.**
 *
 * 스냅샷이 잠시 안 오는 것(장 마감)과 피드가 끝난 것은 겉으로 같다. 시간으로 어림해
 * 스스로 풀면 장 마감에 가상 참가자가 슬그머니 돌아와 **실시세인 척하는 시뮬**이 된다 —
 * 화면에는 "실시세"라고 적혀 있는데 움직이는 것은 내 가짜 주문이다. 실제로 그렇게 됐다.
 *
 * 끝을 알리면 호가창은 **그대로 두고** 가상 참가자만 돌아온다.
 */
/*
 * **실호가를 받는 동안에는 받지 않는 시장도 그 시세를 따른다**(T8-08 보완).
 *
 * 받지 않는 시장이 제 표류를 이어 가면 두 시장이 몇 %씩 벌어지고, 피드가 끝나도 벌어진
 * 채로 남는다 — 실측으로 확인했다(실시세 10분 뒤 KRX 259,500원 · NXT 231,000원).
 * 그 상태에서는 SOR이 늘 한 시장만 고르므로 전략 비교가 무의미해진다.
 */
static void test_unfed_market_follows_feed(void)
{
    ledger_core_t *c = liquid_core();

    /* 기준가(70,000원)에서 멀찍이 떨어진 실호가를 KRX에만 심는다 */
    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 1000);
    const price_t bp[] = {85000};
    const qty_t   bq[] = {500};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    for (int i = 0; i < 600; i++) {
        assert(ledger_core_tick(c, 2) == ERR_OK);
    }

    msg_book_ack_t nxt;
    book(c, "005930", MARKET_NXT, &nxt);
    assert(nxt.bid_price[0] > 0);

    /* 실호가 둘레에 있어야 한다 — 70,000원 언저리에 남아 있으면 따라오지 못한 것이다 */
    price_t gap = nxt.bid_price[0] > 85000 ? nxt.bid_price[0] - 85000 : 85000 - nxt.bid_price[0];
    assert(gap < 85000 / 50); /* 2% 안 */

    ledger_core_destroy(c);
}

static void test_ticks_resume_only_on_feed_end(void)
{
    ledger_core_t *c = liquid_core();

    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 1000);
    const price_t bp[] = {69000};
    const qty_t   bq[] = {11};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    msg_book_ack_t frozen;
    book(c, "005930", MARKET_KRX, &frozen);
    assert(frozen.bid_price[0] == 69000 && frozen.bid_qty[0] == 11);

    /* **아무리 오래 조용해도** 스스로 풀리지 않는다 — 장 마감이 그렇게 보인다 */
    for (int i = 0; i < 600; i++) {
        assert(ledger_core_tick(c, 1) == ERR_OK);
    }
    msg_book_ack_t still;
    book(c, "005930", MARKET_KRX, &still);
    assert(memcmp(&frozen, &still, sizeof(frozen)) == 0);

    /* 끝을 알리면 호가창은 그대로 두고 가상 참가자만 돌아온다 */
    msg_book_feed_t end;
    feed_init(&end, MARKET_KRX, 3000);
    end.flags = MSG_FEED_END;
    assert(ledger_core_apply_feed(c, &end) == ERR_OK);

    msg_book_ack_t kept;
    book(c, "005930", MARKET_KRX, &kept);
    assert(memcmp(&frozen, &kept, sizeof(frozen)) == 0); /* 마지막 실호가가 남는다 */

    for (int i = 0; i < 50; i++) {
        assert(ledger_core_tick(c, 1) == ERR_OK);
    }
    msg_book_ack_t alive;
    book(c, "005930", MARKET_KRX, &alive);
    assert(memcmp(&frozen, &alive, sizeof(frozen)) != 0);

    ledger_core_destroy(c);
}

/*
 * **깊은 곳에 묵은 호가가 남지 않는다.**
 *
 * 조회 응답은 10단뿐이라 그 아래에 남은 호가는 화면에 보이지 않는다. 나중에 실호가가
 * 그쪽으로 내려오면 있지도 않은 체결이 난다. 그래서 조회가 아니라 **호가창 전체**를 본다.
 *
 * 훑는 상한(`LEDGER_FEED_SCAN_DEPTH`, 32단)이 모자랄 수 있으므로 `apply_feed`는 한 바퀴
 * 걷고 다시 훑기를 되풀이한다. 지금 생성기로는 상한에 닿지 않는다 — 지수 분포가 기준가
 * 근처로 몰려 시장당 6,000건을 넣어도 한 방향이 15단이었다. 되풀이를 남기는 이유는
 * 그 사실이 생성기 설정에 달려 있고, 모자랄 때의 실패가 **조용하기** 때문이다.
 */
static void test_feed_clears_deep_levels(void)
{
    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT; /* ledgerd가 실제로 쓰는 설정 */
    ledger_core_t       *c = ledger_core_create(&cfg);
    assert(c != NULL);

    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 9000);
    const price_t bp[] = {69900};
    const qty_t   bq[] = {7};
    const price_t ap[] = {70100};
    const qty_t   aq[] = {9};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);
    fill_side(f.ask_price, f.ask_qty, ap, aq, 1);
    assert(ledger_core_apply_feed(c, &f) == ERR_OK);

    /* 조회 응답(10단)이 아니라 **호가창 전체**를 본다 — 깊은 단이 남았는지가 요점이다 */
    const order_book_t *book = ledger_core_book(c, MARKET_KRX);
    level_view_t        view[256];
    int                 n = book_snapshot(book, SIDE_BUY, 256, view);
    assert(n == 1 && view[0].price == 69900 && view[0].total_qty == 7);
    n = book_snapshot(book, SIDE_SELL, 256, view);
    assert(n == 1 && view[0].price == 70100 && view[0].total_qty == 9);

    ledger_core_destroy(c);
}

/* 전문으로 넣으면 **심은 뒤의 호가창**이 응답으로 온다. */
static void test_feed_message_answers_with_book(void)
{
    ledger_core_t *c = empty_core(64);

    msg_book_feed_t f;
    feed_init(&f, MARKET_KRX, 1000);
    const price_t bp[] = {69900};
    const qty_t   bq[] = {40};
    fill_side(f.bid_price, f.bid_qty, bp, bq, 1);

    uint8_t body[MSG_BOOK_FEED_LEN];
    assert(msg_encode_book_feed(&f, body, sizeof(body)) ==
           (int)MSG_BOOK_FEED_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_BOOK_FEED;
    h.body_len = MSG_BOOK_FEED_LEN;
    h.seq = 77;

    uint8_t out[WIRE_HEADER_LEN + MSG_BOOK_ACK_LEN];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_BOOK_ACK_LEN));

    wire_header_t rh;
    assert(wire_decode_header(out, (size_t)n, &rh) == (int)WIRE_HEADER_LEN);
    assert(rh.type == MSG_BOOK_ACK && rh.seq == 77);

    msg_book_ack_t ack;
    assert(msg_decode_book_ack(out + WIRE_HEADER_LEN, rh.body_len, &ack) ==
           (int)MSG_BOOK_ACK_LEN);
    assert(ack.market == MARKET_KRX);
    assert(ack.bid_price[0] == 69900 && ack.bid_qty[0] == 40);

    /* 길이가 규격과 다른 바디는 접속을 끊는다 */
    h.body_len = MSG_BOOK_FEED_LEN - 1;
    assert(ledger_core_handle(&h, body, out, sizeof(out), c) < 0);

    ledger_core_destroy(c);
}


/*
 * 계좌 개설 전문(T9-01).
 *
 * 사용자마다 계좌가 하나씩 생기므로 **같은 요청이 두 번 와도 돈이 불어나면 안 된다** —
 * 채널계는 로그인할 때마다 이것을 보낸다.
 */
static void test_account_open_is_idempotent(void)
{
    ledger_core_t *c = empty_core(64);

    msg_account_open_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", "u00000000007");
    req.cash = 5000000;

    uint8_t body[MSG_ACCOUNT_OPEN_LEN];
    assert(msg_encode_account_open(&req, body, sizeof(body)) ==
           (int)MSG_ACCOUNT_OPEN_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_ACCOUNT_OPEN;
    h.body_len = MSG_ACCOUNT_OPEN_LEN;
    h.seq = 11;

    uint8_t out[WIRE_HEADER_LEN + MSG_ACCOUNT_ACK_LEN];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_ACCOUNT_ACK_LEN));

    wire_header_t rh;
    assert(wire_decode_header(out, (size_t)n, &rh) == (int)WIRE_HEADER_LEN);
    assert(rh.type == MSG_ACCOUNT_ACK && rh.seq == 11);

    msg_account_ack_t ack;
    assert(msg_decode_account_ack(out + WIRE_HEADER_LEN, rh.body_len, &ack) ==
           (int)MSG_ACCOUNT_ACK_LEN);
    assert(ack.code == ERR_OK);
    assert(strcmp(ack.account, "u00000000007") == 0);
    assert(ack.cash == 5000000 && ack.reserved == 0);

    /* 두 번째 개설 — 열린 계좌를 그대로 답하고 입금은 하지 않는다 */
    h.seq = 12;
    n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_ACCOUNT_ACK_LEN));
    assert(msg_decode_account_ack(out + WIRE_HEADER_LEN, MSG_ACCOUNT_ACK_LEN,
                                  &ack) == (int)MSG_ACCOUNT_ACK_LEN);
    assert(ack.code == ERR_OK);
    assert(ack.cash == 5000000); /* 1000만 원이 되지 않는다 */

    /* 길이가 규격과 다른 바디는 접속을 끊는다 */
    h.body_len = MSG_ACCOUNT_OPEN_LEN - 1;
    assert(ledger_core_handle(&h, body, out, sizeof(out), c) < 0);

    ledger_core_destroy(c);
}

/* 새로 연 계좌로 실제 주문이 나가고, 데모 계좌는 그대로다. */
static void test_opened_account_can_trade(void)
{
    ledger_core_t *c = empty_core(64);

    assert(ledger_core_open_account(c, "u00000000008", 10000000) == ERR_OK);

    msg_order_ack_t ack;
    assert(send_order_ex(c, "u00000000008", SIDE_BUY, ORDER_LIMIT, MARKET_KRX,
                         69000, 10, 1, &ack, NULL, NULL) == ERR_OK);

    int64_t cash = 0, reserved = 0;
    assert(ledger_core_balance(c, "u00000000008", &cash, &reserved) == ERR_OK);
    assert(cash - reserved < 10000000); /* 증거금이 묶였다 */

    /* 처음부터 있던 데모 계좌는 이 주문에 영향받지 않는다 */
    int64_t demo_cash = 0, demo_reserved = 0;
    assert(ledger_core_balance(c, LEDGER_CORE_DEFAULT.account, &demo_cash,
                               &demo_reserved) == ERR_OK);
    assert(demo_reserved == 0);

    ledger_core_destroy(c);
}

/* AAPL 지정가 매수 한 건. 종목을 인자로 받는 헬퍼가 없어 전문을 직접 만든다. */
static int send_us(ledger_core_t *c, price_t price, uint64_t cl)
{
    msg_order_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", ACCT);
    snprintf(req.symbol, sizeof(req.symbol), "%s", "AAPL");
    req.cl_ord_id = cl;
    req.side = SIDE_BUY;
    req.type = ORDER_LIMIT;
    req.market = MARKET_KRX;
    req.price = price;
    req.qty = 5;

    uint8_t body[MSG_ORDER_REQ_LEN];
    assert(msg_encode_order_req(&req, body, sizeof(body)) ==
           (int)MSG_ORDER_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_ORDER_REQ;
    h.body_len = MSG_ORDER_REQ_LEN;
    h.seq = cl;

    uint8_t out[256];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_ORDER_ACK_LEN));

    msg_order_ack_t ack;
    assert(msg_decode_order_ack(out + WIRE_HEADER_LEN, MSG_ORDER_ACK_LEN, &ack) >= 0);
    return ack.reason;
}

/*
 * 미국 종목(T10-01).
 *
 * 가격은 **센트 정수**다. AAPL $191.23 = 19,123센트. 국내 호가 단위 표로 열면
 * 이 값이 "50원 단위" 구간(20,000 미만은 10원)에 걸려 **정상 가격이 거절된다.**
 * 그것이 표를 둘로 나눈 이유다.
 */
static void test_us_symbol_takes_cent_prices(void)
{
    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT;
    cfg.liquidity_per_market = 0;
    cfg.order_capacity = 64;
    cfg.symbol = "AAPL";
    cfg.ref_price = 19123; /* $191.23 */
    cfg.tick_table = TICK_TABLE_US;
    cfg.cash = 10000000; /* $100,000 */

    ledger_core_t *c = ledger_core_create(&cfg);
    assert(c != NULL);
    assert(ledger_core_tick_table(c) == TICK_TABLE_US);

    /* 1센트 단위 가격이 그대로 통한다 */
    assert(send_us(c, 19123, 1) == ERR_OK);
    assert(send_us(c, 19124, 2) == ERR_OK);

    ledger_core_destroy(c);

    /* 같은 가격을 국내 표 원장에 내면 호가 단위에서 걸린다 */
    cfg.tick_table = TICK_TABLE_KRX;
    ledger_core_t *kr = ledger_core_create(&cfg);
    assert(kr != NULL);
    assert(send_us(kr, 19123, 3) == ERR_INVALID_TICK);
    ledger_core_destroy(kr);
}

/* 보유가 없는 계좌. 포지션을 보는 시험은 여기서 시작한다. */
static ledger_core_t *flat_core(void)
{
    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT;
    cfg.liquidity_per_market = 300;
    cfg.order_capacity = 256;
    ledger_core_t *c = ledger_core_create(&cfg);
    assert(c != NULL);
    return c; /* seed_shares를 부르지 않는다 */
}

/* 없는 주식은 팔 수 없다(T11-01). 이것이 없으면 공매도가 된다. */
static void test_cannot_sell_what_you_do_not_own(void)
{
    ledger_core_t *c = flat_core();

    msg_order_ack_t ack;
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 69000, 10, 1, &ack) ==
           ERR_INVALID_QTY);

    int64_t qty = -1, cost = -1, res = -1, realized = -1;
    assert(ledger_core_position(c, ACCT, &qty, &cost, &res, &realized) == ERR_OK);
    assert(qty == 0 && cost == 0 && res == 0 && realized == 0);

    ledger_core_destroy(c);
}

/*
 * 사고 판다 — 보유·평균 단가·실현 손익.
 *
 * 다 팔면 **원가가 정확히 0**이어야 한다. 평균 단가를 저장하는 방식이면 나눗셈
 * 나머지가 남아 "0주인데 원가가 있는" 계좌가 생긴다.
 */
static void test_position_and_realized_pnl(void)
{
    ledger_core_t *c = flat_core();

    /* 호가창을 때려 보유를 만든다. 비싸게 걸어 전량 체결되게 한다 */
    msg_order_ack_t buy;
    assert(send_order(c, SIDE_BUY, MARKET_KRX, 89000, 100, 1, &buy) == ERR_OK);
    assert(buy.filled_qty == 100);

    int64_t qty = 0, cost = 0, res = 0, realized = 0;
    assert(ledger_core_position(c, ACCT, &qty, &cost, &res, &realized) == ERR_OK);
    assert(qty == 100);
    assert(cost == (int64_t)buy.price * 100); /* 원가는 실제 체결가로 쌓인다 */
    assert(res == 0 && realized == 0);

    int64_t avg = cost / qty;

    /* 절반을 판다. 싸게 걸어 전량 체결되게 한다 */
    msg_order_ack_t sell;
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 49000, 50, 2, &sell) == ERR_OK);
    assert(sell.filled_qty == 50);

    assert(ledger_core_position(c, ACCT, &qty, &cost, &res, &realized) == ERR_OK);
    assert(qty == 50);
    assert(res == 0); /* 다 체결됐으므로 묶인 수량이 없다 */
    assert(realized == ((int64_t)sell.price - avg) * 50);

    /* 나머지를 판다 — 보유도 원가도 0이 된다 */
    msg_order_ack_t rest;
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 49000, 50, 3, &rest) == ERR_OK);
    assert(rest.filled_qty == 50);

    assert(ledger_core_position(c, ACCT, &qty, &cost, &res, &realized) == ERR_OK);
    assert(qty == 0);
    assert(cost == 0); /* 나머지 없이 정확히 0 */

    ledger_core_destroy(c);
}

/* 미체결 매도는 수량을 묶는다 — 같은 주식을 두 번 팔 수 없다. */
static void test_resting_sell_reserves_shares(void)
{
    ledger_core_t *c = flat_core();
    assert(ledger_core_seed_position(c, ACCT, 100, 70000LL * 100, 0) == ERR_OK);

    /* 아무도 안 사 갈 비싼 가격에 100주를 건다 */
    msg_order_ack_t ack;
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 89000, 100, 1, &ack) == ERR_OK);
    order_id_t resting = ack.order_id;

    int64_t qty = 0, cost = 0, res = 0, realized = 0;
    assert(ledger_core_position(c, ACCT, &qty, &cost, &res, &realized) == ERR_OK);
    assert(qty == 100 && res == 100); /* 전부 묶였다 */

    /* 한 주도 더 팔 수 없다 */
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 89000, 1, 2, &ack) ==
           ERR_INVALID_QTY);

    /* 취소하면 풀린다 */
    msg_cancel_ack_t cack;
    assert(cancel(c, ACCT, resting, 9, &cack) == ERR_OK);
    assert(ledger_core_position(c, ACCT, &qty, &cost, &res, &realized) == ERR_OK);
    assert(res == 0);
    assert(send_order(c, SIDE_SELL, MARKET_KRX, 89000, 100, 3, &ack) == ERR_OK);

    ledger_core_destroy(c);
}

int main(void)
{
    STEP(test_cannot_sell_what_you_do_not_own);
    STEP(test_position_and_realized_pnl);
    STEP(test_resting_sell_reserves_shares);
    STEP(test_us_symbol_takes_cent_prices);
    STEP(test_account_open_is_idempotent);
    STEP(test_opened_account_can_trade);
    STEP(test_buy_takes_liquidity);
    STEP(test_sell_takes_liquidity);
    STEP(test_resting_then_maker_fill);
    STEP(test_price_improvement_released);
    STEP(test_ioc_remainder_released);
    STEP(test_explicit_nxt_goes_to_nxt);
    STEP(test_query_reflects_maker_fill);
    STEP(test_book_query);
    STEP(test_book_query_matches_book);
    STEP(test_auto_routes_to_cheaper_market);
    STEP(test_rejects_leave_money_alone);
    STEP(test_capacity);
    STEP(test_cancel_releases_margin);
    STEP(test_cancel_after_partial_fill);
    STEP(test_cancel_sell_keeps_cash);
    STEP(test_cancel_rejections);
    STEP(test_modify_is_not_faked);
    STEP(test_detail_shows_legs);
    STEP(test_detail_tracks_later_events);
    STEP(test_detail_rejections);
    STEP(test_balance_message);
    STEP(test_bad_body_drops);
    STEP(test_deterministic);
    STEP(test_tick_moves_book);
    STEP(test_tick_is_deterministic);
    STEP(test_no_tick_keeps_book);
    STEP(test_tick_retires_old_orders);
    STEP(test_tick_without_generator);
    STEP(test_tick_fills_resting_user_order);
    STEP(test_rebase_changes_band_and_symbol);
    STEP(test_feed_keeps_my_order);
    STEP(test_feed_preserves_queue_position);
    STEP(test_feed_crosses_my_order);
    STEP(test_feed_empty_clears_only_theirs);
    STEP(test_feed_replaces_seeded_liquidity);
    STEP(test_feed_is_deterministic);
    STEP(test_feed_rejections);
    STEP(test_feed_stops_ticks_on_that_market);
    STEP(test_ticks_resume_only_on_feed_end);
    STEP(test_unfed_market_follows_feed);
    STEP(test_tape_counts_each_trade_once);
    STEP(test_tape_counts_synthetic_trades);
    STEP(test_tick_drifts_price);
    STEP(test_drift_is_deterministic);
    STEP(test_markets_drift_apart);
    STEP(test_feed_clears_deep_levels);
    STEP(test_feed_message_answers_with_book);
    return 0;
}
