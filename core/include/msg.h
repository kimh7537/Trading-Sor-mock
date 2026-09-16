#ifndef MINI_SOR_MSG_H
#define MINI_SOR_MSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "types.h"
#include "wire.h"

/*
 * 전문 종별과 바디 배치.
 *
 * **이 형식은 이 프로젝트의 자체 설계다** (T3-01과 같다). 업무상 접한 규격이나
 * 내부 문서를 참고하지 않는다.
 *
 * ===========================================================================
 * 종별 목록을 한 군데에 둔다
 * ===========================================================================
 *
 * 종별 코드, 이름, 바디 길이를 **하나의 X 매크로 목록**에서 함께 만든다.
 * errors.h와 같은 방식이고 이유도 같다 — 셋을 따로 관리하면 종별을 추가할 때
 * 한쪽만 빠뜨린다. 그런 누락은 "모르는 종별"로 조용히 거절되어, 어디가 틀렸는지
 * 찾는 데 한참 걸린다.
 *
 * ===========================================================================
 * 문자열 필드 길이
 * ===========================================================================
 *
 * 계좌번호 12바이트, 종목코드 8바이트. 국내 종목코드는 6자리지만 여유를 둔다.
 * 고정 길이라 널 종료를 보장하지 않는다 — 전문은 길이가 규격이다(T3-01).
 *
 * ===========================================================================
 * 바디 배치 (전부 빅엔디언, 채움 바이트 없음)
 * ===========================================================================
 *
 * 직렬화를 바이트 단위로 하므로 **정렬을 맞출 이유가 없다.** 채움 바이트를 두지
 * 않는 것이 규격을 짧고 명확하게 만든다.
 *
 * ORDER_REQ (39)   account[12] symbol[8] cl_ord_id:u64 side:u8 type:u8
 *                  market:u8 price:i32 qty:i32
 * ORDER_ACK (29)   cl_ord_id:u64 order_id:u64 status:u8 reason:i32
 *                  filled_qty:i32 price:i32
 * CANCEL_REQ (28)  account[12] order_id:u64 cl_ord_id:u64
 * CANCEL_ACK (25)  order_id:u64 cl_ord_id:u64 status:u8 reason:i32
 *                  canceled_qty:i32
 * MODIFY_REQ (36)  account[12] order_id:u64 cl_ord_id:u64 new_price:i32
 *                  new_qty:i32
 * MODIFY_ACK (25)  order_id:u64 cl_ord_id:u64 status:u8 reason:i32 price:i32
 * QUERY_REQ (20)   account[12] order_id:u64   (0이면 전체 조회)
 * QUERY_ACK (38)   order_id:u64 cl_ord_id:u64 symbol[8] status:u8 price:i32
 *                  qty:i32 filled_qty:i32 last:u8
 * FILL_NOTI (46)   order_id:u64 cl_ord_id:u64 symbol[8] market:u8 side:u8
 *                  price:i32 qty:i32 remaining_qty:i32 exec_id:u64
 * LOGIN_REQ (16)  session_id[16]
 * LOGIN_ACK (4)    result:i32
 * HEARTBEAT (0)    바디 없음 — 헤더의 seq와 ts가 전부다
 * RESEND_REQ (8)  from_seq:u64   (from_seq부터 지금까지 전부 다시)
 * GAP_FILL (8)     next_seq:u64   (그 앞은 더 없다. next_seq부터 이어라)
 *
 * ===========================================================================
 * 요청과 응답
 * ===========================================================================
 *
 * 요청 종별마다 응답 종별이 하나씩 짝지어 있다(`msg_reply_type`). 체결 통보만
 * 짝이 없다 — 요청 없이 원장이 밀어 보내는 것이라 응답할 대상이 없다.
 */

#define MSG_ACCOUNT_LEN 12
#define MSG_SYMBOL_LEN 8

/* 세션 식별자. 어느 FEP가 붙었는지 구분할 수 있으면 된다. */
#define MSG_SESSION_LEN 16

