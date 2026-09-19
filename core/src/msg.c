#include "msg.h"

#include <stddef.h>
#include <string.h>

#include "errors.h"

/* --- 종별 표 --- */

const char *msg_type_str(uint8_t type)
{
#define MSG_NAME_CASE(name, code, len, text)                                \
    case (code):                                                           \
        return (text);

    switch (type) {
        MSG_TYPE_LIST(MSG_NAME_CASE)
    default:
        return "알 수 없는 전문";
    }

#undef MSG_NAME_CASE
}

int32_t msg_body_len(uint8_t type)
{
#define MSG_LEN_CASE(name, code, len, text)                                \
    case (code):                                                           \
        return (int32_t)(len);

    switch (type) {
        MSG_TYPE_LIST(MSG_LEN_CASE)
    default:
        return -1;
    }

#undef MSG_LEN_CASE
}

bool msg_is_known(uint8_t type)
{
    return msg_body_len(type) >= 0;
}

msg_type_t msg_reply_type(uint8_t req_type)
{
    switch (req_type) {
    case MSG_ORDER_REQ:
        return MSG_ORDER_ACK;
    case MSG_CANCEL_REQ:
        return MSG_CANCEL_ACK;
    case MSG_MODIFY_REQ:
        return MSG_MODIFY_ACK;
    case MSG_QUERY_REQ:
        return MSG_QUERY_ACK;
    case MSG_BOOK_REQ:
        return MSG_BOOK_ACK;
    /*
     * 스냅샷 주입의 답은 **심은 뒤의 호가창**이다(T8-02). 응답 종별을 새로 만들지
     * 않는다 — 보낸 쪽이 알고 싶은 것이 정확히 "그래서 지금 호가창이 어떻게 됐나"다.
     */
    case MSG_BOOK_FEED:
        return MSG_BOOK_ACK;
    case MSG_SYMBOL_SET:
        return MSG_SYMBOL_ACK;
    case MSG_DETAIL_REQ:
        return MSG_DETAIL_ACK;
    case MSG_BALANCE_REQ:
        return MSG_BALANCE_ACK;
    default:
        /* 체결 통보와 응답 종별은 짝이 없다. 모르는 종별도 마찬가지다. */
        return MSG_UNKNOWN;
    }
}

/* --- 공통 검사 --- */

/*
 * 인코딩 자리 검사. 모자라면 **한 바이트도 쓰지 않는다** — 절반만 쓴 버퍼를
 * 그대로 보내면 상대가 필드가 밀린 전문을 받는다.
 */
static int enc_check(const void *m, const uint8_t *buf, size_t cap, size_t need)
{
    if (m == NULL || buf == NULL) {
        return ERR_NULL_PTR;
    }
    if (cap < need) {
        return ERR_INVALID_ARG;
    }
    return ERR_OK;
}

/*
 * 디코딩 길이 검사. **정확히 같아야** 한다.
 *
 * 짧으면 필드가 모자라고, 길면 상대가 다른 규격이다. 긴 쪽을 허용해 앞부분만
 * 읽으면 "호환되는 것처럼" 동작하다가 필드가 재배치된 판을 만나 조용히 틀린다.
 */
static int dec_check(const uint8_t *buf, size_t len, const void *out,
                     size_t need)
{
    if (buf == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (len != need) {
        return ERR_INVALID_ARG;
    }
    return ERR_OK;
}

/* --- 주문 --- */

int msg_encode_order_req(const msg_order_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_ORDER_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_ACCOUNT_LEN, m->account);
    p += MSG_ACCOUNT_LEN;
    wire_put_str(p, MSG_SYMBOL_LEN, m->symbol);
    p += MSG_SYMBOL_LEN;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;
    wire_put_u8(p++, m->side);
    wire_put_u8(p++, m->type);
    wire_put_u8(p++, m->market);
    wire_put_i32(p, m->price);
    p += 4;
    wire_put_i32(p, m->qty);
    p += 4;

    return (int)(p - buf);
}

