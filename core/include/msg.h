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
 * BOOK_REQ (9)     symbol[8] market:u8
 * BOOK_ACK (181)   symbol[8] market:u8 bid_price:i32[10] bid_qty:i32[10]
 *                  ask_price:i32[10] ask_qty:i32[10] last_price:i32
 *                  traded_qty:i64   (없는 단은 0)
 * DETAIL_REQ (20)  account[12] order_id:u64
 * DETAIL_ACK (91)  order_id:u64 cl_ord_id:u64 reason:i32 side:u8 status:u8
 *                  market:u8 price:i32 qty:i32 filled:i32 canceled:i32
 *                  working:i32 notional:i64 leg_sent:i32[2] leg_filled:i32[2]
 *                  leg_canceled:i32[2] leg_notional:i64[2]   (배열은 KRX, NXT 순)
 * BALANCE_REQ (12) account[12]
 * BALANCE_ACK (32) account[12] reason:i32 cash:i64 reserved:i64
 * BOOK_FEED (178)  symbol[8] market:u8 flags:u8 feed_ts:i64 bid_price:i32[10]
 *                  bid_qty:i32[10] ask_price:i32[10] ask_qty:i32[10]
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

/*
 * ORDER_REQ의 `market`에 이 값이 오면 **시장을 원장이 SOR로 정한다**(T6-03).
 *
 * `market_t`(0=KRX, 1=NXT)에 세 번째 값을 더하지 않고 전문에서만 쓰는 값으로 둔다.
 * 열거형에 넣으면 `MARKET_COUNT`가 3이 되어, 시장마다 도는 반복문이 전부 "SOR"이라는
 * 존재하지 않는 시장까지 돌게 된다. 필드 길이(u8)는 그대로라 전문 배치가 바뀌지 않는다.
 */
#define MSG_MARKET_AUTO 255

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
 * 호가창 조회(T6-04). 화면이 원장 안의 실제 호가를 본다.
 *
 * 단 수는 시세 피드(T5-06)의 상한과 같게 10단이다. 가격과 수량을 **단마다 섞지 않고
 * 배열 넷으로 나눈다** — 채널계 코덱이 "같은 타입 N개"만 알면 읽을 수 있게 한다.
 * 섞어 두면 코덱이 구조체 배열까지 알아야 한다.
 */
#define MSG_BOOK_DEPTH 10
#define MSG_BOOK_REQ_LEN (MSG_SYMBOL_LEN + 1)
/*
 * 호가 응답에 **마지막 체결가와 누적 체결 수량**을 함께 싣는다(T8-09).
 *
 * 봉(OHLCV)은 체결을 묶은 것이라 호가만으로는 만들 수 없다. 체결을 건건이 실어 보내는
 * 테이프 전문을 따로 두는 대신, 이미 주기적으로 읽고 있는 호가 응답에 두 값을 얹는다 —
 * 부르는 쪽이 그 표본을 모으면 봉이 된다. 1초에 한 번 읽으면 1분봉에 60 표본이다.
 *
 * `traded_qty`는 **그 시장에서 지금까지 체결된 총 수량**이다. 차이를 내면 구간 거래량이
 * 나온다. 한 체결은 사는 쪽과 파는 쪽 양쪽에 이벤트가 오므로 **한 번만 센다**.
 */
#define MSG_BOOK_ACK_LEN (MSG_SYMBOL_LEN + 1 + MSG_BOOK_DEPTH * 4 * 4 + 4 + 8)

/*
 * 주문 상세와 잔고 조회(T7-02). 화면의 미체결 목록·잔고가 원장의 실제 상태를 따르게 한다.
 *
 * 시장별 값은 **시장 번호를 첨자로 쓰는 배열**이다(0=KRX, 1=NXT). 다리 목록을 가변 길이로
 * 싣지 않는다 — 한 논리 주문이 한 시장에 다리를 여럿 둘 수도 있어(PLAN_LEGS_MAX) 시장별로
 * 더해서 싣는다. 화면이 알고 싶은 것도 "어느 시장에 얼마"다.
 *
 * `MSG_LEG_SLOTS`는 `MARKET_COUNT`와 같아야 한다(msg.c가 컴파일할 때 확인한다). 열거형 값을
 * 쓰지 않고 숫자로 두는 이유는 채널계 테스트가 이 헤더의 길이 식을 읽어 계산하기 때문이다.
 */
