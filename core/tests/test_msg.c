/*
 * T3-02 전문 종별 정의.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 종별 코드·이름·바디 길이가 한 목록에서 나온다 (목록을 순회해 확인)
 *  2. 바디 배치가 표와 일치한다 — **바이트 위치를 직접 대조한다**
 *  3. 요청 종별마다 응답 종별이 있다
 *  4. 왕복이 모든 필드를 보존한다
 *  5. **모르는 종별**과 **길이가 규격과 다른 전문**을 거절한다
 *
 * 2번은 T3-01과 같은 이유로 중요하다. 왕복만 보면 인코딩과 디코딩이 같이 틀려도
 * 통과한다 — 두 프로세스가 서로 다른 빌드일 수 있는 곳에서 그건 검사가 아니다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "msg.h"

/* --- 1. 종별 표 --- */

/* 목록에서 코드·길이를 뽑아 표와 대조한다. */
#define ENTRY(name, code, len, text) {(code), (len), (text)},

static const struct {
    uint8_t     code;
    int32_t     len;
    const char *text;
} TABLE[] = {MSG_TYPE_LIST(ENTRY)};

#undef ENTRY

#define TABLE_N (sizeof(TABLE) / sizeof(TABLE[0]))

static void test_type_table(void)
{
    assert(TABLE_N == 25);

    for (size_t i = 0; i < TABLE_N; i++) {
        assert(msg_is_known(TABLE[i].code));
        assert(msg_body_len(TABLE[i].code) == TABLE[i].len);
        assert(strcmp(msg_type_str(TABLE[i].code), TABLE[i].text) == 0);

        /* 코드가 겹치지 않는다. 겹치면 전문 하나를 둘로 해석하게 된다. */
        for (size_t j = i + 1; j < TABLE_N; j++) {
            assert(TABLE[i].code != TABLE[j].code);
        }
    }

    /* 표에 적힌 길이가 문서의 값과 같다. */
    assert(MSG_ORDER_REQ_LEN == 39);
    assert(MSG_ORDER_ACK_LEN == 29);
    assert(MSG_CANCEL_REQ_LEN == 28);
    assert(MSG_CANCEL_ACK_LEN == 25);
    assert(MSG_MODIFY_REQ_LEN == 36);
    assert(MSG_MODIFY_ACK_LEN == 25);
    assert(MSG_QUERY_REQ_LEN == 20);
    assert(MSG_QUERY_ACK_LEN == 38);
    assert(MSG_FILL_NOTI_LEN == 46);
    assert(MSG_LOGIN_REQ_LEN == 16);
    assert(MSG_LOGIN_ACK_LEN == 4);
    /*
     * 하트비트는 바디가 0이다. 0을 "없음"이 아니라 유효한 길이로 다뤄야
     * 한다 — 조립기(T3-09)도 디코더도 0을 그대로 받아들인다.
     */
    assert(MSG_HEARTBEAT_LEN == 0);
    assert(MSG_RESEND_REQ_LEN == 8);
    assert(MSG_GAP_FILL_LEN == 8);
    assert(MSG_BOOK_REQ_LEN == 9);
    assert(MSG_BOOK_ACK_LEN == 181);
    assert(MSG_DETAIL_REQ_LEN == 20);
    assert(MSG_DETAIL_ACK_LEN == 91);
    assert(MSG_BALANCE_REQ_LEN == 12);
    assert(MSG_BALANCE_ACK_LEN == 32);
    assert(MSG_BOOK_FEED_LEN == 178);
    /* 종목 종류 1바이트가 붙었다(T10-01) */
    assert(MSG_SYMBOL_SET_LEN == 13);
    assert(MSG_SYMBOL_ACK_LEN == 17);
    assert(MSG_ACCOUNT_OPEN_LEN == 36);
    assert(MSG_ACCOUNT_ACK_LEN == 56);

    /* 어떤 전문도 프레임 한도를 넘지 않는다. */
    for (size_t i = 0; i < TABLE_N; i++) {
        assert((uint32_t)TABLE[i].len <= WIRE_BODY_MAX);
    }
}

/* 완료 조건 5 — 모르는 종별. */
static void test_unknown_types(void)
{
    /* 0은 종별로 쓰지 않는다. 0으로 초기화된 버퍼가 유효해 보이면 안 된다. */
    assert(!msg_is_known(MSG_UNKNOWN));
    assert(msg_body_len(MSG_UNKNOWN) == -1);

    /* 목록에 없는 코드는 전부 모르는 종별이다 — 256가지를 다 훑는다. */
    for (int code = 0; code < 256; code++) {
        bool in_table = false;
        for (size_t i = 0; i < TABLE_N; i++) {
            if (TABLE[i].code == (uint8_t)code) {
                in_table = true;
                break;
            }
        }
        assert(msg_is_known((uint8_t)code) == in_table);
        if (!in_table) {
            assert(msg_body_len((uint8_t)code) == -1);
            assert(msg_type_str((uint8_t)code) != NULL);
        }
    }
}

/* --- 2. 요청과 응답 --- */

