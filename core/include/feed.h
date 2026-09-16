#ifndef MINI_SOR_FEED_H
#define MINI_SOR_FEED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "types.h"
#include "wire.h"

/*
 * 전략 엔진용 시세 피드.
 *
 * **이 형식은 이 프로젝트의 자체 설계다** (T3-01/T3-02와 같다). 업무상 접한
 * 규격이나 내부 문서를 참고하지 않는다.
 *
 * ===========================================================================
 * 주문 전문과 다른 채널이다
 * ===========================================================================
 *
 * 주문 전문(T3-02)은 **한 상대와 주고받는다.** 요청에는 응답이 있고, 빠진
 * 것은 다시 달라고 할 수 있다(T3-12의 RESEND_REQ).
 *
 * 시세는 다르다. **한 방향이고, 요청이 없고, 여럿이 같은 것을 받는다.**
 * 이 세 가지가 아래 결정을 전부 끌고 간다.
 *
 * ===========================================================================
 * 증분을 두지 않는다 — 매번 스냅샷을 보낸다
 * ===========================================================================
 *
 * 증분(델타)은 분명히 작다. "3번 호가 잔량이 100 줄었다"는 몇 바이트면 된다.
 * 그런데 증분은 **앞의 것을 모두 받았다는 전제 위에서만 뜻이 있다.**
 *
 * 하나를 놓치면 그 뒤가 전부 어긋난다. 그리고 어긋난 것을 **알 방법이
 * 없다** — 잔량이 100이어야 하는데 200으로 보이는 것은 틀린 값이지 오류가
 * 아니라서, 전략은 그 호가창을 믿고 주문을 낸다. 장이 끝난 뒤 체결이 안
 * 맞을 때에야 드러난다.
 *
 * 스냅샷은 **매번 자기 완결적이다.** 놓쳐도 다음 것을 받으면 다시 맞는다.
 * 대가는 크기인데, 10단이면 한 건이 200바이트대이고 그 정도면 이 프로젝트가
 * 다루는 규모에서 문제가 되지 않는다. **재기 전에 아끼지 않는다**(CLAUDE.md).
 *
 * ponytail: 종목과 단 수가 늘어 대역이 실제로 재이면 그때 증분을 더한다.
 * 그때도 "증분 + 주기적 전체 스냅샷"이지 증분만으로 가지 않는다.
 *
 * ===========================================================================
 * 빠졌을 때 — 재전송을 요청할 상대가 없다
 * ===========================================================================
 *
 * T3-12는 갭을 보면 RESEND_REQ를 보냈다. **여기서는 그 답을 쓸 수 없다.**
 * 방송이라 보내는 쪽이 나 하나를 위해 되돌아가 주지 않는다. 되돌아가 준다면
 * 다른 모든 구독자도 그 사이 멈춰야 한다.
 *
 * 그래서 이 피드의 답은 **"다음 스냅샷까지 믿지 않는다"** 이다.
 *
 *  - 갭을 보면 그 사실을 알린다. 조용히 넘어가지 않는다
 *  - 전략은 **새 주문을 내지 않는다.** 이미 낸 주문의 취소는 할 수 있다 —
 *    취소는 틀린 호가를 근거로 하지 않기 때문이다
 *  - 다음 스냅샷을 받으면 그것으로 갈아 끼우고 다시 쓴다
 *
 * 증분을 안 둔 덕에 이 회복이 **저절로** 된다. 증분이었다면 갭 이후의 모든
 * 증분이 쓸모없어져서 전체 재동기를 따로 요청해야 했다.
 *
 * ===========================================================================
 * 시각은 논리 시각이다
 * ===========================================================================
 *
 * 피드를 만드는 쪽도 읽는 쪽도 시스템 시각을 읽지 않는다. 읽으면 같은 입력이
 * 같은 피드를 만들지 않고, 그러면 전략 비교라는 이 프로젝트의 전제가
 * 무너진다(CLAUDE.md의 결정성).
 *
 * ===========================================================================
 * 바이트 배치 (전부 빅엔디언, 채움 없음)
 * ===========================================================================
 *
 * 머리 (20)  magic:u16 version:u8 type:u8 seq:u64 ts:i64
 *
 * BOOK      symbol[8] market:u8 depth:u8 rsv:u16
 *           그 뒤에 depth개의 매수 줄, 이어서 depth개의 매도 줄.
 *           줄 하나 = price:i32 qty:i32 (8바이트)
 *           **없는 단은 price=0, qty=0으로 채운다.** 줄 수를 세어 읽지 않고
 *           depth로 읽게 하려면 자리가 고정되어야 한다
 *
 * TRADE (26) symbol[8] market:u8 side:u8 price:i32 qty:i32 exec_id:u64
 *           side는 **taker의 방향**이다. 누가 먼저 움직였는지가 정보다
 */

