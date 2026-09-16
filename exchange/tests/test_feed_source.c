/*
 * T5-06 — 호가창에서 시세 피드를 만든다.
 *
 * `core/tests/test_feed.c`는 형식(바이트 배치와 빠짐 판정)을 본다. 여기서는
 * **진짜 호가창에서 뽑은 값이 맞는지**를 본다. 형식이 옳은 것과 담긴 값이
 * 옳은 것은 다른 일이다.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "feed_source.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

#define REF 68000
#define CAP 1024
#define TS 1000

static order_t mk(order_id_t id, side_t side, price_t price, qty_t qty)
{
    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = id;
    o.side = side;
    o.price = price;
    o.qty = qty;
    o.type = ORDER_LIMIT;
    o.ts = TS;
    return o;
}

/*
 * 매수 3단, 매도 2단을 쌓는다. 가격은 호가 단위에 맞춘다 — 50000원 위
 * 구간은 100원이다. (T5-05에서 호가 단위를 안 맞춰 전부 거부된 적이 있다.)
 */
static match_engine_t *build(void)
{
    match_engine_t *eng = match_engine_create(REF, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t       o;

    /* 매수 — 높은 가격이 먼저다 */
    o = mk(1, SIDE_BUY, 67900, 137);
    assert(match_limit(eng, &o, &res) == ERR_OK);
    o = mk(2, SIDE_BUY, 67800, 211);
    assert(match_limit(eng, &o, &res) == ERR_OK);
    o = mk(3, SIDE_BUY, 67800, 43);
    assert(match_limit(eng, &o, &res) == ERR_OK);
    o = mk(4, SIDE_BUY, 67700, 89);
    assert(match_limit(eng, &o, &res) == ERR_OK);

    /* 매도 — 낮은 가격이 먼저다. 매수와 겹치지 않게 놓는다 */
    o = mk(5, SIDE_SELL, 68100, 173);
    assert(match_limit(eng, &o, &res) == ERR_OK);
    o = mk(6, SIDE_SELL, 68300, 61);
    assert(match_limit(eng, &o, &res) == ERR_OK);

    return eng;
}

/* 호가창의 값이 그대로 실린다 */
static void test_book_from_engine(void)
{
    match_engine_t *eng = build();

    feed_book_t b;
    assert(feed_book_from_engine(eng, "005930", MARKET_NXT, 5, &b) == ERR_OK);

    assert(strcmp(b.symbol, "005930") == 0);
    assert(b.market == MARKET_NXT);
    assert(b.depth == 5);

    /* 매수 1단 = 가장 높은 가격 */
    assert(b.bid[0].price == 67900);
    assert(b.bid[0].qty == 137);
    /* 2단은 같은 가격의 두 주문이 합쳐진다 */
    assert(b.bid[1].price == 67800);
    assert(b.bid[1].qty == 211 + 43);
    assert(b.bid[2].price == 67700);
    assert(b.bid[2].qty == 89);

    /* 매도 1단 = 가장 낮은 가격 */
    assert(b.ask[0].price == 68100);
    assert(b.ask[0].qty == 173);
    assert(b.ask[1].price == 68300);
    assert(b.ask[1].qty == 61);

    /*
     * **없는 단은 0이다.** 매수는 3단, 매도는 2단뿐인데 5단을 달라고 했다.
     * 자리를 고정해야 읽는 쪽이 줄 수를 세지 않고 읽는다.
     */
    assert(b.bid[3].price == 0 && b.bid[3].qty == 0);
    assert(b.bid[4].price == 0 && b.bid[4].qty == 0);
    assert(b.ask[2].price == 0 && b.ask[2].qty == 0);
    assert(b.ask[3].price == 0 && b.ask[3].qty == 0);
    assert(b.ask[4].price == 0 && b.ask[4].qty == 0);

    match_engine_destroy(eng);
}

/* 빈 호가창이어도 터지지 않고 전부 0이 나온다 */
static void test_book_from_empty(void)
{
    match_engine_t *eng = match_engine_create(REF, CAP);
    assert(eng != NULL);

    feed_book_t b;
    assert(feed_book_from_engine(eng, "000660", MARKET_KRX, 3, &b) == ERR_OK);
    assert(b.depth == 3);
    for (int32_t i = 0; i < 3; i++) {
        assert(b.bid[i].price == 0 && b.bid[i].qty == 0);
        assert(b.ask[i].price == 0 && b.ask[i].qty == 0);
    }

    match_engine_destroy(eng);
}

/* 만든 것이 곧바로 전선에 나갈 수 있어야 한다 */
static void test_book_encodes(void)
{
    match_engine_t *eng = build();

    feed_book_t b;
    assert(feed_book_from_engine(eng, "005930", MARKET_NXT, 4, &b) == ERR_OK);

    uint8_t buf[FEED_BOOK_BODY_MAX];
    int     n = feed_encode_book(&b, buf, sizeof(buf));
    assert(n == (int)FEED_BOOK_BODY_LEN(4));

    feed_book_t got;
    assert(feed_decode_book(buf, (size_t)n, &got) == n);
    assert(got.bid[0].price == 67900 && got.bid[0].qty == 137);
    assert(got.ask[0].price == 68100 && got.ask[0].qty == 173);

    match_engine_destroy(eng);
}

/*
 * **같은 호가창은 같은 바이트를 만든다.**
 *
 * 이것이 깨지면 같은 시드로 돌린 두 실험의 피드가 달라진다.
 */
static void test_deterministic(void)
{
    match_engine_t *e1 = build();
    match_engine_t *e2 = build();

    feed_book_t b1, b2;
    assert(feed_book_from_engine(e1, "005930", MARKET_KRX, 8, &b1) == ERR_OK);
    assert(feed_book_from_engine(e2, "005930", MARKET_KRX, 8, &b2) == ERR_OK);

    uint8_t w1[FEED_BOOK_BODY_MAX];
    uint8_t w2[FEED_BOOK_BODY_MAX];
    memset(w1, 0xEE, sizeof(w1));
    memset(w2, 0x11, sizeof(w2));

    int n1 = feed_encode_book(&b1, w1, sizeof(w1));
    int n2 = feed_encode_book(&b2, w2, sizeof(w2));
    assert(n1 == n2);
    assert(memcmp(w1, w2, (size_t)n1) == 0);

    match_engine_destroy(e1);
    match_engine_destroy(e2);
}

/* 체결 한 건이 실린다 — taker의 방향을 싣는다 */
static void test_trade_from_fill(void)
{
    match_engine_t *eng = build();

    /* 매도 호가를 때리는 매수 주문 */
    exec_result_t res;
    order_t       taker = mk(77, SIDE_BUY, 68100, 100);
    assert(match_limit(eng, &taker, &res) == ERR_OK);
    assert(res.fill_count >= 1);

    feed_trade_t t;
    assert(feed_trade_from_fill(&res.fills[0], "005930", MARKET_KRX, SIDE_BUY,
                                &t) == ERR_OK);
    assert(strcmp(t.symbol, "005930") == 0);
    assert(t.market == MARKET_KRX);
    assert(t.side == SIDE_BUY); /* **taker의 방향이다** */
    assert(t.price == res.fills[0].price);
    assert(t.qty == res.fills[0].qty);
    assert(t.exec_id == res.fills[0].taker_id);
    assert(t.exec_id == 77);

    uint8_t buf[FEED_TRADE_BODY_LEN];
    assert(feed_encode_trade(&t, buf, sizeof(buf)) ==
           (int)FEED_TRADE_BODY_LEN);

    match_engine_destroy(eng);
}

/* 인자 */
static void test_args(void)
{
    match_engine_t *eng = build();
    feed_book_t     b;
    feed_trade_t    t;
    fill_t          f;
    memset(&f, 0, sizeof(f));

    assert(feed_book_from_engine(NULL, "005930", MARKET_KRX, 3, &b) ==
           ERR_NULL_PTR);
    assert(feed_book_from_engine(eng, NULL, MARKET_KRX, 3, &b) ==
           ERR_NULL_PTR);
    assert(feed_book_from_engine(eng, "005930", MARKET_KRX, 3, NULL) ==
           ERR_NULL_PTR);

    assert(feed_book_from_engine(eng, "005930", MARKET_KRX, 0, &b) ==
           ERR_INVALID_ARG);
    assert(feed_book_from_engine(eng, "005930", MARKET_KRX,
                                 FEED_DEPTH_MAX + 1, &b) == ERR_INVALID_ARG);
    assert(feed_book_from_engine(eng, "005930", (market_t)MARKET_COUNT, 3,
                                 &b) == ERR_INVALID_ARG);

    assert(feed_trade_from_fill(NULL, "005930", MARKET_KRX, SIDE_BUY, &t) ==
           ERR_NULL_PTR);
    assert(feed_trade_from_fill(&f, NULL, MARKET_KRX, SIDE_BUY, &t) ==
           ERR_NULL_PTR);
    assert(feed_trade_from_fill(&f, "005930", MARKET_KRX, SIDE_BUY, NULL) ==
           ERR_NULL_PTR);
    assert(feed_trade_from_fill(&f, "005930", (market_t)MARKET_COUNT, SIDE_BUY,
                                &t) == ERR_INVALID_ARG);
    assert(feed_trade_from_fill(&f, "005930", MARKET_KRX, (side_t)9, &t) ==
           ERR_INVALID_ARG);

    /* 종목코드가 길면 잘리되 넘쳐 읽지 않는다 */
    assert(feed_book_from_engine(eng, "0123456789ABCDEF", MARKET_KRX, 2, &b) ==
           ERR_OK);
    assert(strlen(b.symbol) == FEED_SYMBOL_LEN);

    match_engine_destroy(eng);
}

int main(void)
{
    STEP(test_book_from_engine);
    STEP(test_book_from_empty);
    STEP(test_book_encodes);
    STEP(test_deterministic);
    STEP(test_trade_from_fill);
    STEP(test_args);
    return 0;
}