static void test_reply_pairs(void)
{
    assert(msg_reply_type(MSG_ORDER_REQ) == MSG_ORDER_ACK);
    assert(msg_reply_type(MSG_CANCEL_REQ) == MSG_CANCEL_ACK);
    assert(msg_reply_type(MSG_MODIFY_REQ) == MSG_MODIFY_ACK);
    assert(msg_reply_type(MSG_QUERY_REQ) == MSG_QUERY_ACK);
    assert(msg_reply_type(MSG_BOOK_REQ) == MSG_BOOK_ACK);
    /* 스냅샷 주입의 답은 심은 뒤의 호가창이다(T8-02). */
    assert(msg_reply_type(MSG_BOOK_FEED) == MSG_BOOK_ACK);
    /* 종목 전환은 바뀐 결과를 따로 답한다(T8-10). */
    assert(msg_reply_type(MSG_SYMBOL_SET) == MSG_SYMBOL_ACK);
    assert(msg_reply_type(MSG_SYMBOL_ACK) == MSG_UNKNOWN);
    /* 계좌 개설의 답은 그 계좌의 잔고다(T9-01). */
    assert(msg_reply_type(MSG_ACCOUNT_OPEN) == MSG_ACCOUNT_ACK);
    assert(msg_reply_type(MSG_ACCOUNT_ACK) == MSG_UNKNOWN);
    assert(msg_reply_type(MSG_BOOK_ACK) == MSG_UNKNOWN);
    assert(msg_reply_type(MSG_DETAIL_REQ) == MSG_DETAIL_ACK);
    assert(msg_reply_type(MSG_BALANCE_REQ) == MSG_BALANCE_ACK);
    assert(msg_reply_type(MSG_DETAIL_ACK) == MSG_UNKNOWN);
    assert(msg_reply_type(MSG_BALANCE_ACK) == MSG_UNKNOWN);

    /* 체결 통보는 요청 없이 밀어 보내는 것이라 응답할 대상이 없다. */
    assert(msg_reply_type(MSG_FILL_NOTI) == MSG_UNKNOWN);

    /* 응답의 응답은 없다. */
    assert(msg_reply_type(MSG_ORDER_ACK) == MSG_UNKNOWN);
    assert(msg_reply_type(MSG_CANCEL_ACK) == MSG_UNKNOWN);
    assert(msg_reply_type(MSG_MODIFY_ACK) == MSG_UNKNOWN);
    assert(msg_reply_type(MSG_QUERY_ACK) == MSG_UNKNOWN);

    assert(msg_reply_type(MSG_UNKNOWN) == MSG_UNKNOWN);
    assert(msg_reply_type(200) == MSG_UNKNOWN);

    /* 응답 종별은 모두 아는 종별이다 — 짝만 지어 놓고 정의를 빼먹지 않았다. */
    for (size_t i = 0; i < TABLE_N; i++) {
        msg_type_t reply = msg_reply_type(TABLE[i].code);
        if (reply != MSG_UNKNOWN) {
            assert(msg_is_known((uint8_t)reply));
        }
    }
}

/* --- 3. 바디 배치를 바이트로 대조 --- */

static void test_order_req_layout(void)
{
    msg_order_req_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.account, sizeof(m.account), "%s", "ACC-001");
    snprintf(m.symbol, sizeof(m.symbol), "%s", "005930");
    m.cl_ord_id = 0x0102030405060708ULL;
    m.side = 1;
    m.type = 2;
    m.market = 1;
    m.price = 10000;
    m.qty = -1; /* 부호가 그대로 실리는지 본다 */

    uint8_t buf[MSG_ORDER_REQ_LEN];
    assert(msg_encode_order_req(&m, buf, sizeof(buf)) == MSG_ORDER_REQ_LEN);

    static const uint8_t WANT[MSG_ORDER_REQ_LEN] = {
        /* account[12] — "ACC-001" 뒤는 0으로 채운다 */
        'A', 'C', 'C', '-', '0', '0', '1', 0, 0, 0, 0, 0,
        /* symbol[8] */
        '0', '0', '5', '9', '3', '0', 0, 0,
        /* cl_ord_id */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
        /* side, type, market */
        0x01, 0x02, 0x01,
        /* price = 10000 = 0x2710 */
        0x00, 0x00, 0x27, 0x10,
        /* qty = -1 */
        0xFF, 0xFF, 0xFF, 0xFF,
    };
    assert(memcmp(buf, WANT, sizeof(WANT)) == 0);

    msg_order_req_t out;
    assert(msg_decode_order_req(buf, sizeof(buf), &out) == MSG_ORDER_REQ_LEN);
    assert(strcmp(out.account, "ACC-001") == 0);
    assert(strcmp(out.symbol, "005930") == 0);
    assert(out.cl_ord_id == m.cl_ord_id);
    assert(out.side == 1 && out.type == 2 && out.market == 1);
    assert(out.price == 10000);
    assert(out.qty == -1);
}