/* "MF" — 주문 전문의 "MS"와 다르게 둔다. 채널을 섞으면 바로 드러나야 한다. */
#define FEED_MAGIC 0x4D46u
#define FEED_VERSION 1u

#define FEED_HEADER_LEN 20u

/* 종목코드 길이. 주문 전문과 같다. */
#define FEED_SYMBOL_LEN 8

/* 호가 단 수 상한. 10단이면 국내 시장의 관행적인 깊이를 덮는다. */
#define FEED_DEPTH_MAX 10

/*
 * 바디 길이. **계산식으로 적어 필드를 더할 때 같이 움직이게 한다**(msg.h와
 * 같은 이유). 처음에 TRADE를 손으로 30이라 적었다가 실제 배치(26)와 어긋나
 * 테스트가 터졌다 — 사람이 더한 수는 사람이 틀린다.
 */

/* BOOK: symbol[8] market:u8 depth:u8 rsv:u16 = 12, 그 뒤에 줄들 */
#define FEED_BOOK_LINE_LEN (4u + 4u)
#define FEED_BOOK_HEAD_LEN ((uint32_t)FEED_SYMBOL_LEN + 1u + 1u + 2u)
#define FEED_BOOK_BODY_LEN(d) \
    (FEED_BOOK_HEAD_LEN + (uint32_t)(d) * 2u * FEED_BOOK_LINE_LEN)
#define FEED_BOOK_BODY_MAX FEED_BOOK_BODY_LEN(FEED_DEPTH_MAX)

/* TRADE: symbol[8] market:u8 side:u8 price:i32 qty:i32 exec_id:u64 */
#define FEED_TRADE_BODY_LEN \
    ((uint32_t)FEED_SYMBOL_LEN + 1u + 1u + 4u + 4u + 8u)

/* 한 메시지의 최대 길이. 버퍼를 잡을 때 쓴다. */
#define FEED_MSG_MAX (FEED_HEADER_LEN + FEED_BOOK_BODY_MAX)

typedef enum { FEED_BOOK = 1, FEED_TRADE = 2 } feed_type_t;

/* 사람이 읽을 이름. 모르는 값이면 "?". */
const char *feed_type_str(uint8_t type);

/* --- 호가 스냅샷 --- */

/* 한 단. 없는 단은 둘 다 0이다. */
typedef struct {
    price_t price;
    qty_t   qty;
} feed_level_t;

typedef struct {
    char     symbol[FEED_SYMBOL_LEN + 1];
    market_t market;
    int32_t  depth; /* 1..FEED_DEPTH_MAX */

    feed_level_t bid[FEED_DEPTH_MAX];
    feed_level_t ask[FEED_DEPTH_MAX];
} feed_book_t;

/* --- 체결 --- */

typedef struct {
    char     symbol[FEED_SYMBOL_LEN + 1];
    market_t market;
    side_t   side; /* taker의 방향 */
    price_t  price;
    qty_t    qty;
    uint64_t exec_id;
} feed_trade_t;

/* --- 머리 --- */

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint64_t seq;
    ts_t     ts; /* 논리 시각 */
} feed_hdr_t;

/*
 * 머리를 쓴다. 성공하면 FEED_HEADER_LEN, 자리가 모자라면 ERR_INVALID_ARG.
 */