#define MSG_LEG_SLOTS 2
#define MSG_DETAIL_REQ_LEN (MSG_ACCOUNT_LEN + 8)
#define MSG_DETAIL_ACK_LEN                                                 \
    (8 + 8 + 4 + 1 + 1 + 1 + 4 * 5 + 8 + MSG_LEG_SLOTS * 4 * 3 +             \
     MSG_LEG_SLOTS * 8)
#define MSG_BALANCE_REQ_LEN (MSG_ACCOUNT_LEN)
#define MSG_BALANCE_ACK_LEN (MSG_ACCOUNT_LEN + 4 + 8 + 8)

/*
 * 호가 스냅샷 주입(T8-02). 바깥에서 받은 실호가를 원장 호가창에 심는다.
 *
 * 배치는 `BOOK_ACK`에 **피드 시각(i64)**을 더한 것이다 — 응답과 같은 모양이라
 * 코덱이 같은 헬퍼를 쓴다. 시각을 싣는 이유는 원장이 시스템 시각을 읽지 않기
 * 때문이다(CLAUDE.md 결정성). 스냅샷이 자기 시각을 들고 와야 리플레이(T8-06)가
 * 같은 파일에서 같은 결과를 낸다.
 *
 * **단수는 10단 고정이되 모자라면 0으로 채우고 넘치면 자른다.** 토스 오픈 API의
 * 호가 스키마에는 `maxItems`가 없고 예시가 3단·1단이라, 보내는 쪽이 단수를 맞춰
 * 주리라 기대할 수 없다.
 */
/*
 * `flags`의 비트.
 *
 * **바깥 시세가 끝났다는 것을 원장이 스스로 알 수는 없다.** 스냅샷이 잠시 안 오는 것과
 * 피드가 끝난 것은 겉으로 같다. 시간으로 어림하면 장 마감에 가상 참가자가 슬그머니
 * 돌아와 "실시세인 척하는 시뮬"이 된다 — 실제로 그렇게 됐다. 그래서 **보내는 쪽이
 * 명시적으로 끝을 알린다.**
 */
#define MSG_FEED_END 0x01

#define MSG_BOOK_FEED_LEN (MSG_SYMBOL_LEN + 1 + 1 + 8 + MSG_BOOK_DEPTH * 4 * 4)

/*
 * 종목 전환(T8-10).
 *
 * **호가창은 기준가 ±30%(가격 제한폭)만 펼쳐 둔다.** 그래서 다루는 종목을 바꾸려면
 * 그 종목의 가격대를 같이 줘야 한다 — 89,000원짜리 호가창에 260,000원 호가를 심으면
 * 통째로 버려지고 화면에서는 아무 일도 일어나지 않는다. 실제로 그렇게 됐다(T8-05).
 *
 * 원장은 이 전문을 받으면 **그 종목의 원장을 새로 연다.** 미체결 주문과 잔고는
 * 초기화된다 — 이 원장은 한 종목짜리이고, 앞 종목의 주문을 다른 종목의 호가창에
 * 남겨 둘 자리가 없다. 응답의 `code`가 0이면 바뀐 것이고 음수면 그대로다.
 *
 * **기준가 0은 묻기만 하는 것이다.** 아무것도 바꾸지 않고 지금 종목과 기준가를 답한다 —
 * 채널계가 다시 떴을 때 원장이 무엇을 다루고 있는지 맞추는 데 쓴다.
 */
#define MSG_SYMBOL_SET_LEN (MSG_SYMBOL_LEN + 4)
#define MSG_SYMBOL_ACK_LEN (MSG_SYMBOL_LEN + 4 + 4)

/*
 * 계좌 개설(T9-01).
 *
 * 이 원장은 처음에 계좌 하나를 설정에서 받아 열었다. 사용자마다 따로 모의투자를
 * 하려면 **계좌가 붙는 쪽의 요청으로 생겨야 한다** — 채널계가 회원가입을 받은
 * 그 자리에서 원장에 계좌를 연다.
 *
 * **다시 불러도 된다.** 이미 있는 계좌면 아무것도 바꾸지 않고 지금 잔고를 답한다
 * (`code`는 0). 원장은 메모리에만 있어서 껐다 켜면 계좌가 사라지는데, 그때
 * 이미 가입한 사람이 다시 로그인하면 채널계가 이 전문으로 계좌를 되살린다.
 * 없는 계좌를 만들 때만 `cash`를 입금하므로 **다시 불러도 돈이 불어나지 않는다.**
 */
