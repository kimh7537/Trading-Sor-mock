/*
 * T5-06 전략 엔진용 시세 피드 규격.
 *
 * 완료 조건을 그대로 옮긴다: 왕복, 단 수 경계, 짧은·긴 버퍼, 빠짐 판정,
 * **같은 입력이 같은 바이트를 만든다**.
 *
 * 숫자는 맞아떨어지지 않는 값으로 고른다(T3-11에서 한 번 당했다).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "feed.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

/* --- 머리 --- */

static void test_hdr_round_trip(void)
{
    feed_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.version = FEED_VERSION;
    h.type = FEED_BOOK;
    h.seq = 8814771ull;
    h.ts = 34567891234567ll;

    uint8_t buf[FEED_HEADER_LEN];
    assert(feed_encode_hdr(&h, buf, sizeof(buf)) == (int)FEED_HEADER_LEN);

    /* 빅엔디언으로 적혔는가 — magic "MF" */
    assert(buf[0] == 0x4D);
    assert(buf[1] == 0x46);

    feed_hdr_t got;
    assert(feed_decode_hdr(buf, sizeof(buf), &got) == (int)FEED_HEADER_LEN);
    assert(got.version == FEED_VERSION);
    assert(got.type == FEED_BOOK);
    assert(got.seq == 8814771ull);
    assert(got.ts == 34567891234567ll);
}

/*
 * **남의 채널과 우리 채널의 다른 판은 다른 일이다.**
 * 앞은 끊을 일이고 뒤는 상대에게 알릴 일이라 에러를 구분한다.
 */
static void test_hdr_rejects(void)
{
    feed_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.version = FEED_VERSION;
    h.type = FEED_TRADE;
    h.seq = 5;

    uint8_t buf[FEED_HEADER_LEN];
    assert(feed_encode_hdr(&h, buf, sizeof(buf)) == (int)FEED_HEADER_LEN);

    feed_hdr_t got;

    /* 주문 전문의 magic "MS"를 먹인다 — 남의 채널이다 */
    uint8_t other[FEED_HEADER_LEN];
    memcpy(other, buf, sizeof(other));
    other[0] = 0x4D;
    other[1] = 0x53;
    assert(feed_decode_hdr(other, sizeof(other), &got) == ERR_INVALID_ARG);

    /* magic은 맞고 버전만 다르다 — 우리 채널의 다른 판이다 */
    uint8_t newer[FEED_HEADER_LEN];
    memcpy(newer, buf, sizeof(newer));
    newer[2] = FEED_VERSION + 1;
    assert(feed_decode_hdr(newer, sizeof(newer), &got) == ERR_NOT_SUPPORTED);

    /* 짧으면 읽지 않는다 */
    assert(feed_decode_hdr(buf, FEED_HEADER_LEN - 1, &got) == ERR_INVALID_ARG);
    assert(feed_encode_hdr(&h, buf, FEED_HEADER_LEN - 1) == ERR_INVALID_ARG);

    assert(feed_encode_hdr(NULL, buf, sizeof(buf)) == ERR_NULL_PTR);
    assert(feed_encode_hdr(&h, NULL, sizeof(buf)) == ERR_NULL_PTR);
    assert(feed_decode_hdr(NULL, sizeof(buf), &got) == ERR_NULL_PTR);
    assert(feed_decode_hdr(buf, sizeof(buf), NULL) == ERR_NULL_PTR);
}

/* --- 호가 스냅샷 --- */

static feed_book_t sample_book(int32_t depth)
{
    feed_book_t b;
    memset(&b, 0, sizeof(b));
    snprintf(b.symbol, sizeof(b.symbol), "005930");
    b.market = MARKET_NXT;
    b.depth = depth;

    for (int32_t i = 0; i < depth; i++) {
        b.bid[i].price = 68350 - i * 50;
        b.bid[i].qty = 137 + i * 29;
        b.ask[i].price = 68400 + i * 50;
        b.ask[i].qty = 211 + i * 43;
    }
    return b;
}