static void test_fill_noti_layout(void)
{
    msg_fill_noti_t m;
    memset(&m, 0, sizeof(m));
    m.order_id = 17;
    m.cl_ord_id = 99;
    snprintf(m.symbol, sizeof(m.symbol), "%s", "AB");
    m.market = 1;
    m.side = 0;
    m.price = 256;
    m.qty = 1;
    m.remaining_qty = 0;
    m.exec_id = 0xFFFFFFFFFFFFFFFFULL;

    uint8_t buf[MSG_FILL_NOTI_LEN];
    assert(msg_encode_fill_noti(&m, buf, sizeof(buf)) == MSG_FILL_NOTI_LEN);

    static const uint8_t WANT[MSG_FILL_NOTI_LEN] = {
        0, 0, 0, 0, 0, 0, 0, 0x11,                      /* order_id = 17 */
        0, 0, 0, 0, 0, 0, 0, 0x63,                      /* cl_ord_id = 99 */
        'A', 'B', 0, 0, 0, 0, 0, 0,                     /* symbol[8] */
        0x01,                                           /* market */
        0x00,                                           /* side */
        0x00, 0x00, 0x01, 0x00,                         /* price = 256 */
        0x00, 0x00, 0x00, 0x01,                         /* qty = 1 */
        0x00, 0x00, 0x00, 0x00,                         /* remaining_qty */
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* exec_id */
    };
    assert(memcmp(buf, WANT, sizeof(WANT)) == 0);
}

/*
 * 호가 응답은 배열 넷이다. **각 배열의 첫 칸과 마지막 칸 위치를 바이트로 대조한다** —
 * 배열 순서가 뒤바뀌거나 한 칸 밀리면 왕복은 통과해도 여기서 깨진다.
 */
static void test_book_ack_layout(void)
{
    msg_book_ack_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.symbol, sizeof(m.symbol), "%s", "005930");
    m.market = 1;
    for (int i = 0; i < MSG_BOOK_DEPTH; i++) {
        m.bid_price[i] = 0x01000000 + i;
        m.bid_qty[i] = 0x02000000 + i;
        m.ask_price[i] = 0x03000000 + i;
        m.ask_qty[i] = 0x04000000 + i;
    }

    uint8_t buf[MSG_BOOK_ACK_LEN];
    assert(msg_encode_book_ack(&m, buf, sizeof(buf)) == MSG_BOOK_ACK_LEN);

    static const uint8_t SYM[8] = {'0', '0', '5', '9', '3', '0', 0, 0};
    assert(memcmp(buf, SYM, sizeof(SYM)) == 0);
    assert(buf[8] == 1);
    for (int arr = 0; arr < 4; arr++) {
        size_t first = 9 + (size_t)arr * MSG_BOOK_DEPTH * 4;
        size_t last = first + (MSG_BOOK_DEPTH - 1) * 4;
        assert(buf[first] == arr + 1 && buf[first + 3] == 0);
        assert(buf[last] == arr + 1 && buf[last + 3] == MSG_BOOK_DEPTH - 1);
    }
}

/*
 * 호가 스냅샷 주입(T8-02). 호가 응답과 같은 배열 넷 앞에 **피드 시각 i64**가 끼어든다.
 * 그 8바이트만큼 배열 전체가 밀리므로, 시각 바이트와 첫 배열의 시작 위치를 함께 본다.
 */
static void test_book_feed_layout(void)
{
    msg_book_feed_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.symbol, sizeof(m.symbol), "%s", "005930");
    m.market = 1;
    m.flags = MSG_FEED_END;
    m.feed_ts = 0x0102030405060708LL;
    for (int i = 0; i < MSG_BOOK_DEPTH; i++) {
        m.bid_price[i] = 0x01000000 + i;
        m.bid_qty[i] = 0x02000000 + i;
        m.ask_price[i] = 0x03000000 + i;
        m.ask_qty[i] = 0x04000000 + i;
    }

    uint8_t buf[MSG_BOOK_FEED_LEN];
    assert(msg_encode_book_feed(&m, buf, sizeof(buf)) == MSG_BOOK_FEED_LEN);

    static const uint8_t SYM[8] = {'0', '0', '5', '9', '3', '0', 0, 0};
    assert(memcmp(buf, SYM, sizeof(SYM)) == 0);
    assert(buf[8] == 1);
    assert(buf[9] == MSG_FEED_END);
    for (int i = 0; i < 8; i++) {
        assert(buf[10 + i] == i + 1); /* 빅엔디언 i64 */
    }
    for (int arr = 0; arr < 4; arr++) {
        size_t first = 18 + (size_t)arr * MSG_BOOK_DEPTH * 4;
        size_t last = first + (MSG_BOOK_DEPTH - 1) * 4;
        assert(buf[first] == arr + 1 && buf[first + 3] == 0);
        assert(buf[last] == arr + 1 && buf[last + 3] == MSG_BOOK_DEPTH - 1);
    }

    /* 단수가 모자란 스냅샷 — 남는 단은 0이고 그대로 왕복한다. */
    msg_book_feed_t few;
    memset(&few, 0, sizeof(few));
    snprintf(few.symbol, sizeof(few.symbol), "%s", "005930");
    few.bid_price[0] = 69900;
    few.bid_qty[0] = 12;
    few.ask_price[0] = 70000;
    few.ask_qty[0] = 7;
    assert(msg_encode_book_feed(&few, buf, sizeof(buf)) == MSG_BOOK_FEED_LEN);
    msg_book_feed_t back;
    assert(msg_decode_book_feed(buf, MSG_BOOK_FEED_LEN, &back) ==
           MSG_BOOK_FEED_LEN);
    assert(memcmp(&few, &back, sizeof(few)) == 0);
}