#define MSG_ACCOUNT_OPEN_LEN (MSG_ACCOUNT_LEN + 8)
#define MSG_ACCOUNT_ACK_LEN (MSG_ACCOUNT_LEN + 4 + 8 + 8)

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
    X(MSG_GAP_FILL, 14, MSG_GAP_FILL_LEN, "갭 건너뛰기")                 \
    X(MSG_BOOK_REQ, 15, MSG_BOOK_REQ_LEN, "호가 조회 요청")              \
    X(MSG_BOOK_ACK, 16, MSG_BOOK_ACK_LEN, "호가 조회 응답")              \
    X(MSG_DETAIL_REQ, 17, MSG_DETAIL_REQ_LEN, "주문 상세 요청")           \
    X(MSG_DETAIL_ACK, 18, MSG_DETAIL_ACK_LEN, "주문 상세 응답")           \
    X(MSG_BALANCE_REQ, 19, MSG_BALANCE_REQ_LEN, "잔고 조회 요청")         \
    X(MSG_BALANCE_ACK, 20, MSG_BALANCE_ACK_LEN, "잔고 조회 응답")            \
    X(MSG_BOOK_FEED, 21, MSG_BOOK_FEED_LEN, "호가 스냅샷 주입")           \
    X(MSG_SYMBOL_SET, 22, MSG_SYMBOL_SET_LEN, "종목 전환 요청")           \
    X(MSG_SYMBOL_ACK, 23, MSG_SYMBOL_ACK_LEN, "종목 전환 응답")           \
    X(MSG_ACCOUNT_OPEN, 24, MSG_ACCOUNT_OPEN_LEN, "계좌 개설 요청")       \
    X(MSG_ACCOUNT_ACK, 25, MSG_ACCOUNT_ACK_LEN, "계좌 개설 응답")

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

typedef struct {
    char    symbol[MSG_SYMBOL_LEN + 1];
    uint8_t market; /* market_t */
} msg_book_req_t;

/* 매수는 높은 가격부터, 매도는 낮은 가격부터. 없는 단은 가격·수량 모두 0. */
typedef struct {
    char    symbol[MSG_SYMBOL_LEN + 1];
    uint8_t market;
    price_t bid_price[MSG_BOOK_DEPTH];
    qty_t   bid_qty[MSG_BOOK_DEPTH];
    price_t ask_price[MSG_BOOK_DEPTH];
    qty_t   ask_qty[MSG_BOOK_DEPTH];
    /* 마지막 체결가. 아직 한 건도 없으면 0 */
    price_t last_price;
    /* 이 시장에서 지금까지 체결된 총 수량. 차이가 구간 거래량이다 */
    int64_t traded_qty;
} msg_book_ack_t;

typedef struct {
    char       account[MSG_ACCOUNT_LEN + 1];
    order_id_t order_id;
} msg_detail_req_t;

/* 없는 주문·남의 주문이면 reason = ERR_NOT_FOUND, status = 거부, 나머지 0. */
typedef struct {
    order_id_t order_id;
    uint64_t   cl_ord_id;
    int32_t    reason;
    uint8_t    side;   /* side_t */
    uint8_t    status; /* order_status_t */
    uint8_t    market; /* 주문할 때 고른 시장. MSG_MARKET_AUTO면 SOR */
    price_t    price;  /* 지정가 */
    qty_t      qty;
    qty_t      filled;
    qty_t      canceled;
    qty_t      working; /* 아직 호가창에 살아 있는 수량 */
    int64_t    notional;
    qty_t      leg_sent[MSG_LEG_SLOTS];
    qty_t      leg_filled[MSG_LEG_SLOTS];
    qty_t      leg_canceled[MSG_LEG_SLOTS];
    int64_t    leg_notional[MSG_LEG_SLOTS];
} msg_detail_ack_t;

typedef struct {
    char account[MSG_ACCOUNT_LEN + 1];
} msg_balance_req_t;

typedef struct {
    char    account[MSG_ACCOUNT_LEN + 1];
    int32_t reason; /* 없는 계좌면 ERR_NOT_FOUND, 금액 0 */
    int64_t cash;
    int64_t reserved;
} msg_balance_ack_t;