static void test_book_round_trip(void)
{
    for (int32_t d = 1; d <= FEED_DEPTH_MAX; d++) {
        feed_book_t b = sample_book(d);

        uint8_t buf[FEED_BOOK_BODY_MAX];
        int     n = feed_encode_book(&b, buf, sizeof(buf));
        assert(n == (int)FEED_BOOK_BODY_LEN(d));

        feed_book_t got;
        assert(feed_decode_book(buf, (size_t)n, &got) == n);

        assert(strcmp(got.symbol, "005930") == 0);
        assert(got.market == MARKET_NXT);
        assert(got.depth == d);
        for (int32_t i = 0; i < d; i++) {
            assert(got.bid[i].price == b.bid[i].price);
            assert(got.bid[i].qty == b.bid[i].qty);
            assert(got.ask[i].price == b.ask[i].price);
            assert(got.ask[i].qty == b.ask[i].qty);
        }
        /* 쓰지 않은 자리는 0이다 */
        for (int32_t i = d; i < FEED_DEPTH_MAX; i++) {
            assert(got.bid[i].price == 0 && got.bid[i].qty == 0);
            assert(got.ask[i].price == 0 && got.ask[i].qty == 0);
        }
    }
}

/* 단 수 경계 — 0단과 상한을 넘는 값은 받지 않는다 */
static void test_book_depth_bounds(void)
{
    feed_book_t b = sample_book(3);
    uint8_t     buf[FEED_BOOK_BODY_MAX];

    b.depth = 0;
    assert(feed_encode_book(&b, buf, sizeof(buf)) == ERR_INVALID_ARG);
    b.depth = -1;
    assert(feed_encode_book(&b, buf, sizeof(buf)) == ERR_INVALID_ARG);
    b.depth = FEED_DEPTH_MAX + 1;
    assert(feed_encode_book(&b, buf, sizeof(buf)) == ERR_INVALID_ARG);

    /* 시장이 범위 밖이면 거절한다 */
    b = sample_book(3);
    b.market = (market_t)MARKET_COUNT;
    assert(feed_encode_book(&b, buf, sizeof(buf)) == ERR_INVALID_ARG);
}

/*
 * **길이 필드를 믿기 전에 범위를 본다.**
 *
 * depth를 믿고 계산하면 엉뚱한 자리까지 바디로 삼는다(T5-01 J3에서 배운 것).
 */
static void test_book_bad_depth_in_wire(void)
{
    feed_book_t b = sample_book(4);
    uint8_t     buf[FEED_BOOK_BODY_MAX];
    int         n = feed_encode_book(&b, buf, sizeof(buf));
    assert(n > 0);

    feed_book_t got;

    /* depth 자리를 터무니없는 값으로 바꾼다 */
    uint8_t bad[FEED_BOOK_BODY_MAX];
    memcpy(bad, buf, sizeof(bad));
    bad[FEED_SYMBOL_LEN + 1] = 0xFF;
    assert(feed_decode_book(bad, (size_t)n, &got) == ERR_INVALID_ARG);

    memcpy(bad, buf, sizeof(bad));
    bad[FEED_SYMBOL_LEN + 1] = 0;
    assert(feed_decode_book(bad, (size_t)n, &got) == ERR_INVALID_ARG);

    /* 시장 자리가 범위 밖 */
    memcpy(bad, buf, sizeof(bad));
    bad[FEED_SYMBOL_LEN] = 0x7F;
    assert(feed_decode_book(bad, (size_t)n, &got) == ERR_INVALID_ARG);
}

/*
 * **길이까지 맞춰 보낸 과대 depth.**
 *
 * depth만 키우면 길이 검사가 대신 막아 준다. 그래서 범위 검사를 지워도
 * 테스트가 통과했다(변이 D3). 진짜 위험한 입력은 **depth와 길이를 서로
 * 맞춰서** 보내는 것이다 — 그러면 길이 검사를 통과하고 `bid[]` 뒤로 넘쳐
 * 쓴다. 자리가 열 개인데 열한 단을 채우려 들기 때문이다.
 *
 * 두 검사가 겹쳐 보이지만 막는 것이 다르다. 겹친다고 하나를 지우면 이 입력이
 * 통과한다.
 */