int msg_decode_order_req(const uint8_t *buf, size_t len, msg_order_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_ORDER_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_ACCOUNT_LEN, out->account);
    p += MSG_ACCOUNT_LEN;
    wire_get_str(p, MSG_SYMBOL_LEN, out->symbol);
    p += MSG_SYMBOL_LEN;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;
    out->side = wire_get_u8(p++);
    out->type = wire_get_u8(p++);
    out->market = wire_get_u8(p++);
    out->price = wire_get_i32(p);
    p += 4;
    out->qty = wire_get_i32(p);
    p += 4;

    return (int)(p - buf);
}

int msg_encode_order_ack(const msg_order_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_ORDER_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;
    wire_put_u64(p, m->order_id);
    p += 8;
    wire_put_u8(p++, m->status);
    wire_put_i32(p, m->reason);
    p += 4;
    wire_put_i32(p, m->filled_qty);
    p += 4;
    wire_put_i32(p, m->price);
    p += 4;

    return (int)(p - buf);
}

int msg_decode_order_ack(const uint8_t *buf, size_t len, msg_order_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_ORDER_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;
    out->order_id = wire_get_u64(p);
    p += 8;
    out->status = wire_get_u8(p++);
    out->reason = wire_get_i32(p);
    p += 4;
    out->filled_qty = wire_get_i32(p);
    p += 4;
    out->price = wire_get_i32(p);
    p += 4;

    return (int)(p - buf);
}

/* --- 취소 --- */

int msg_encode_cancel_req(const msg_cancel_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_CANCEL_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_ACCOUNT_LEN, m->account);
    p += MSG_ACCOUNT_LEN;
    wire_put_u64(p, m->order_id);
    p += 8;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;

    return (int)(p - buf);
}

int msg_decode_cancel_req(const uint8_t *buf, size_t len, msg_cancel_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_CANCEL_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_ACCOUNT_LEN, out->account);
    p += MSG_ACCOUNT_LEN;
    out->order_id = wire_get_u64(p);
    p += 8;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;

    return (int)(p - buf);
}

int msg_encode_cancel_ack(const msg_cancel_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_CANCEL_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_u64(p, m->order_id);
    p += 8;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;
    wire_put_u8(p++, m->status);
    wire_put_i32(p, m->reason);
    p += 4;
    wire_put_i32(p, m->canceled_qty);
    p += 4;

    return (int)(p - buf);
}

int msg_decode_cancel_ack(const uint8_t *buf, size_t len, msg_cancel_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_CANCEL_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->order_id = wire_get_u64(p);
    p += 8;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;
    out->status = wire_get_u8(p++);
    out->reason = wire_get_i32(p);
    p += 4;
    out->canceled_qty = wire_get_i32(p);
    p += 4;

    return (int)(p - buf);
}

/* --- 정정 --- */

int msg_encode_modify_req(const msg_modify_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_MODIFY_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_ACCOUNT_LEN, m->account);
    p += MSG_ACCOUNT_LEN;
    wire_put_u64(p, m->order_id);
    p += 8;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;
    wire_put_i32(p, m->new_price);
    p += 4;
    wire_put_i32(p, m->new_qty);
    p += 4;

    return (int)(p - buf);
}

int msg_decode_modify_req(const uint8_t *buf, size_t len, msg_modify_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_MODIFY_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_ACCOUNT_LEN, out->account);
    p += MSG_ACCOUNT_LEN;
    out->order_id = wire_get_u64(p);
    p += 8;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;
    out->new_price = wire_get_i32(p);
    p += 4;
    out->new_qty = wire_get_i32(p);
    p += 4;

    return (int)(p - buf);
}

int msg_encode_modify_ack(const msg_modify_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_MODIFY_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_u64(p, m->order_id);
    p += 8;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;
    wire_put_u8(p++, m->status);
    wire_put_i32(p, m->reason);
    p += 4;
    wire_put_i32(p, m->price);
    p += 4;

    return (int)(p - buf);
}

