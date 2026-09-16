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