static void test_book_oversized_depth_with_matching_len(void)
{
    /* FEED_DEPTH_MAX + 1 단짜리 바디를 손으로 만든다 */
    const int32_t over = FEED_DEPTH_MAX + 1;
    const size_t  len = FEED_BOOK_BODY_LEN(over);

    uint8_t body[FEED_BOOK_BODY_LEN(FEED_DEPTH_MAX + 4)];
    memset(body, 0x33, sizeof(body));
    memcpy(body, "005930\0\0", FEED_SYMBOL_LEN);
    body[FEED_SYMBOL_LEN] = (uint8_t)MARKET_KRX;
    body[FEED_SYMBOL_LEN + 1] = (uint8_t)over;
    body[FEED_SYMBOL_LEN + 2] = 0;
    body[FEED_SYMBOL_LEN + 3] = 0;

    feed_book_t got;
    assert(feed_decode_book(body, len, &got) == ERR_INVALID_ARG);

    /* 딱 상한까지는 받는다 — 거절이 너무 넓지 않은지도 본다 */
    body[FEED_SYMBOL_LEN + 1] = (uint8_t)FEED_DEPTH_MAX;
    assert(feed_decode_book(body, FEED_BOOK_BODY_LEN(FEED_DEPTH_MAX), &got) ==
           (int)FEED_BOOK_BODY_LEN(FEED_DEPTH_MAX));
    assert(got.depth == FEED_DEPTH_MAX);
}

/* 짧은 버퍼도 긴 버퍼도 받지 않는다 — 길이가 규격이다 */
static void test_book_length_exact(void)
{
    feed_book_t b = sample_book(6);
    uint8_t     buf[FEED_BOOK_BODY_MAX + 8];
    int         n = feed_encode_book(&b, buf, sizeof(buf));
    assert(n == (int)FEED_BOOK_BODY_LEN(6));

    feed_book_t got;
    assert(feed_decode_book(buf, (size_t)n - 1, &got) == ERR_INVALID_ARG);
    assert(feed_decode_book(buf, (size_t)n + 1, &got) == ERR_INVALID_ARG);
    assert(feed_decode_book(buf, 0, &got) == ERR_INVALID_ARG);

    /* 쓸 자리가 한 바이트라도 모자라면 쓰지 않는다 */
    assert(feed_encode_book(&b, buf, (size_t)n - 1) == ERR_INVALID_ARG);
    assert(feed_encode_book(&b, buf, (size_t)n) == n);
}

/* --- 체결 --- */

static void test_trade_round_trip(void)
{
    feed_trade_t t;
    memset(&t, 0, sizeof(t));
    snprintf(t.symbol, sizeof(t.symbol), "000660");
    t.market = MARKET_KRX;
    t.side = SIDE_SELL;
    t.price = 191300;
    t.qty = 417;
    t.exec_id = 77123456789ull;

    uint8_t buf[FEED_TRADE_BODY_LEN];
    int     n = feed_encode_trade(&t, buf, sizeof(buf));
    assert(n == (int)FEED_TRADE_BODY_LEN);

    feed_trade_t got;
    assert(feed_decode_trade(buf, (size_t)n, &got) == n);
    assert(strcmp(got.symbol, "000660") == 0);
    assert(got.market == MARKET_KRX);
    assert(got.side == SIDE_SELL);
    assert(got.price == 191300);
    assert(got.qty == 417);
    assert(got.exec_id == 77123456789ull);
}

static void test_trade_rejects(void)
{
    feed_trade_t t;
    memset(&t, 0, sizeof(t));
    snprintf(t.symbol, sizeof(t.symbol), "000660");
    t.market = MARKET_KRX;
    t.side = SIDE_BUY;
    t.price = 100;
    t.qty = 1;

    uint8_t buf[FEED_TRADE_BODY_LEN];
    assert(feed_encode_trade(&t, buf, sizeof(buf)) ==
           (int)FEED_TRADE_BODY_LEN);

    t.market = (market_t)MARKET_COUNT;
    assert(feed_encode_trade(&t, buf, sizeof(buf)) == ERR_INVALID_ARG);
    t.market = MARKET_KRX;
    t.side = (side_t)7;
    assert(feed_encode_trade(&t, buf, sizeof(buf)) == ERR_INVALID_ARG);
    t.side = SIDE_BUY;
    assert(feed_encode_trade(&t, buf, FEED_TRADE_BODY_LEN - 1) ==
           ERR_INVALID_ARG);

    assert(feed_encode_trade(&t, buf, sizeof(buf)) ==
           (int)FEED_TRADE_BODY_LEN);

    feed_trade_t got;
    assert(feed_decode_trade(buf, FEED_TRADE_BODY_LEN - 1, &got) ==
           ERR_INVALID_ARG);
    assert(feed_decode_trade(buf, FEED_TRADE_BODY_LEN + 1, &got) ==
           ERR_INVALID_ARG);

    /* 전선에서 시장·방향이 깨진 경우 */
    uint8_t bad[FEED_TRADE_BODY_LEN];
    memcpy(bad, buf, sizeof(bad));
    bad[FEED_SYMBOL_LEN] = 0x40;
    assert(feed_decode_trade(bad, sizeof(bad), &got) == ERR_INVALID_ARG);

    memcpy(bad, buf, sizeof(bad));
    bad[FEED_SYMBOL_LEN + 1] = 0x40;
    assert(feed_decode_trade(bad, sizeof(bad), &got) == ERR_INVALID_ARG);

    assert(feed_encode_trade(NULL, buf, sizeof(buf)) == ERR_NULL_PTR);
    assert(feed_decode_trade(NULL, sizeof(buf), &got) == ERR_NULL_PTR);
    assert(feed_decode_trade(buf, sizeof(buf), NULL) == ERR_NULL_PTR);
}