int msg_decode_modify_ack(const uint8_t *buf, size_t len, msg_modify_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_MODIFY_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->order_id = wire_get_u64(p);
    p += 8;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;
    out->status = wire_get_u8(p++);
    out->reason = wire_get_i32(p);
    p += 4;
    out->price = wire_get_i32(p);
    p += 4;

    return (int)(p - buf);
}

/* --- 조회 --- */

int msg_encode_query_req(const msg_query_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_QUERY_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_ACCOUNT_LEN, m->account);
    p += MSG_ACCOUNT_LEN;
    wire_put_u64(p, m->order_id);
    p += 8;

    return (int)(p - buf);
}

int msg_decode_query_req(const uint8_t *buf, size_t len, msg_query_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_QUERY_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_ACCOUNT_LEN, out->account);
    p += MSG_ACCOUNT_LEN;
    out->order_id = wire_get_u64(p);
    p += 8;

    return (int)(p - buf);
}

int msg_encode_query_ack(const msg_query_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_QUERY_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_u64(p, m->order_id);
    p += 8;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;
    wire_put_str(p, MSG_SYMBOL_LEN, m->symbol);
    p += MSG_SYMBOL_LEN;
    wire_put_u8(p++, m->status);
    wire_put_i32(p, m->price);
    p += 4;
    wire_put_i32(p, m->qty);
    p += 4;
    wire_put_i32(p, m->filled_qty);
    p += 4;

    wire_put_u8(p++, m->last ? 1u : 0u);
    return (int)(p - buf);
}

int msg_decode_query_ack(const uint8_t *buf, size_t len, msg_query_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_QUERY_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->order_id = wire_get_u64(p);
    p += 8;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;
    wire_get_str(p, MSG_SYMBOL_LEN, out->symbol);
    p += MSG_SYMBOL_LEN;
    out->status = wire_get_u8(p++);
    out->price = wire_get_i32(p);
    p += 4;
    out->qty = wire_get_i32(p);
    p += 4;
    out->filled_qty = wire_get_i32(p);
    p += 4;

    out->last = (wire_get_u8(p++) != 0);
    return (int)(p - buf);
}

/* --- 체결 통보 --- */

int msg_encode_fill_noti(const msg_fill_noti_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_FILL_NOTI_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_u64(p, m->order_id);
    p += 8;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;
    wire_put_str(p, MSG_SYMBOL_LEN, m->symbol);
    p += MSG_SYMBOL_LEN;
    wire_put_u8(p++, m->market);
    wire_put_u8(p++, m->side);
    wire_put_i32(p, m->price);
    p += 4;
    wire_put_i32(p, m->qty);
    p += 4;
    wire_put_i32(p, m->remaining_qty);
    p += 4;
    wire_put_u64(p, m->exec_id);
    p += 8;

    return (int)(p - buf);
}

int msg_decode_fill_noti(const uint8_t *buf, size_t len, msg_fill_noti_t *out)
{
    int rc = dec_check(buf, len, out, MSG_FILL_NOTI_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->order_id = wire_get_u64(p);
    p += 8;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;
    wire_get_str(p, MSG_SYMBOL_LEN, out->symbol);
    p += MSG_SYMBOL_LEN;
    out->market = wire_get_u8(p++);
    out->side = wire_get_u8(p++);
    out->price = wire_get_i32(p);
    p += 4;
    out->qty = wire_get_i32(p);
    p += 4;
    out->remaining_qty = wire_get_i32(p);
    p += 4;
    out->exec_id = wire_get_u64(p);
    p += 8;

    return (int)(p - buf);
}

int msg_encode_login_req(const msg_login_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_LOGIN_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_SESSION_LEN, m->session_id);
    p += MSG_SESSION_LEN;

    return (int)(p - buf);
}