/*
 * 주문 상세 응답(T7-02). 앞쪽 고정 필드 뒤에 시장별 배열 넷이 온다 — 배열 원소가 KRX(0)·NXT(1)
 * 순서인지, i64 배열이 8바이트씩 놓이는지를 바이트 위치로 대조한다.
 */
/* 종목 전환 전문의 바이트 배치(T8-10). */
static void test_symbol_set_layout(void)
{
    msg_symbol_set_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.symbol, sizeof(m.symbol), "%s", "000660");
    m.ref_price = 0x01020304;

    uint8_t buf[MSG_SYMBOL_SET_LEN];
    assert(msg_encode_symbol_set(&m, buf, sizeof(buf)) == MSG_SYMBOL_SET_LEN);

    static const uint8_t SYM[8] = {'0', '0', '0', '6', '6', '0', 0, 0};
    assert(memcmp(buf, SYM, sizeof(SYM)) == 0);
    assert(buf[8] == 0x01 && buf[9] == 0x02 && buf[10] == 0x03 &&
           buf[11] == 0x04);            /* 빅엔디언 i32 */
    assert(buf[12] == MSG_SYMBOL_KR);   /* 종목 종류가 맨 뒤에 붙는다 */

    msg_symbol_ack_t a;
    memset(&a, 0, sizeof(a));
    snprintf(a.symbol, sizeof(a.symbol), "%s", "000660");
    a.ref_price = 260000;
    a.code = -7;
    a.kind = MSG_SYMBOL_US;

    uint8_t abuf[MSG_SYMBOL_ACK_LEN];
    assert(msg_encode_symbol_ack(&a, abuf, sizeof(abuf)) == MSG_SYMBOL_ACK_LEN);
    assert(memcmp(abuf, SYM, sizeof(SYM)) == 0);
    msg_symbol_ack_t back;
    assert(msg_decode_symbol_ack(abuf, MSG_SYMBOL_ACK_LEN, &back) ==
           MSG_SYMBOL_ACK_LEN);
    assert(back.code == -7 && back.ref_price == 260000);
    assert(back.kind == MSG_SYMBOL_US);
}

static void test_detail_ack_layout(void)
{
    msg_detail_ack_t m;
    memset(&m, 0, sizeof(m));
    m.order_id = 0x0102030405060708ULL;
    m.cl_ord_id = 9;
    m.reason = -9;
    m.side = 1;
    m.status = 2;
    m.market = 255;
    m.price = 70000;
    m.qty = 100;
    m.filled = 60;
    m.canceled = 40;
    m.working = 0;
    m.notional = 0x0000000100000002LL;
    m.leg_sent[MARKET_KRX] = 0x11;
    m.leg_sent[MARKET_NXT] = 0x12;
    m.leg_filled[MARKET_NXT] = 0x22;
    m.leg_canceled[MARKET_KRX] = 0x31;
    m.leg_notional[MARKET_KRX] = 0x0A0B0C0D0E0F1011LL;
    m.leg_notional[MARKET_NXT] = 0x42;

    uint8_t buf[MSG_DETAIL_ACK_LEN];
    assert(msg_encode_detail_ack(&m, buf, sizeof(buf)) == MSG_DETAIL_ACK_LEN);

    assert(buf[0] == 0x01 && buf[7] == 0x08);                     /* order_id */
    assert(buf[15] == 9);                                         /* cl_ord_id */
    assert(buf[16] == 0xFF && buf[19] == 0xF7);                   /* reason = -9 */
    assert(buf[20] == 1 && buf[21] == 2 && buf[22] == 255);       /* side status market */
    assert(buf[23] == 0x00 && buf[25] == 0x11 && buf[26] == 0x70); /* price 70000 */
    assert(buf[42] == 0x00 && buf[46] == 0x01 && buf[50] == 0x02); /* notional i64 at 43 */
    /* 51: leg_sent[2] */
    assert(buf[54] == 0x11 && buf[58] == 0x12);
    /* 59: leg_filled[2] */
    assert(buf[62] == 0x00 && buf[66] == 0x22);
    /* 67: leg_canceled[2] */
    assert(buf[70] == 0x31 && buf[74] == 0x00);
    /* 75: leg_notional i64[2] */
    assert(buf[75] == 0x0A && buf[82] == 0x11);
    assert(buf[90] == 0x42);
}

/*
 * 잔고 응답 — 예수금과 묶인 금액이 같은 i64라 **자리를 바꿔 써도 왕복은 통과한다.**
 * 그래서 두 값을 다르게 넣고 바이트 위치로 대조한다(변이 D15가 왕복만으로는 살아남았다).
 */