/* --- 결정성 --- */

/*
 * **같은 입력이 같은 바이트를 만든다.**
 *
 * 이것이 깨지면 같은 시드로 돌린 두 실험의 피드가 달라지고, 전략 비교라는
 * 이 프로젝트의 전제가 무너진다.
 */
static void test_same_input_same_bytes(void)
{
    feed_book_t b = sample_book(7);
    uint8_t     a1[FEED_BOOK_BODY_MAX];
    uint8_t     a2[FEED_BOOK_BODY_MAX];
    memset(a1, 0xEE, sizeof(a1));
    memset(a2, 0x11, sizeof(a2)); /* 서로 다른 쓰레기로 채워 둔다 */

    int n1 = feed_encode_book(&b, a1, sizeof(a1));
    int n2 = feed_encode_book(&b, a2, sizeof(a2));
    assert(n1 == n2);
    assert(memcmp(a1, a2, (size_t)n1) == 0);

    /* 머리도 마찬가지다 */
    feed_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.version = FEED_VERSION;
    h.type = FEED_BOOK;
    h.seq = 42;
    h.ts = 999;

    uint8_t h1[FEED_HEADER_LEN];
    uint8_t h2[FEED_HEADER_LEN];
    memset(h1, 0xEE, sizeof(h1));
    memset(h2, 0x11, sizeof(h2));
    assert(feed_encode_hdr(&h, h1, sizeof(h1)) == (int)FEED_HEADER_LEN);
    assert(feed_encode_hdr(&h, h2, sizeof(h2)) == (int)FEED_HEADER_LEN);
    assert(memcmp(h1, h2, sizeof(h1)) == 0);
}

/* --- 빠짐 판정 --- */

static feed_hdr_t hdr_at(uint64_t seq, uint8_t type)
{
    feed_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.version = FEED_VERSION;
    h.type = type;
    h.seq = seq;
    h.ts = (ts_t)seq * 1000;
    return h;
}

/* 첫 메시지는 번호가 무엇이든 받는다 — 언제 붙었는지는 고를 수 없다 */
static void test_sub_first_message(void)
{
    feed_sub_t s;
    feed_sub_init(&s);
    assert(!feed_sub_usable(&s)); /* 아직 아무것도 못 받았다 */

    feed_hdr_t h = hdr_at(91731, FEED_BOOK);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);
    assert(feed_sub_usable(&s));
    assert(s.expected == 91732);
    assert(s.gaps == 0);
}

static void test_sub_in_order(void)
{
    feed_sub_t s;
    feed_sub_init(&s);

    for (uint64_t i = 500; i < 520; i++) {
        feed_hdr_t h = hdr_at(i, (i % 3 == 0) ? FEED_BOOK : FEED_TRADE);
        assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);
    }
    assert(s.expected == 520);
    assert(s.gaps == 0);
    assert(feed_sub_usable(&s));
}

/* 이미 본 번호는 버린다. 기대치를 되돌리지 않는다 */
static void test_sub_duplicate(void)
{
    feed_sub_t s;
    feed_sub_init(&s);

    feed_hdr_t h = hdr_at(100, FEED_BOOK);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);
    h = hdr_at(101, FEED_TRADE);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);

    h = hdr_at(100, FEED_BOOK);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_DUP);
    assert(s.expected == 102); /* 되돌아가지 않았다 */
    assert(s.gaps == 0);
    assert(feed_sub_usable(&s));
}