int feed_encode_hdr(const feed_hdr_t *h, uint8_t *buf, size_t cap);

/*
 * 머리를 읽는다. 성공하면 FEED_HEADER_LEN.
 *
 * magic이 다르면 ERR_INVALID_ARG — **되맞추려 들지 않는다**(T3-09와 같은
 * 판단). 버전이 다르면 ERR_NOT_SUPPORTED. 둘을 구분하는 이유는, 앞은 남의
 * 채널이고 뒤는 우리 채널의 다른 판이라 대응이 다르기 때문이다.
 */
int feed_decode_hdr(const uint8_t *buf, size_t len, feed_hdr_t *out);

/* --- 바디 --- */

/*
 * 인코딩 — **바디만 쓴다.** 머리는 호출부가 따로 쓴다(T3-02와 같은 이유:
 * 시퀀스와 논리 시각은 보내는 쪽의 상태이지 시세의 내용이 아니다).
 *
 * 성공하면 쓴 바이트 수, 자리가 모자라거나 인자가 잘못되면 ERR_INVALID_ARG.
 */
int feed_encode_book(const feed_book_t *m, uint8_t *buf, size_t cap);
int feed_encode_trade(const feed_trade_t *m, uint8_t *buf, size_t cap);

/*
 * 디코딩 — 바디 길이가 규격과 **정확히 같아야** 한다. 짧으면 필드가 모자라고,
 * 길면 규격이 다른 상대다. 둘 다 ERR_INVALID_ARG.
 *
 * BOOK은 길이가 depth에 따라 달라지므로, 머리 12바이트의 depth를 먼저 읽고
 * 그것으로 기대 길이를 정한 뒤 맞춰 본다.
 */
int feed_decode_book(const uint8_t *buf, size_t len, feed_book_t *out);
int feed_decode_trade(const uint8_t *buf, size_t len, feed_trade_t *out);

/* --- 빠짐 판정 --- */

typedef enum {
    FEED_SEQ_OK = 0, /* 기다리던 번호다 */
    FEED_SEQ_DUP,    /* 이미 본 번호다. 버린다 */
    FEED_SEQ_GAP     /* 사이가 비었다. **다음 스냅샷까지 믿지 않는다** */
} feed_seq_t;

/*
 * 구독자가 들고 다니는 상태.
 *
 * `stale`은 **갭을 본 뒤 다음 스냅샷을 받기 전까지** 선다. 이것이 서 있는
 * 동안 전략은 새 주문을 내지 않는다 — 틀린 호가를 근거로 삼는 것이기 때문이다.
 * (이미 낸 주문의 취소는 해도 된다. 취소는 호가를 근거로 하지 않는다.)
 */
typedef struct {
    uint64_t expected; /* 다음에 올 번호. 0이면 아직 아무것도 못 받았다 */
    bool     stale;
    uint64_t gaps; /* 지금까지 본 갭의 수. 숨기지 않는다 */
} feed_sub_t;

/* 구독 상태를 비운다. */
void feed_sub_init(feed_sub_t *s);

/*
 * 받은 번호를 판정하고 상태를 갱신한다.
 *
 * 첫 메시지는 번호가 무엇이든 FEED_SEQ_OK다 — 언제 붙었는지는 우리가 고를 수
 * 없고, "1번부터 받아야 한다"고 우기면 장중에 붙을 수가 없다.
 *
 * 갭이면 `stale`이 선다. 그 뒤 **스냅샷(FEED_BOOK)을 받으면 내려간다** —
 * 스냅샷은 자기 완결적이라 앞을 몰라도 믿을 수 있다. 체결(FEED_TRADE)로는
 * 내려가지 않는다. 체결 하나는 호가창을 복원해 주지 못한다.
 */
feed_seq_t feed_sub_accept(feed_sub_t *s, const feed_hdr_t *h);

/* 지금 이 구독의 시세를 믿어도 되는가. */
bool feed_sub_usable(const feed_sub_t *s);

#endif /* MINI_SOR_FEED_H */