/*
 * 바깥 시세를 원장 호가창에 심는다(T8-02). 모양은 `msg_book_ack_t` + 피드 시각.
 * 응답은 심은 뒤의 호가창(`MSG_BOOK_ACK`)이다 — 보낸 쪽이 반영 결과를 바로 본다.
 */
typedef struct {
    char    symbol[MSG_SYMBOL_LEN + 1];
    uint8_t market;
    uint8_t flags; /* MSG_FEED_END 등 */
    ts_t    feed_ts;
    price_t bid_price[MSG_BOOK_DEPTH];
    qty_t   bid_qty[MSG_BOOK_DEPTH];
    price_t ask_price[MSG_BOOK_DEPTH];
    qty_t   ask_qty[MSG_BOOK_DEPTH];
} msg_book_feed_t;

typedef struct {
    char    symbol[MSG_SYMBOL_LEN + 1];
    price_t ref_price; /* 그 종목의 현재가. 호가창이 펼칠 가격대의 중심 */
} msg_symbol_set_t;

typedef struct {
    char    symbol[MSG_SYMBOL_LEN + 1]; /* 바뀐 뒤의 종목. 실패하면 그대로인 종목 */
    price_t ref_price;
    int32_t code; /* 0이면 바뀌었다. 음수면 errors.h의 에러코드 */
} msg_symbol_ack_t;

typedef struct {
    char    account[MSG_ACCOUNT_LEN + 1];
    int64_t cash; /* 새로 열 때만 입금한다. 이미 있으면 무시 */
} msg_account_open_t;

typedef struct {
    char    account[MSG_ACCOUNT_LEN + 1];
    int32_t code; /* 0이면 쓸 수 있는 계좌다(새로 열었든 이미 있었든) */
    int64_t cash;
    int64_t reserved;
} msg_account_ack_t;

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
int msg_encode_book_req(const msg_book_req_t *m, uint8_t *buf, size_t cap);
int msg_encode_book_ack(const msg_book_ack_t *m, uint8_t *buf, size_t cap);
int msg_encode_detail_req(const msg_detail_req_t *m, uint8_t *buf, size_t cap);
int msg_encode_detail_ack(const msg_detail_ack_t *m, uint8_t *buf, size_t cap);
int msg_encode_balance_req(const msg_balance_req_t *m, uint8_t *buf,
                           size_t cap);
int msg_encode_balance_ack(const msg_balance_ack_t *m, uint8_t *buf,
                           size_t cap);
int msg_encode_symbol_set(const msg_symbol_set_t *m, uint8_t *buf, size_t cap);
int msg_decode_symbol_set(const uint8_t *buf, size_t len, msg_symbol_set_t *out);
int msg_encode_symbol_ack(const msg_symbol_ack_t *m, uint8_t *buf, size_t cap);
int msg_decode_symbol_ack(const uint8_t *buf, size_t len, msg_symbol_ack_t *out);
int msg_encode_account_open(const msg_account_open_t *m, uint8_t *buf, size_t cap);
int msg_decode_account_open(const uint8_t *buf, size_t len,
                            msg_account_open_t *out);
int msg_encode_account_ack(const msg_account_ack_t *m, uint8_t *buf, size_t cap);
int msg_decode_account_ack(const uint8_t *buf, size_t len,
                           msg_account_ack_t *out);

int msg_encode_book_feed(const msg_book_feed_t *m, uint8_t *buf, size_t cap);

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
int msg_decode_book_req(const uint8_t *buf, size_t len, msg_book_req_t *out);
int msg_decode_book_ack(const uint8_t *buf, size_t len, msg_book_ack_t *out);
int msg_decode_detail_req(const uint8_t *buf, size_t len,
                          msg_detail_req_t *out);
int msg_decode_detail_ack(const uint8_t *buf, size_t len,
                          msg_detail_ack_t *out);
int msg_decode_balance_req(const uint8_t *buf, size_t len,
                           msg_balance_req_t *out);
int msg_decode_balance_ack(const uint8_t *buf, size_t len,
                           msg_balance_ack_t *out);
int msg_decode_book_feed(const uint8_t *buf, size_t len, msg_book_feed_t *out);

#endif /* MINI_SOR_MSG_H */