/*
 * **갭을 보면 믿지 않는다. 다음 스냅샷으로 회복한다.**
 *
 * 이것이 이 태스크의 핵심 판단이다 — 방송 채널이라 재전송을 요청할 상대가
 * 없으므로, T3-12의 답(RESEND_REQ)을 쓸 수 없다.
 */
static void test_sub_gap_then_snapshot(void)
{
    feed_sub_t s;
    feed_sub_init(&s);

    feed_hdr_t h = hdr_at(200, FEED_BOOK);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);
    assert(feed_sub_usable(&s));

    /* 201..203을 놓치고 204가 체결로 왔다 */
    h = hdr_at(204, FEED_TRADE);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_GAP);
    assert(s.gaps == 1);
    assert(s.stale);
    assert(!feed_sub_usable(&s)); /* **새 주문을 내면 안 된다** */

    /* 체결이 더 와도 회복되지 않는다 — 체결 하나는 호가창을 복원 못 한다 */
    h = hdr_at(205, FEED_TRADE);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);
    assert(!feed_sub_usable(&s));

    /* 스냅샷이 오면 회복한다 — 자기 완결적이라 앞을 몰라도 믿을 수 있다 */
    h = hdr_at(206, FEED_BOOK);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);
    assert(feed_sub_usable(&s));
    assert(s.gaps == 1); /* 있었던 일은 지우지 않는다 */
}

/*
 * 갭 뒤에도 기대치가 따라 올라간다.
 *
 * 올라가지 않으면 이후 모든 메시지가 갭으로 잡혀 **영영 회복하지 못한다.**
 */
static void test_sub_gap_advances_expected(void)
{
    feed_sub_t s;
    feed_sub_init(&s);

    feed_hdr_t h = hdr_at(10, FEED_TRADE);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);

    h = hdr_at(1000, FEED_TRADE);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_GAP);
    assert(s.expected == 1001);

    /* 바로 다음 것은 정상이어야 한다 */
    h = hdr_at(1001, FEED_TRADE);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);
    assert(s.gaps == 1);
}

/* 갭인데 그것이 스냅샷이면 그 메시지로 이미 회복된 것이다 */
static void test_sub_gap_on_snapshot(void)
{
    feed_sub_t s;
    feed_sub_init(&s);

    feed_hdr_t h = hdr_at(50, FEED_TRADE);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_OK);

    h = hdr_at(60, FEED_BOOK);
    assert(feed_sub_accept(&s, &h) == FEED_SEQ_GAP);
    assert(s.gaps == 1); /* 빠진 것은 빠진 것이다 */
    assert(!s.stale);    /* 그래도 이 스냅샷은 믿을 수 있다 */
    assert(feed_sub_usable(&s));
}

static void test_sub_args(void)
{
    feed_sub_t s;
    feed_sub_init(&s);
    feed_hdr_t h = hdr_at(1, FEED_BOOK);

    assert(feed_sub_accept(NULL, &h) == FEED_SEQ_DUP);
    assert(feed_sub_accept(&s, NULL) == FEED_SEQ_DUP);
    assert(!feed_sub_usable(NULL));

    feed_sub_init(NULL); /* 터지지 않는다 */
}

static void test_type_str(void)
{
    assert(strcmp(feed_type_str(FEED_BOOK), "BOOK") == 0);
    assert(strcmp(feed_type_str(FEED_TRADE), "TRADE") == 0);
    assert(strcmp(feed_type_str(0), "?") == 0);
    assert(strcmp(feed_type_str(200), "?") == 0);
}

int main(void)
{
    STEP(test_hdr_round_trip);
    STEP(test_hdr_rejects);
    STEP(test_book_round_trip);
    STEP(test_book_depth_bounds);
    STEP(test_book_bad_depth_in_wire);
    STEP(test_book_oversized_depth_with_matching_len);
    STEP(test_book_length_exact);
    STEP(test_trade_round_trip);
    STEP(test_trade_rejects);
    STEP(test_same_input_same_bytes);
    STEP(test_sub_first_message);
    STEP(test_sub_in_order);
    STEP(test_sub_duplicate);
    STEP(test_sub_gap_then_snapshot);
    STEP(test_sub_gap_advances_expected);
    STEP(test_sub_gap_on_snapshot);
    STEP(test_sub_args);
    STEP(test_type_str);
    return 0;
}