static void test_balance_ack_layout(void)
{
    msg_balance_ack_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.account, sizeof(m.account), "%s", "123456789012");
    m.reason = -9;
    m.cash = 0x0102030405060708LL;
    m.reserved = 0x1112131415161718LL;

    uint8_t buf[MSG_BALANCE_ACK_LEN];
    assert(msg_encode_balance_ack(&m, buf, sizeof(buf)) == MSG_BALANCE_ACK_LEN);
    assert(buf[0] == '1' && buf[11] == '2');         /* account[12] */
    assert(buf[12] == 0xFF && buf[15] == 0xF7);      /* reason = -9 */
    assert(buf[16] == 0x01 && buf[23] == 0x08);      /* cash */
    assert(buf[24] == 0x11 && buf[31] == 0x18);      /* reserved */
}

/* --- 4. 왕복 --- */

/*
 * 아홉 전문을 전부 왕복시킨다. 경계값을 섞어 넣고, 디코딩 결과를 **바이트로도**
 * 비교한다 — 패딩을 밀지 않으면 같은 값에서 다른 바이트가 나온다.
 */
#define ROUNDTRIP(TYPE, ENC, DEC, LEN, FILL)                                \
    do {                                                                   \
        TYPE in;                                                           \
        memset(&in, 0, sizeof(in));                                        \
        FILL;                                                              \
        uint8_t b[LEN];                                                    \
        assert(ENC(&in, b, sizeof(b)) == (LEN));                           \
        TYPE o1;                                                           \
        TYPE o2;                                                           \
        assert(DEC(b, sizeof(b), &o1) == (LEN));                           \
        assert(DEC(b, sizeof(b), &o2) == (LEN));                           \
        assert(memcmp(&o1, &o2, sizeof(o1)) == 0);                         \
        uint8_t again[LEN];                                                \
        assert(ENC(&o1, again, sizeof(again)) == (LEN));                   \
        assert(memcmp(b, again, LEN) == 0);                                \
    } while (0)

