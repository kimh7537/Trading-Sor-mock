#include "wire.h"

#include <stddef.h>
#include <string.h>

#include "errors.h"

/* --- 정수 --- */

void wire_put_u8(uint8_t *p, uint8_t v)
{
    p[0] = v;
}

void wire_put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

void wire_put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void wire_put_u64(uint8_t *p, uint64_t v)
{
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)v;
}

/*
 * 부호 있는 값은 2의 보수 비트열을 그대로 싣는다.
 *
 * 부호 있는 정수를 부호 없는 정수로 바꾸는 것은 C가 모듈러 연산으로 정의한다 —
 * 구현에 달려 있지 않다. 반대 방향(꺼낼 때)이 문제인데, 범위를 벗어나는 값의
 * 변환은 구현 정의라 wire_get_i*()에서 따로 다룬다.
 */
void wire_put_i32(uint8_t *p, int32_t v)
{
    wire_put_u32(p, (uint32_t)v);
}

void wire_put_i64(uint8_t *p, int64_t v)
{
    wire_put_u64(p, (uint64_t)v);
}

uint8_t wire_get_u8(const uint8_t *p)
{
    return p[0];
}

uint16_t wire_get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

uint32_t wire_get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

uint64_t wire_get_u64(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

/*
 * 2의 보수 비트열을 부호 있는 값으로 되돌린다.
 *
 * `(int32_t)u`로 바로 캐스팅하면 u가 INT32_MAX를 넘을 때 결과가 구현 정의다.
 * 대부분의 컴파일러가 우리가 기대하는 대로 하지만, "대부분"에 기대는 것이
 * 이 파일이 없애려는 종류의 의존이다. 아래는 정의된 연산만 쓴다.
 */
int32_t wire_get_i32(const uint8_t *p)
{
    uint32_t u = wire_get_u32(p);

    if (u <= (uint32_t)INT32_MAX) {
        return (int32_t)u;
    }
    return (int32_t)(u - (uint32_t)INT32_MIN) + INT32_MIN;
}

int64_t wire_get_i64(const uint8_t *p)
{
    uint64_t u = wire_get_u64(p);

    if (u <= (uint64_t)INT64_MAX) {
        return (int64_t)u;
    }
    return (int64_t)(u - (uint64_t)INT64_MIN) + INT64_MIN;
}

/* --- 문자열 --- */

void wire_put_str(uint8_t *p, size_t field_len, const char *s)
{
    if (p == NULL || field_len == 0) {
        return;
    }

    memset(p, 0, field_len);
    if (s == NULL) {
        return;
    }

    /* 넘치면 자른다. 널 종료를 보장하지 않는다 — 길이가 규격이다. */
    size_t n = strlen(s);
    if (n > field_len) {
        n = field_len;
    }
    memcpy(p, s, n);
}

void wire_get_str(const uint8_t *p, size_t field_len, char *out)
{
    if (out == NULL) {
        return;
    }
    if (p == NULL || field_len == 0) {
        out[0] = '\0';
        return;
    }

    size_t n = 0;
    while (n < field_len && p[n] != 0) {
        n++;
    }
    memcpy(out, p, n);
    out[n] = '\0';
}

/* --- 헤더 --- */

int wire_encode_header(const wire_header_t *h, uint8_t *buf, size_t cap)
{
    if (h == NULL || buf == NULL) {
        return ERR_NULL_PTR;
    }
    if (cap < WIRE_HEADER_LEN) {
        return ERR_INVALID_ARG;
    }
    if (h->body_len > WIRE_BODY_MAX) {
        return ERR_INVALID_ARG;
    }

    wire_put_u16(buf + 0, WIRE_MAGIC);
    wire_put_u8(buf + 2, WIRE_VERSION);
    wire_put_u8(buf + 3, h->type);
    wire_put_u32(buf + 4, h->body_len);
    wire_put_u64(buf + 8, h->seq);
    wire_put_i64(buf + 16, h->ts);

    return (int)WIRE_HEADER_LEN;
}

int wire_decode_header(const uint8_t *buf, size_t len, wire_header_t *out)
{
    if (buf == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (len < WIRE_HEADER_LEN) {
        return ERR_INVALID_ARG;
    }
    if (wire_get_u16(buf + 0) != WIRE_MAGIC) {
        return ERR_INVALID_ARG; /* 스트림 동기가 깨졌다 */
    }

    uint8_t version = wire_get_u8(buf + 2);
    if (version != WIRE_VERSION) {
        /*
         * 판이 다르면 **해석하지 않는다.** 필드가 옮겨졌을 수 있고, 그대로 읽으면
         * 엉뚱한 값을 그럴듯하게 돌려준다. 조용한 오해석이 거절보다 나쁘다.
         */
        return ERR_NOT_SUPPORTED;
    }

    uint32_t body_len = wire_get_u32(buf + 4);
    if (body_len > WIRE_BODY_MAX) {
        return ERR_INVALID_ARG;
    }

    /* 패딩까지 밀어 둔다. 기록을 바이트로 비교하는 습관을 여기서도 지킨다. */
    memset(out, 0, sizeof(*out));
    out->version = version;
    out->type = wire_get_u8(buf + 3);
    out->body_len = body_len;
    out->seq = wire_get_u64(buf + 8);
    out->ts = wire_get_i64(buf + 16);

    return (int)WIRE_HEADER_LEN;
}