/* 바디 길이. 위 표와 같다. 계산식으로 적어 필드를 더할 때 같이 움직이게 한다. */
#define MSG_ORDER_REQ_LEN                                                  \
    (MSG_ACCOUNT_LEN + MSG_SYMBOL_LEN + 8 + 1 + 1 + 1 + 4 + 4)
#define MSG_ORDER_ACK_LEN (8 + 8 + 1 + 4 + 4 + 4)
#define MSG_CANCEL_REQ_LEN (MSG_ACCOUNT_LEN + 8 + 8)
#define MSG_CANCEL_ACK_LEN (8 + 8 + 1 + 4 + 4)
#define MSG_MODIFY_REQ_LEN (MSG_ACCOUNT_LEN + 8 + 8 + 4 + 4)
#define MSG_MODIFY_ACK_LEN (8 + 8 + 1 + 4 + 4)
#define MSG_QUERY_REQ_LEN (MSG_ACCOUNT_LEN + 8)
/*
 * 마지막 1바이트는 "이것이 마지막 응답"이라는 표시다(T3-14).
 *
 * **끝을 모르면 "거래소에 없다"를 결론 낼 수 없다.** 조회 응답이 여러 건으로
 * 오는데 어디까지가 답인지 모르면, 응답에 없던 주문이 *아직 안 온 것*인지
 * *정말 없는 것*인지 구분되지 않는다. 그 구분이 미응답 주문 판정의 전부다.
 */
#define MSG_QUERY_ACK_LEN (8 + 8 + MSG_SYMBOL_LEN + 1 + 4 + 4 + 4 + 1)
#define MSG_FILL_NOTI_LEN (8 + 8 + MSG_SYMBOL_LEN + 1 + 1 + 4 + 4 + 4 + 8)
#define MSG_LOGIN_REQ_LEN (MSG_SESSION_LEN)
#define MSG_LOGIN_ACK_LEN (4)
/* 하트비트는 바디가 없다. 0은 유효한 길이다 — 헤더만으로 뜻이 완성된다. */
#define MSG_HEARTBEAT_LEN (0)
#define MSG_RESEND_REQ_LEN (8)
#define MSG_GAP_FILL_LEN (8)

/*
 * 종별 목록. X(이름, 코드, 바디 길이, 설명).
 *
 * 코드는 0을 쓰지 않는다 — 0으로 초기화된 버퍼가 유효한 종별로 보이면 안 된다.
 */
#define MSG_TYPE_LIST(X)                                                   \
    X(MSG_ORDER_REQ, 1, MSG_ORDER_REQ_LEN, "주문 요청")                    \
    X(MSG_ORDER_ACK, 2, MSG_ORDER_ACK_LEN, "주문 응답")                    \
    X(MSG_CANCEL_REQ, 3, MSG_CANCEL_REQ_LEN, "취소 요청")                  \
    X(MSG_CANCEL_ACK, 4, MSG_CANCEL_ACK_LEN, "취소 응답")                  \
    X(MSG_MODIFY_REQ, 5, MSG_MODIFY_REQ_LEN, "정정 요청")                  \
    X(MSG_MODIFY_ACK, 6, MSG_MODIFY_ACK_LEN, "정정 응답")                  \
    X(MSG_QUERY_REQ, 7, MSG_QUERY_REQ_LEN, "조회 요청")                    \
    X(MSG_QUERY_ACK, 8, MSG_QUERY_ACK_LEN, "조회 응답")                    \
    X(MSG_FILL_NOTI, 9, MSG_FILL_NOTI_LEN, "체결 통보")                    \
    X(MSG_LOGIN_REQ, 10, MSG_LOGIN_REQ_LEN, "로그인 요청")                 \
    X(MSG_LOGIN_ACK, 11, MSG_LOGIN_ACK_LEN, "로그인 응답")                 \
    X(MSG_HEARTBEAT, 12, MSG_HEARTBEAT_LEN, "하트비트")                      \
    X(MSG_RESEND_REQ, 13, MSG_RESEND_REQ_LEN, "재전송 요청")                 \
    X(MSG_GAP_FILL, 14, MSG_GAP_FILL_LEN, "갭 건너뛰기")