static void test_roundtrip_all(void)
{
    ROUNDTRIP(msg_order_req_t, msg_encode_order_req, msg_decode_order_req,
              MSG_ORDER_REQ_LEN, {
                  snprintf(in.account, sizeof(in.account), "%s", "123456789012");
                  snprintf(in.symbol, sizeof(in.symbol), "%s", "12345678");
                  in.cl_ord_id = UINT64_MAX;
                  in.side = 255;
                  in.type = 255;
                  in.market = 255;
                  in.price = INT32_MIN;
                  in.qty = INT32_MAX;
              });

    ROUNDTRIP(msg_order_ack_t, msg_encode_order_ack, msg_decode_order_ack,
              MSG_ORDER_ACK_LEN, {
                  in.cl_ord_id = UINT64_MAX;
                  in.order_id = 1;
                  in.status = 4;
                  in.reason = -13;
                  in.filled_qty = 0;
                  in.price = INT32_MAX;
              });

    ROUNDTRIP(msg_cancel_req_t, msg_encode_cancel_req, msg_decode_cancel_req,
              MSG_CANCEL_REQ_LEN, {
                  snprintf(in.account, sizeof(in.account), "%s", "A");
                  in.order_id = UINT64_MAX;
                  in.cl_ord_id = 0;
              });

    ROUNDTRIP(msg_cancel_ack_t, msg_encode_cancel_ack, msg_decode_cancel_ack,
              MSG_CANCEL_ACK_LEN, {
                  in.order_id = 7;
                  in.cl_ord_id = 8;
                  in.status = 3;
                  in.reason = -9;
                  in.canceled_qty = 12345;
              });

    ROUNDTRIP(msg_modify_req_t, msg_encode_modify_req, msg_decode_modify_req,
              MSG_MODIFY_REQ_LEN, {
                  snprintf(in.account, sizeof(in.account), "%s", "ACC");
                  in.order_id = 100;
                  in.cl_ord_id = 200;
                  in.new_price = 9999;
                  in.new_qty = -5;
              });

    ROUNDTRIP(msg_modify_ack_t, msg_encode_modify_ack, msg_decode_modify_ack,
              MSG_MODIFY_ACK_LEN, {
                  in.order_id = 100;
                  in.cl_ord_id = 200;
                  in.status = 1;
                  in.reason = 0;
                  in.price = 9999;
              });

    ROUNDTRIP(msg_query_req_t, msg_encode_query_req, msg_decode_query_req,
              MSG_QUERY_REQ_LEN, {
                  snprintf(in.account, sizeof(in.account), "%s", "Q");
                  in.order_id = 0; /* 전체 조회 */
              });

    ROUNDTRIP(msg_query_ack_t, msg_encode_query_ack, msg_decode_query_ack,
              MSG_QUERY_ACK_LEN, {
                  in.order_id = 5;
                  in.cl_ord_id = 6;
                  snprintf(in.symbol, sizeof(in.symbol), "%s", "000660");
                  in.status = 1;
                  in.price = 70000;
                  in.qty = 10;
                  in.filled_qty = 3;
                  in.last = true;
              });

    ROUNDTRIP(msg_fill_noti_t, msg_encode_fill_noti, msg_decode_fill_noti,
              MSG_FILL_NOTI_LEN, {
                  in.order_id = 1;
                  in.cl_ord_id = 2;
                  snprintf(in.symbol, sizeof(in.symbol), "%s", "X");
                  in.market = 1;
                  in.side = 1;
                  in.price = 1;
                  in.qty = 1;
                  in.remaining_qty = INT32_MAX;
                  in.exec_id = UINT64_MAX;
              });

    ROUNDTRIP(msg_login_req_t, msg_encode_login_req, msg_decode_login_req,
              MSG_LOGIN_REQ_LEN, { snprintf(in.session_id, sizeof(in.session_id), "%s", "FEP-KRX-0000001"); });

    ROUNDTRIP(msg_login_ack_t, msg_encode_login_ack, msg_decode_login_ack,
              MSG_LOGIN_ACK_LEN, { in.result = INT32_MIN; });

    ROUNDTRIP(msg_resend_req_t, msg_encode_resend_req, msg_decode_resend_req,
              MSG_RESEND_REQ_LEN, { in.from_seq = UINT64_MAX; });

    ROUNDTRIP(msg_gap_fill_t, msg_encode_gap_fill, msg_decode_gap_fill,
              MSG_GAP_FILL_LEN, { in.next_seq = UINT64_MAX; });

    ROUNDTRIP(msg_book_req_t, msg_encode_book_req, msg_decode_book_req,
              MSG_BOOK_REQ_LEN, {
                  snprintf(in.symbol, sizeof(in.symbol), "%s", "12345678");
                  in.market = 255;
              });

    ROUNDTRIP(msg_book_ack_t, msg_encode_book_ack, msg_decode_book_ack,
              MSG_BOOK_ACK_LEN, {
                  snprintf(in.symbol, sizeof(in.symbol), "%s", "005930");
                  in.market = 1;
                  in.bid_price[0] = INT32_MAX;
                  in.bid_qty[9] = INT32_MIN;
                  in.ask_price[5] = 70100;
                  in.ask_qty[0] = 1;
                  in.last_price = 69950;
                  in.traded_qty = INT64_MAX;
              });

    ROUNDTRIP(msg_book_feed_t, msg_encode_book_feed, msg_decode_book_feed,
              MSG_BOOK_FEED_LEN, {
                  snprintf(in.symbol, sizeof(in.symbol), "%s", "005930");
                  in.market = 1;
                  in.flags = MSG_FEED_END;
                  in.feed_ts = INT64_MIN;
                  in.bid_price[0] = INT32_MAX;
                  in.ask_qty[9] = INT32_MIN;
              });

    ROUNDTRIP(msg_symbol_set_t, msg_encode_symbol_set, msg_decode_symbol_set,
              MSG_SYMBOL_SET_LEN, {
                  snprintf(in.symbol, sizeof(in.symbol), "%s", "000660");
                  in.ref_price = INT32_MAX;
                  in.kind = MSG_SYMBOL_US;
              });

    ROUNDTRIP(msg_symbol_ack_t, msg_encode_symbol_ack, msg_decode_symbol_ack,
              MSG_SYMBOL_ACK_LEN, {
                  snprintf(in.symbol, sizeof(in.symbol), "%s", "000660");
                  in.ref_price = 260000;
                  in.code = INT32_MIN;
                  in.kind = MSG_SYMBOL_US;
              });

    ROUNDTRIP(msg_account_open_t, msg_encode_account_open,
              msg_decode_account_open, MSG_ACCOUNT_OPEN_LEN, {
                  snprintf(in.account, sizeof(in.account), "%s", "u00000000042");
                  in.cash = INT64_MAX;
                  in.pos_qty = 1234;
                  in.pos_cost = INT64_MIN;
              });

    ROUNDTRIP(msg_account_ack_t, msg_encode_account_ack, msg_decode_account_ack,
              MSG_ACCOUNT_ACK_LEN, {
                  snprintf(in.account, sizeof(in.account), "%s", "u00000000042");
                  in.code = INT32_MIN;
                  in.cash = INT64_MIN;
                  in.reserved = INT64_MAX;
                  in.pos_qty = 77;
                  in.pos_cost = 123456789;
                  in.realized = -4242;
              });

    ROUNDTRIP(msg_detail_req_t, msg_encode_detail_req, msg_decode_detail_req,
              MSG_DETAIL_REQ_LEN, {
                  snprintf(in.account, sizeof(in.account), "%s", "123456789012");
                  in.order_id = UINT64_MAX;
              });

    ROUNDTRIP(msg_detail_ack_t, msg_encode_detail_ack, msg_decode_detail_ack,
              MSG_DETAIL_ACK_LEN, {
                  in.order_id = 200000001;
                  in.cl_ord_id = UINT64_MAX;
                  in.reason = INT32_MIN;
                  in.side = 1;
                  in.status = 4;
                  in.market = 255;
                  in.price = INT32_MAX;
                  in.qty = 7;
                  in.filled = 3;
                  in.canceled = 2;
                  in.working = 2;
                  in.notional = INT64_MIN;
                  in.leg_sent[1] = 7;
                  in.leg_filled[0] = 3;
                  in.leg_canceled[1] = 2;
                  in.leg_notional[1] = INT64_MAX;
              });

    ROUNDTRIP(msg_balance_req_t, msg_encode_balance_req, msg_decode_balance_req,
              MSG_BALANCE_REQ_LEN,
              { snprintf(in.account, sizeof(in.account), "%s", "210987654321"); });

    ROUNDTRIP(msg_balance_ack_t, msg_encode_balance_ack, msg_decode_balance_ack,
              MSG_BALANCE_ACK_LEN, {
                  snprintf(in.account, sizeof(in.account), "%s", "123456789012");
                  in.reason = -9;
                  in.cash = INT64_MAX;
                  in.reserved = 1400000;
              });
}

