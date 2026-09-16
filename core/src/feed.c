#include "feed.h"

#include <string.h>

#include "errors.h"

const char *feed_type_str(uint8_t type)
{
    switch (type) {
    case FEED_BOOK:
        return "BOOK";
    case FEED_TRADE:
        return "TRADE";
    default:
        return "?";
    }
}

/* --- 머리 --- */

int feed_encode_hdr(const feed_hdr_t *h, uint8_t *buf, size_t cap)
{
    if (h == NULL || buf == NULL) {
        return ERR_NULL_PTR;
    }
    if (cap < FEED_HEADER_LEN) {
        return ERR_INVALID_ARG;
    }

    wire_put_u16(buf + 0, FEED_MAGIC);
    wire_put_u8(buf + 2, h->version);
    wire_put_u8(buf + 3, h->type);
    wire_put_u64(buf + 4, h->seq);
    wire_put_i64(buf + 12, h->ts);

    return (int)FEED_HEADER_LEN;
}

int feed_decode_hdr(const uint8_t *buf, size_t len, feed_hdr_t *out)
{
    if (buf == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (len < FEED_HEADER_LEN) {
        return ERR_INVALID_ARG;
    }
    if (wire_get_u16(buf + 0) != FEED_MAGIC) {
        /*
         * 남의 채널이다. **되맞추려 들지 않는다**(T3-09의 판단과 같다) —
         * 앞이 어긋났으면 뒤의 경계도 믿을 수 없다.
         */
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->version = wire_get_u8(buf + 2);
    out->type = wire_get_u8(buf + 3);
    out->seq = wire_get_u64(buf + 4);
    out->ts = wire_get_i64(buf + 12);

    /*
     * 버전은 magic 뒤에 본다. **남의 채널과 우리 채널의 다른 판은 다른
     * 일이다** — 앞은 끊을 일이고 뒤는 상대에게 알릴 일이다.
     */
    if (out->version != FEED_VERSION) {
        return ERR_NOT_SUPPORTED;
    }

    return (int)FEED_HEADER_LEN;
}

/* --- 호가 스냅샷 --- */

int feed_encode_book(const feed_book_t *m, uint8_t *buf, size_t cap)
{
    if (m == NULL || buf == NULL) {
        return ERR_NULL_PTR;
    }
    if (m->depth < 1 || m->depth > FEED_DEPTH_MAX) {
        return ERR_INVALID_ARG;
    }
    if (m->market < 0 || m->market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }

    const uint32_t need = FEED_BOOK_BODY_LEN(m->depth);
    if (cap < need) {
        return ERR_INVALID_ARG;
    }

    uint8_t *p = buf;
    wire_put_str(p, FEED_SYMBOL_LEN, m->symbol);
    p += FEED_SYMBOL_LEN;
    wire_put_u8(p++, (uint8_t)m->market);
    wire_put_u8(p++, (uint8_t)m->depth);
    wire_put_u16(p, 0); /* 채움. 0으로 둔다 */
    p += 2;

    /*
     * **매수를 다 쓰고 매도를 쓴다.** 섞어 쓰면(1단 매수, 1단 매도, 2단 ...)
     * 한쪽만 읽고 싶을 때도 전부 훑어야 한다.
     */
    for (int32_t i = 0; i < m->depth; i++) {
        wire_put_i32(p, m->bid[i].price);
        p += 4;
        wire_put_i32(p, m->bid[i].qty);
        p += 4;
    }
    for (int32_t i = 0; i < m->depth; i++) {
        wire_put_i32(p, m->ask[i].price);
        p += 4;
        wire_put_i32(p, m->ask[i].qty);
        p += 4;
    }

    return (int)(p - buf);
}

int feed_decode_book(const uint8_t *buf, size_t len, feed_book_t *out)
{
    if (buf == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (len < FEED_BOOK_HEAD_LEN) {
        return ERR_INVALID_ARG;
    }

    /*
     * **depth를 먼저 읽고 그것으로 기대 길이를 정한다.** 길이가 가변인 유일한
     * 종별이라 여기만 이 모양이다. depth를 믿기 전에 범위를 본다 — 믿고
     * 계산하면 엉뚱한 자리까지 바디로 삼는다(T5-01 J3에서 배운 것).
     */
    int32_t depth = (int32_t)wire_get_u8(buf + FEED_SYMBOL_LEN + 1);
    if (depth < 1 || depth > FEED_DEPTH_MAX) {
        return ERR_INVALID_ARG;
    }
    if (len != FEED_BOOK_BODY_LEN(depth)) {
        return ERR_INVALID_ARG;
    }

    uint8_t market = wire_get_u8(buf + FEED_SYMBOL_LEN);
    if (market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    const uint8_t *p = buf;
    wire_get_str(p, FEED_SYMBOL_LEN, out->symbol);
    p += FEED_SYMBOL_LEN;
    out->market = (market_t)market;
    out->depth = depth;
    p += 4; /* market, depth, rsv */

    for (int32_t i = 0; i < depth; i++) {
        out->bid[i].price = wire_get_i32(p);
        p += 4;
        out->bid[i].qty = wire_get_i32(p);
        p += 4;
    }
    for (int32_t i = 0; i < depth; i++) {
        out->ask[i].price = wire_get_i32(p);
        p += 4;
        out->ask[i].qty = wire_get_i32(p);
        p += 4;
    }

    return (int)(p - buf);
}

/* --- 체결 --- */

int feed_encode_trade(const feed_trade_t *m, uint8_t *buf, size_t cap)
{
    if (m == NULL || buf == NULL) {
        return ERR_NULL_PTR;
    }
    if (m->market < 0 || m->market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }
    if (m->side != SIDE_BUY && m->side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }
    if (cap < FEED_TRADE_BODY_LEN) {
        return ERR_INVALID_ARG;
    }

    uint8_t *p = buf;
    wire_put_str(p, FEED_SYMBOL_LEN, m->symbol);
    p += FEED_SYMBOL_LEN;
    wire_put_u8(p++, (uint8_t)m->market);
    wire_put_u8(p++, (uint8_t)m->side);
    wire_put_i32(p, m->price);
    p += 4;
    wire_put_i32(p, m->qty);
    p += 4;
    wire_put_u64(p, m->exec_id);
    p += 8;

    return (int)(p - buf);
}

int feed_decode_trade(const uint8_t *buf, size_t len, feed_trade_t *out)
{
    if (buf == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (len != FEED_TRADE_BODY_LEN) {
        return ERR_INVALID_ARG;
    }

    uint8_t market = wire_get_u8(buf + FEED_SYMBOL_LEN);
    uint8_t side = wire_get_u8(buf + FEED_SYMBOL_LEN + 1);
    if (market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }
    if (side != SIDE_BUY && side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    const uint8_t *p = buf;
    wire_get_str(p, FEED_SYMBOL_LEN, out->symbol);
    p += FEED_SYMBOL_LEN;
    out->market = (market_t)market;
    out->side = (side_t)side;
    p += 2;
    out->price = wire_get_i32(p);
    p += 4;
    out->qty = wire_get_i32(p);
    p += 4;
    out->exec_id = wire_get_u64(p);
    p += 8;

    return (int)(p - buf);
}

/* --- 빠짐 판정 --- */

void feed_sub_init(feed_sub_t *s)
{
    if (s == NULL) {
        return;
    }
    memset(s, 0, sizeof(*s));
}

feed_seq_t feed_sub_accept(feed_sub_t *s, const feed_hdr_t *h)
{
    if (s == NULL || h == NULL) {
        return FEED_SEQ_DUP; /* 볼 것이 없으면 쓰지 않는다 */
    }

    /*
     * 첫 메시지. **언제 붙었는지는 우리가 고를 수 없다** — "1번부터"라고
     * 우기면 장중에 붙을 수가 없다. 받은 번호를 시작점으로 삼는다.
     */
    if (s->expected == 0) {
        s->expected = h->seq + 1;
        s->stale = false;
        return FEED_SEQ_OK;
    }

    if (h->seq < s->expected) {
        return FEED_SEQ_DUP; /* 이미 본 것이다. expected를 되돌리지 않는다 */
    }

    if (h->seq > s->expected) {
        s->gaps++;
        s->stale = true;
        /*
         * **기대치는 받은 번호 다음으로 옮긴다.** 빠진 것을 기다려도
         * 오지 않는다 — 요청할 상대가 없는 채널이다. 옮기지 않으면 이후
         * 모든 메시지가 갭으로 잡혀 영영 회복하지 못한다.
         */
        s->expected = h->seq + 1;

        /*
         * 갭인데 그것이 스냅샷이면 **이 메시지로 이미 회복된 것이다.**
         * 스냅샷은 자기 완결적이라 앞을 몰라도 믿을 수 있다.
         */
        if (h->type == FEED_BOOK) {
            s->stale = false;
        }
        return FEED_SEQ_GAP;
    }

    s->expected = h->seq + 1;
    if (h->type == FEED_BOOK) {
        s->stale = false; /* 스냅샷을 받았으니 다시 믿는다 */
    }
    return FEED_SEQ_OK;
}

bool feed_sub_usable(const feed_sub_t *s)
{
    return s != NULL && s->expected != 0 && !s->stale;
}