#define MSG_ENUM_ENTRY(name, code, len, text) name = (code),

typedef enum {
    MSG_UNKNOWN = 0,
    MSG_TYPE_LIST(MSG_ENUM_ENTRY)
} msg_type_t;

#undef MSG_ENUM_ENTRY

/* 종별 이름. 모르는 값에도 NULL을 반환하지 않는다. */
const char *msg_type_str(uint8_t type);

/* 아는 종별인가. */
bool msg_is_known(uint8_t type);

/*
 * 그 종별의 바디 길이. 모르는 종별이면 -1.
 *
 * **헤더의 body_len을 믿지 않고 이 값과 대조한다.** 길이가 규격과 다른 전문은
 * 필드가 밀려 있다는 뜻이고, 그대로 읽으면 엉뚱한 값을 그럴듯하게 돌려준다.
 */
int32_t msg_body_len(uint8_t type);

/*
 * 요청 종별에 대응하는 응답 종별. 응답이 없는 종별(체결 통보)이면 MSG_UNKNOWN.
 * 요청이 아닌 종별을 줘도 MSG_UNKNOWN.
 */
msg_type_t msg_reply_type(uint8_t req_type);

/* --- 전문 본문 --- */

typedef struct {
    char     account[MSG_ACCOUNT_LEN + 1];
    char     symbol[MSG_SYMBOL_LEN + 1];
    uint64_t cl_ord_id;
    uint8_t  side;
    uint8_t  type;   /* order_type_t */
    uint8_t  market; /* market_t */
    price_t  price;
    qty_t    qty;
} msg_order_req_t;

typedef struct {
    uint64_t   cl_ord_id;
    order_id_t order_id;
    uint8_t    status; /* order_status_t */
    int32_t    reason; /* 거부 이유. 성공이면 ERR_OK */
    qty_t      filled_qty;
    price_t    price;
} msg_order_ack_t;

typedef struct {
    char       account[MSG_ACCOUNT_LEN + 1];
    order_id_t order_id;
    uint64_t   cl_ord_id;
} msg_cancel_req_t;

typedef struct {
    order_id_t order_id;
    uint64_t   cl_ord_id;
    uint8_t    status;
    int32_t    reason;
    qty_t      canceled_qty;
} msg_cancel_ack_t;

typedef struct {
    char       account[MSG_ACCOUNT_LEN + 1];
    order_id_t order_id;
    uint64_t   cl_ord_id;
    price_t    new_price;
    qty_t      new_qty;
} msg_modify_req_t;

typedef struct {
    order_id_t order_id;
    uint64_t   cl_ord_id;
    uint8_t    status;
    int32_t    reason;
    price_t    price;
} msg_modify_ack_t;

typedef struct {
    char       account[MSG_ACCOUNT_LEN + 1];
    order_id_t order_id; /* 0이면 전체 조회 */
} msg_query_req_t;

typedef struct {
    order_id_t order_id;
    uint64_t   cl_ord_id;
    char       symbol[MSG_SYMBOL_LEN + 1];
    uint8_t    status;
    price_t    price;
    qty_t      qty;
    qty_t      filled_qty;
    /*
     * 이 응답이 마지막인가. 조회가 한 건도 걸리지 않아도 **마지막 표시가 붙은
     * 빈 응답 하나는 와야 한다** — 그래야 "없다"가 결론이 된다.
     */
    bool last;
} msg_query_ack_t;

typedef struct {
    order_id_t order_id;
    uint64_t   cl_ord_id;
    char       symbol[MSG_SYMBOL_LEN + 1];
    uint8_t    market;
    uint8_t    side;
    price_t    price;
    qty_t      qty;
    qty_t      remaining_qty;
    uint64_t   exec_id;
} msg_fill_noti_t;