/* --- 5. 거절 --- */

/*
 * 자리가 모자라면 **한 바이트도 쓰지 않는다.** 절반만 쓴 버퍼를 그대로 보내면
 * 상대가 필드가 밀린 전문을 받는다.
 */
static void test_encode_rejects(void)
{
    msg_order_req_t m;
    memset(&m, 0, sizeof(m));
    m.price = 100;

    uint8_t buf[MSG_ORDER_REQ_LEN];
    assert(msg_encode_order_req(NULL, buf, sizeof(buf)) == ERR_NULL_PTR);
    assert(msg_encode_order_req(&m, NULL, sizeof(buf)) == ERR_NULL_PTR);

    for (size_t cap = 0; cap < MSG_ORDER_REQ_LEN; cap++) {
        uint8_t probe[MSG_ORDER_REQ_LEN];
        memset(probe, 0x5A, sizeof(probe));
        assert(msg_encode_order_req(&m, probe, cap) == ERR_INVALID_ARG);
        for (size_t i = 0; i < sizeof(probe); i++) {
            assert(probe[i] == 0x5A);
        }
    }
    assert(msg_encode_order_req(&m, buf, MSG_ORDER_REQ_LEN) ==
           MSG_ORDER_REQ_LEN);

    /* 나머지 전문도 같은 규약을 지킨다. */
    msg_fill_noti_t f;
    memset(&f, 0, sizeof(f));
    uint8_t fb[MSG_FILL_NOTI_LEN];
    assert(msg_encode_fill_noti(&f, fb, MSG_FILL_NOTI_LEN - 1) ==
           ERR_INVALID_ARG);
    assert(msg_encode_fill_noti(&f, fb, MSG_FILL_NOTI_LEN) ==
           MSG_FILL_NOTI_LEN);
}

/*
 * 완료 조건 5 — **길이가 규격과 다르면 거절한다.**
 *
 * 짧으면 필드가 모자라고, 길면 상대가 다른 규격이다. 긴 쪽을 허용해 앞부분만
 * 읽으면 "호환되는 것처럼" 동작하다가 필드가 재배치된 판을 만나 조용히 틀린다.
 */