int msg_decode_login_req(const uint8_t *buf, size_t len, msg_login_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_LOGIN_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_SESSION_LEN, out->session_id);
    p += MSG_SESSION_LEN;

    return (int)(p - buf);
}

int msg_encode_login_ack(const msg_login_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_LOGIN_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_i32(p, m->result);
    p += 4;

    return (int)(p - buf);
}

int msg_decode_login_ack(const uint8_t *buf, size_t len, msg_login_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_LOGIN_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->result = wire_get_i32(p);
    p += 4;

    return (int)(p - buf);
}

int msg_encode_resend_req(const msg_resend_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_RESEND_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_u64(p, m->from_seq);
    p += 8;

    return (int)(p - buf);
}

int msg_decode_resend_req(const uint8_t *buf, size_t len,
                          msg_resend_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_RESEND_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->from_seq = wire_get_u64(p);
    p += 8;

    return (int)(p - buf);
}

int msg_encode_gap_fill(const msg_gap_fill_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_GAP_FILL_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_u64(p, m->next_seq);
    p += 8;

    return (int)(p - buf);
}

int msg_decode_gap_fill(const uint8_t *buf, size_t len, msg_gap_fill_t *out)
{
    int rc = dec_check(buf, len, out, MSG_GAP_FILL_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->next_seq = wire_get_u64(p);
    p += 8;

    return (int)(p - buf);
}

/* --- 호가창 조회 --- */

int msg_encode_book_req(const msg_book_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_BOOK_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_SYMBOL_LEN, m->symbol);
    p += MSG_SYMBOL_LEN;
    wire_put_u8(p++, m->market);
    return (int)(p - buf);
}

int msg_decode_book_req(const uint8_t *buf, size_t len, msg_book_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_BOOK_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_SYMBOL_LEN, out->symbol);
    p += MSG_SYMBOL_LEN;
    out->market = wire_get_u8(p++);
    return (int)(p - buf);
}

/* --- 종목 전환 (T8-10) --- */

int msg_encode_symbol_set(const msg_symbol_set_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_SYMBOL_SET_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_SYMBOL_LEN, m->symbol);
    p += MSG_SYMBOL_LEN;
    wire_put_i32(p, m->ref_price);
    p += 4;
    return (int)(p - buf);
}

int msg_decode_symbol_set(const uint8_t *buf, size_t len, msg_symbol_set_t *out)
{
    int rc = dec_check(buf, len, out, MSG_SYMBOL_SET_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_SYMBOL_LEN, out->symbol);
    p += MSG_SYMBOL_LEN;
    out->ref_price = wire_get_i32(p);
    p += 4;
    return (int)(p - buf);
}

int msg_encode_symbol_ack(const msg_symbol_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_SYMBOL_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_SYMBOL_LEN, m->symbol);
    p += MSG_SYMBOL_LEN;
    wire_put_i32(p, m->ref_price);
    p += 4;
    wire_put_i32(p, m->code);
    p += 4;
    return (int)(p - buf);
}

int msg_decode_symbol_ack(const uint8_t *buf, size_t len, msg_symbol_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_SYMBOL_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_SYMBOL_LEN, out->symbol);
    p += MSG_SYMBOL_LEN;
    out->ref_price = wire_get_i32(p);
    p += 4;
    out->code = wire_get_i32(p);
    p += 4;
    return (int)(p - buf);
}

static uint8_t *put_i32s(uint8_t *p, const int32_t *v)
{
    for (int i = 0; i < MSG_BOOK_DEPTH; i++) {
        wire_put_i32(p, v[i]);
        p += 4;
    }
    return p;
}

static const uint8_t *get_i32s(const uint8_t *p, int32_t *v)
{
    for (int i = 0; i < MSG_BOOK_DEPTH; i++) {
        v[i] = wire_get_i32(p);
        p += 4;
    }
    return p;
}