typedef struct {
    char session_id[MSG_SESSION_LEN + 1];
} msg_login_req_t;

typedef struct {
    int32_t result; /* 성공이면 ERR_OK, 아니면 거부 사유 */
} msg_login_ack_t;

typedef struct {
    uint64_t from_seq;
} msg_resend_req_t;

typedef struct {
    uint64_t next_seq;
} msg_gap_fill_t;

/*
 * 인코딩 — 바디만 쓴다. 헤더는 호출부가 wire_encode_header()로 따로 쓴다.
 * 두 일을 합치면 시퀀스 번호와 논리 시각을 여기서 정해야 하는데, 그건 세션의
 * 상태이지 전문의 내용이 아니다(T3-11이 맡는다).
 *
 * 성공하면 쓴 바이트 수, 자리가 모자라면 ERR_INVALID_ARG.
 */
int msg_encode_order_req(const msg_order_req_t *m, uint8_t *buf, size_t cap);
int msg_encode_order_ack(const msg_order_ack_t *m, uint8_t *buf, size_t cap);
int msg_encode_cancel_req(const msg_cancel_req_t *m, uint8_t *buf, size_t cap);
int msg_encode_cancel_ack(const msg_cancel_ack_t *m, uint8_t *buf, size_t cap);
int msg_encode_modify_req(const msg_modify_req_t *m, uint8_t *buf, size_t cap);
int msg_encode_modify_ack(const msg_modify_ack_t *m, uint8_t *buf, size_t cap);
int msg_encode_query_req(const msg_query_req_t *m, uint8_t *buf, size_t cap);
int msg_encode_query_ack(const msg_query_ack_t *m, uint8_t *buf, size_t cap);
int msg_encode_fill_noti(const msg_fill_noti_t *m, uint8_t *buf, size_t cap);
int msg_encode_login_req(const msg_login_req_t *m, uint8_t *buf, size_t cap);
int msg_encode_login_ack(const msg_login_ack_t *m, uint8_t *buf, size_t cap);
int msg_encode_resend_req(const msg_resend_req_t *m, uint8_t *buf, size_t cap);
int msg_encode_gap_fill(const msg_gap_fill_t *m, uint8_t *buf, size_t cap);

/*
 * 디코딩 — 바디 길이가 규격과 **정확히 같아야** 한다. 짧으면 필드가 모자라고,
 * 길면 규격이 다른 상대다. 둘 다 ERR_INVALID_ARG.
 */
int msg_decode_order_req(const uint8_t *buf, size_t len, msg_order_req_t *out);
int msg_decode_order_ack(const uint8_t *buf, size_t len, msg_order_ack_t *out);
int msg_decode_cancel_req(const uint8_t *buf, size_t len,
                          msg_cancel_req_t *out);
int msg_decode_cancel_ack(const uint8_t *buf, size_t len,
                          msg_cancel_ack_t *out);
int msg_decode_modify_req(const uint8_t *buf, size_t len,
                          msg_modify_req_t *out);
int msg_decode_modify_ack(const uint8_t *buf, size_t len,
                          msg_modify_ack_t *out);
int msg_decode_query_req(const uint8_t *buf, size_t len, msg_query_req_t *out);
int msg_decode_query_ack(const uint8_t *buf, size_t len, msg_query_ack_t *out);
int msg_decode_fill_noti(const uint8_t *buf, size_t len, msg_fill_noti_t *out);
int msg_decode_login_req(const uint8_t *buf, size_t len, msg_login_req_t *out);
int msg_decode_login_ack(const uint8_t *buf, size_t len, msg_login_ack_t *out);
int msg_decode_resend_req(const uint8_t *buf, size_t len,
                          msg_resend_req_t *out);
int msg_decode_gap_fill(const uint8_t *buf, size_t len, msg_gap_fill_t *out);

#endif /* MINI_SOR_MSG_H */