static void test_decode_rejects_wrong_length(void)
{
    msg_order_req_t m;
    memset(&m, 0, sizeof(m));

    uint8_t buf[MSG_ORDER_REQ_LEN + 8];
    memset(buf, 0, sizeof(buf));
    assert(msg_encode_order_req(&m, buf, sizeof(buf)) == MSG_ORDER_REQ_LEN);

    msg_order_req_t out;
    assert(msg_decode_order_req(NULL, MSG_ORDER_REQ_LEN, &out) == ERR_NULL_PTR);
    assert(msg_decode_order_req(buf, MSG_ORDER_REQ_LEN, NULL) == ERR_NULL_PTR);

    /* 짧은 것 전부. */
    for (size_t len = 0; len < MSG_ORDER_REQ_LEN; len++) {
        assert(msg_decode_order_req(buf, len, &out) == ERR_INVALID_ARG);
    }
    /* 딱 맞는 것만 통과. */
    assert(msg_decode_order_req(buf, MSG_ORDER_REQ_LEN, &out) ==
           MSG_ORDER_REQ_LEN);
    /* 긴 것도 거절한다. */
    for (size_t len = MSG_ORDER_REQ_LEN + 1; len <= sizeof(buf); len++) {
        assert(msg_decode_order_req(buf, len, &out) == ERR_INVALID_ARG);
    }

    /* 나머지 전문도 같다. 규격 길이 ±1을 확인한다. */
#define CHECK_LEN(DEC, TYPE, LEN)                                           \
    do {                                                                   \
        uint8_t bb[(LEN) + 1];                                             \
        memset(bb, 0, sizeof(bb));                                         \
        TYPE oo;                                                           \
        assert(DEC(bb, (LEN) - 1, &oo) == ERR_INVALID_ARG);                \
        assert(DEC(bb, (LEN), &oo) == (LEN));                              \
        assert(DEC(bb, (LEN) + 1, &oo) == ERR_INVALID_ARG);                \
    } while (0)

    CHECK_LEN(msg_decode_order_ack, msg_order_ack_t, MSG_ORDER_ACK_LEN);
    CHECK_LEN(msg_decode_cancel_req, msg_cancel_req_t, MSG_CANCEL_REQ_LEN);
    CHECK_LEN(msg_decode_cancel_ack, msg_cancel_ack_t, MSG_CANCEL_ACK_LEN);
    CHECK_LEN(msg_decode_modify_req, msg_modify_req_t, MSG_MODIFY_REQ_LEN);
    CHECK_LEN(msg_decode_modify_ack, msg_modify_ack_t, MSG_MODIFY_ACK_LEN);
    CHECK_LEN(msg_decode_query_req, msg_query_req_t, MSG_QUERY_REQ_LEN);
    CHECK_LEN(msg_decode_query_ack, msg_query_ack_t, MSG_QUERY_ACK_LEN);
    CHECK_LEN(msg_decode_fill_noti, msg_fill_noti_t, MSG_FILL_NOTI_LEN);
    CHECK_LEN(msg_decode_book_req, msg_book_req_t, MSG_BOOK_REQ_LEN);
    CHECK_LEN(msg_decode_book_ack, msg_book_ack_t, MSG_BOOK_ACK_LEN);
    CHECK_LEN(msg_decode_detail_req, msg_detail_req_t, MSG_DETAIL_REQ_LEN);
    CHECK_LEN(msg_decode_detail_ack, msg_detail_ack_t, MSG_DETAIL_ACK_LEN);
    CHECK_LEN(msg_decode_balance_req, msg_balance_req_t, MSG_BALANCE_REQ_LEN);
    CHECK_LEN(msg_decode_balance_ack, msg_balance_ack_t, MSG_BALANCE_ACK_LEN);
    CHECK_LEN(msg_decode_book_feed, msg_book_feed_t, MSG_BOOK_FEED_LEN);
    CHECK_LEN(msg_decode_symbol_set, msg_symbol_set_t, MSG_SYMBOL_SET_LEN);
    CHECK_LEN(msg_decode_symbol_ack, msg_symbol_ack_t, MSG_SYMBOL_ACK_LEN);
    CHECK_LEN(msg_decode_account_open, msg_account_open_t,
              MSG_ACCOUNT_OPEN_LEN);
    CHECK_LEN(msg_decode_account_ack, msg_account_ack_t, MSG_ACCOUNT_ACK_LEN);

#undef CHECK_LEN
}

/*
 * 헤더와 바디가 함께 쓰이는 방식 — 헤더의 body_len을 **종별 표와 대조한다.**
 * 이것이 T3-09(전문 조립)가 쓰게 될 검사다.
 */
static void test_header_and_body_agree(void)
{
    msg_order_req_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.symbol, sizeof(m.symbol), "%s", "005930");
    m.price = 10000;
    m.qty = 10;

    uint8_t frame[WIRE_HEADER_LEN + MSG_ORDER_REQ_LEN];

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = MSG_ORDER_REQ;
    h.body_len = MSG_ORDER_REQ_LEN;
    h.seq = 42;
    h.ts = 1234567890;

    assert(wire_encode_header(&h, frame, sizeof(frame)) ==
           (int)WIRE_HEADER_LEN);
    assert(msg_encode_order_req(&m, frame + WIRE_HEADER_LEN,
                                sizeof(frame) - WIRE_HEADER_LEN) ==
           MSG_ORDER_REQ_LEN);

    wire_header_t got;
    assert(wire_decode_header(frame, sizeof(frame), &got) ==
           (int)WIRE_HEADER_LEN);
    assert(msg_is_known(got.type));
    /* 헤더가 말하는 길이가 종별의 규격 길이와 같아야 한다. */
    assert((int32_t)got.body_len == msg_body_len(got.type));

    msg_order_req_t out;
    assert(msg_decode_order_req(frame + WIRE_HEADER_LEN, got.body_len, &out) ==
           MSG_ORDER_REQ_LEN);
    assert(strcmp(out.symbol, "005930") == 0);
    assert(out.price == 10000 && out.qty == 10);

    /* 거짓말하는 헤더는 종별 표와 대조해서 잡는다. */
    wire_put_u32(frame + 4, MSG_ORDER_REQ_LEN + 1);
    assert(wire_decode_header(frame, sizeof(frame), &got) ==
           (int)WIRE_HEADER_LEN);
    assert((int32_t)got.body_len != msg_body_len(got.type));
}

int main(void)
{
    test_type_table();
    test_unknown_types();
    test_reply_pairs();
    test_order_req_layout();
    test_fill_noti_layout();
    test_book_ack_layout();
    test_book_feed_layout();
    test_symbol_set_layout();
    test_detail_ack_layout();
    test_balance_ack_layout();
    test_roundtrip_all();
    test_encode_rejects();
    test_decode_rejects_wrong_length();
    test_header_and_body_agree();
    return 0;
}