int msg_encode_book_ack(const msg_book_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_BOOK_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_SYMBOL_LEN, m->symbol);
    p += MSG_SYMBOL_LEN;
    wire_put_u8(p++, m->market);
    p = put_i32s(p, m->bid_price);
    p = put_i32s(p, m->bid_qty);
    p = put_i32s(p, m->ask_price);
    p = put_i32s(p, m->ask_qty);
    wire_put_i32(p, m->last_price);
    p += 4;
    wire_put_i64(p, m->traded_qty);
    p += 8;
    return (int)(p - buf);
}

int msg_decode_book_ack(const uint8_t *buf, size_t len, msg_book_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_BOOK_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_SYMBOL_LEN, out->symbol);
    p += MSG_SYMBOL_LEN;
    out->market = wire_get_u8(p++);
    p = get_i32s(p, out->bid_price);
    p = get_i32s(p, out->bid_qty);
    p = get_i32s(p, out->ask_price);
    p = get_i32s(p, out->ask_qty);
    out->last_price = wire_get_i32(p);
    p += 4;
    out->traded_qty = wire_get_i64(p);
    p += 8;
    return (int)(p - buf);
}

/* --- 호가 스냅샷 주입 (T8-02) --- */

int msg_encode_book_feed(const msg_book_feed_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_BOOK_FEED_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_SYMBOL_LEN, m->symbol);
    p += MSG_SYMBOL_LEN;
    wire_put_u8(p++, m->market);
    wire_put_u8(p++, m->flags);
    wire_put_i64(p, m->feed_ts);
    p += 8;
    p = put_i32s(p, m->bid_price);
    p = put_i32s(p, m->bid_qty);
    p = put_i32s(p, m->ask_price);
    p = put_i32s(p, m->ask_qty);
    return (int)(p - buf);
}

int msg_decode_book_feed(const uint8_t *buf, size_t len, msg_book_feed_t *out)
{
    int rc = dec_check(buf, len, out, MSG_BOOK_FEED_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_SYMBOL_LEN, out->symbol);
    p += MSG_SYMBOL_LEN;
    out->market = wire_get_u8(p++);
    out->flags = wire_get_u8(p++);
    out->feed_ts = wire_get_i64(p);
    p += 8;
    p = get_i32s(p, out->bid_price);
    p = get_i32s(p, out->bid_qty);
    p = get_i32s(p, out->ask_price);
    p = get_i32s(p, out->ask_qty);
    return (int)(p - buf);
}

/* --- 주문 상세·잔고 조회 (T7-02) --- */

_Static_assert(MSG_LEG_SLOTS == MARKET_COUNT,
               "MSG_LEG_SLOTS는 시장 수와 같아야 한다");

int msg_encode_detail_req(const msg_detail_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_DETAIL_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_ACCOUNT_LEN, m->account);
    p += MSG_ACCOUNT_LEN;
    wire_put_u64(p, m->order_id);
    p += 8;
    return (int)(p - buf);
}

int msg_decode_detail_req(const uint8_t *buf, size_t len, msg_detail_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_DETAIL_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_ACCOUNT_LEN, out->account);
    p += MSG_ACCOUNT_LEN;
    out->order_id = wire_get_u64(p);
    p += 8;
    return (int)(p - buf);
}

int msg_encode_detail_ack(const msg_detail_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_DETAIL_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_u64(p, m->order_id);
    p += 8;
    wire_put_u64(p, m->cl_ord_id);
    p += 8;
    wire_put_i32(p, m->reason);
    p += 4;
    wire_put_u8(p++, m->side);
    wire_put_u8(p++, m->status);
    wire_put_u8(p++, m->market);
    wire_put_i32(p, m->price);
    p += 4;
    wire_put_i32(p, m->qty);
    p += 4;
    wire_put_i32(p, m->filled);
    p += 4;
    wire_put_i32(p, m->canceled);
    p += 4;
    wire_put_i32(p, m->working);
    p += 4;
    wire_put_i64(p, m->notional);
    p += 8;
    for (int i = 0; i < MSG_LEG_SLOTS; i++, p += 4) {
        wire_put_i32(p, m->leg_sent[i]);
    }
    for (int i = 0; i < MSG_LEG_SLOTS; i++, p += 4) {
        wire_put_i32(p, m->leg_filled[i]);
    }
    for (int i = 0; i < MSG_LEG_SLOTS; i++, p += 4) {
        wire_put_i32(p, m->leg_canceled[i]);
    }
    for (int i = 0; i < MSG_LEG_SLOTS; i++, p += 8) {
        wire_put_i64(p, m->leg_notional[i]);
    }
    return (int)(p - buf);
}

int msg_decode_detail_ack(const uint8_t *buf, size_t len, msg_detail_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_DETAIL_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    out->order_id = wire_get_u64(p);
    p += 8;
    out->cl_ord_id = wire_get_u64(p);
    p += 8;
    out->reason = wire_get_i32(p);
    p += 4;
    out->side = wire_get_u8(p++);
    out->status = wire_get_u8(p++);
    out->market = wire_get_u8(p++);
    out->price = wire_get_i32(p);
    p += 4;
    out->qty = wire_get_i32(p);
    p += 4;
    out->filled = wire_get_i32(p);
    p += 4;
    out->canceled = wire_get_i32(p);
    p += 4;
    out->working = wire_get_i32(p);
    p += 4;
    out->notional = wire_get_i64(p);
    p += 8;
    for (int i = 0; i < MSG_LEG_SLOTS; i++, p += 4) {
        out->leg_sent[i] = wire_get_i32(p);
    }
    for (int i = 0; i < MSG_LEG_SLOTS; i++, p += 4) {
        out->leg_filled[i] = wire_get_i32(p);
    }
    for (int i = 0; i < MSG_LEG_SLOTS; i++, p += 4) {
        out->leg_canceled[i] = wire_get_i32(p);
    }
    for (int i = 0; i < MSG_LEG_SLOTS; i++, p += 8) {
        out->leg_notional[i] = wire_get_i64(p);
    }
    return (int)(p - buf);
}

int msg_encode_balance_req(const msg_balance_req_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_BALANCE_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }
    wire_put_str(buf, MSG_ACCOUNT_LEN, m->account);
    return MSG_BALANCE_REQ_LEN;
}

int msg_decode_balance_req(const uint8_t *buf, size_t len,
                           msg_balance_req_t *out)
{
    int rc = dec_check(buf, len, out, MSG_BALANCE_REQ_LEN);
    if (rc != ERR_OK) {
        return rc;
    }
    memset(out, 0, sizeof(*out));
    wire_get_str(buf, MSG_ACCOUNT_LEN, out->account);
    return MSG_BALANCE_REQ_LEN;
}

int msg_encode_balance_ack(const msg_balance_ack_t *m, uint8_t *buf, size_t cap)
{
    int rc = enc_check(m, buf, cap, MSG_BALANCE_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    uint8_t *p = buf;
    wire_put_str(p, MSG_ACCOUNT_LEN, m->account);
    p += MSG_ACCOUNT_LEN;
    wire_put_i32(p, m->reason);
    p += 4;
    wire_put_i64(p, m->cash);
    p += 8;
    wire_put_i64(p, m->reserved);
    p += 8;
    return (int)(p - buf);
}

int msg_decode_balance_ack(const uint8_t *buf, size_t len,
                           msg_balance_ack_t *out)
{
    int rc = dec_check(buf, len, out, MSG_BALANCE_ACK_LEN);
    if (rc != ERR_OK) {
        return rc;
    }

    memset(out, 0, sizeof(*out));

    const uint8_t *p = buf;
    wire_get_str(p, MSG_ACCOUNT_LEN, out->account);
    p += MSG_ACCOUNT_LEN;
    out->reason = wire_get_i32(p);
    p += 4;
    out->cash = wire_get_i64(p);
    p += 8;
    out->reserved = wire_get_i64(p);
    p += 8;
    return (int)(p - buf);
}

