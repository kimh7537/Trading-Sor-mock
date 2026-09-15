/*
 * T3-01 전문 공통 헤더와 직렬화 유틸.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 헤더 배치가 문서와 일치한다 — **바이트 위치를 직접 대조한다**
 *  2. 구조체를 그대로 보내지 않는다 — 패딩·엔디언에 기대지 않는다
 *  3. 인코딩 -> 디코딩 왕복이 모든 필드 값을 보존한다 (경계값 포함)
 *  4. 잘린 버퍼 / 한도를 넘는 바디 / 다른 형식을 거절한다
 *
 * 1번이 중요하다. 왕복만 검사하면 인코딩과 디코딩이 **같이 틀려도** 통과한다.
 * 두 프로세스가 서로 다른 빌드일 수 있는 곳에서 그건 검사가 아니다.
 * 그래서 기대 바이트열을 손으로 적어 놓고 대조한다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"
#include "wire.h"

/* --- 1. 정수 직렬화가 빅엔디언인가 --- */

static void test_big_endian_layout(void)
{
    uint8_t b[8];

    wire_put_u16(b, 0x0102);
    assert(b[0] == 0x01 && b[1] == 0x02);

    wire_put_u32(b, 0x01020304u);
    assert(b[0] == 0x01 && b[1] == 0x02 && b[2] == 0x03 && b[3] == 0x04);

    wire_put_u64(b, 0x0102030405060708ULL);
    for (int i = 0; i < 8; i++) {
        assert(b[i] == (uint8_t)(i + 1));
    }

    /* 100은 덤프에서 00 00 00 64로 읽힌다 — 이것이 빅엔디언을 고른 이유다. */
    wire_put_u32(b, 100);
    assert(b[0] == 0 && b[1] == 0 && b[2] == 0 && b[3] == 0x64);
}

static void test_integer_roundtrip(void)
{
    uint8_t b[8];

    static const uint64_t U[] = {0,
                                 1,
                                 127,
                                 128,
                                 255,
                                 256,
                                 65535,
                                 65536,
                                 0x7FFFFFFFu,
                                 0x80000000u,
                                 0xFFFFFFFFu,
                                 0x7FFFFFFFFFFFFFFFULL,
                                 0x8000000000000000ULL,
                                 0xFFFFFFFFFFFFFFFFULL};

    for (size_t i = 0; i < sizeof(U) / sizeof(U[0]); i++) {
        wire_put_u64(b, U[i]);
        assert(wire_get_u64(b) == U[i]);

        wire_put_u32(b, (uint32_t)U[i]);
        assert(wire_get_u32(b) == (uint32_t)U[i]);

        wire_put_u16(b, (uint16_t)U[i]);
        assert(wire_get_u16(b) == (uint16_t)U[i]);

        wire_put_u8(b, (uint8_t)U[i]);
        assert(wire_get_u8(b) == (uint8_t)U[i]);
    }

    /* 부호 있는 값 — 음수와 경계가 핵심이다. */
    static const int64_t S[] = {0,         1,         -1,        127,
                                -128,      32767,     -32768,    INT32_MAX,
                                INT32_MIN, INT64_MAX, INT64_MIN, -1000000};

    for (size_t i = 0; i < sizeof(S) / sizeof(S[0]); i++) {
        wire_put_i64(b, S[i]);
        assert(wire_get_i64(b) == S[i]);
    }

    static const int32_t S32[] = {0, 1, -1, INT32_MAX, INT32_MIN, -12345};
    for (size_t i = 0; i < sizeof(S32) / sizeof(S32[0]); i++) {
        wire_put_i32(b, S32[i]);
        assert(wire_get_i32(b) == S32[i]);
    }

    /* -1은 전부 0xFF다. 2의 보수 비트열을 그대로 싣는다는 뜻. */
    wire_put_i32(b, -1);
    assert(b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF && b[3] == 0xFF);
}

/* --- 2. 문자열 필드 --- */

static void test_str_field(void)
{
    uint8_t b[8];
    char    out[9];

    wire_put_str(b, sizeof(b), "abc");
    assert(memcmp(b, "abc\0\0\0\0\0", 8) == 0); /* 남는 자리는 0 */
    wire_get_str(b, sizeof(b), out);
    assert(strcmp(out, "abc") == 0);

    /* 딱 맞는 길이 — 널 종료 자리가 없다. 길이가 규격이다. */
    wire_put_str(b, sizeof(b), "12345678");
    assert(memcmp(b, "12345678", 8) == 0);
    wire_get_str(b, sizeof(b), out);
    assert(strcmp(out, "12345678") == 0);

    /* 넘치면 자른다. 넘쳐 쓰지 않는다. */
    uint8_t guard[16];
    memset(guard, 0xAA, sizeof(guard));
    wire_put_str(guard, 8, "123456789012");
    assert(memcmp(guard, "12345678", 8) == 0);
    for (int i = 8; i < 16; i++) {
        assert(guard[i] == 0xAA); /* 필드 밖은 건드리지 않는다 */
    }

    /* 빈 문자열과 NULL. */
    wire_put_str(b, sizeof(b), "");
    wire_get_str(b, sizeof(b), out);
    assert(out[0] == '\0');

    wire_put_str(b, sizeof(b), NULL);
    wire_get_str(b, sizeof(b), out);
    assert(out[0] == '\0');

    /* 인자가 없어도 죽지 않는다. */
    wire_put_str(NULL, 8, "x");
    wire_get_str(NULL, 8, out);
    assert(out[0] == '\0');
    wire_get_str(b, sizeof(b), NULL);
}

/* --- 3. 헤더 배치 --- */

/*
 * 완료 조건 1. 기대 바이트열을 손으로 적고 대조한다.
 *
 * type=7, body_len=0x00000100(256), seq=0x0102030405060708,
 * ts=0x000000A1B2C3D4E5
 */
static void test_header_layout_is_exact(void)
{
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = 7;
    h.body_len = 256;
    h.seq = 0x0102030405060708ULL;
    h.ts = 0x000000A1B2C3D4E5LL;

    uint8_t buf[WIRE_HEADER_LEN];
    assert(wire_encode_header(&h, buf, sizeof(buf)) == (int)WIRE_HEADER_LEN);

    static const uint8_t WANT[WIRE_HEADER_LEN] = {
        0x4D, 0x53,             /* magic "MS" */
        0x01,                   /* version */
        0x07,                   /* type */
        0x00, 0x00, 0x01, 0x00, /* body_len = 256 */
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, /* seq */
        0x00, 0x00, 0x00, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, /* ts */
    };
    assert(memcmp(buf, WANT, WIRE_HEADER_LEN) == 0);

    /* 필드 크기 합이 곧 헤더 길이다. */
    assert(WIRE_HEADER_LEN == 2 + 1 + 1 + 4 + 8 + 8);
}

/*
 * 완료 조건 2 — **구조체 크기에 기대지 않는다.**
 *
 * `wire_header_t`는 필드를 담는 그릇일 뿐이고, 전송되는 것은 언제나 24바이트다.
 * 구조체가 커지거나 작아져도 전문은 그대로여야 한다.
 */
static void test_header_len_is_independent_of_struct(void)
{
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = 1;

    static uint8_t buf[WIRE_FRAME_MAX];
    memset(buf, 0xAA, sizeof(buf));
    assert(wire_encode_header(&h, buf, sizeof(buf)) == (int)WIRE_HEADER_LEN);

    /* 24바이트 뒤로는 한 바이트도 쓰지 않는다. */
    for (size_t i = WIRE_HEADER_LEN; i < sizeof(buf); i++) {
        assert(buf[i] == 0xAA);
    }
}

/* --- 4. 왕복 --- */

static void test_header_roundtrip(void)
{
    static const uint8_t  TYPES[] = {0, 1, 127, 128, 255};
    static const uint32_t LENS[] = {0, 1, 24, 1024, WIRE_BODY_MAX};
    static const uint64_t SEQS[] = {0, 1, 0xFFFFFFFFULL,
                                    0xFFFFFFFFFFFFFFFFULL};
    static const int64_t  TSS[] = {0, 1, -1, INT64_MAX, INT64_MIN};

    for (size_t a = 0; a < sizeof(TYPES) / sizeof(TYPES[0]); a++) {
        for (size_t b = 0; b < sizeof(LENS) / sizeof(LENS[0]); b++) {
            for (size_t c = 0; c < sizeof(SEQS) / sizeof(SEQS[0]); c++) {
                for (size_t d = 0; d < sizeof(TSS) / sizeof(TSS[0]); d++) {
                    wire_header_t in;
                    memset(&in, 0, sizeof(in));
                    in.type = TYPES[a];
                    in.body_len = LENS[b];
                    in.seq = SEQS[c];
                    in.ts = TSS[d];

                    uint8_t buf[WIRE_HEADER_LEN];
                    assert(wire_encode_header(&in, buf, sizeof(buf)) ==
                           (int)WIRE_HEADER_LEN);

                    wire_header_t out;
                    assert(wire_decode_header(buf, sizeof(buf), &out) ==
                           (int)WIRE_HEADER_LEN);

                    assert(out.version == WIRE_VERSION);
                    assert(out.type == in.type);
                    assert(out.body_len == in.body_len);
                    assert(out.seq == in.seq);
                    assert(out.ts == in.ts);

                    /*
                     * 디코딩 결과를 바이트로도 비교한다. 패딩을 밀지 않으면
                     * 같은 값에서 다른 바이트가 나온다.
                     */
                    wire_header_t again;
                    assert(wire_decode_header(buf, sizeof(buf), &again) ==
                           (int)WIRE_HEADER_LEN);
                    assert(memcmp(&out, &again, sizeof(out)) == 0);
                }
            }
        }
    }
}

/* --- 5. 거절 --- */

static void test_encode_rejects(void)
{
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = 1;

    uint8_t buf[WIRE_HEADER_LEN];

    assert(wire_encode_header(NULL, buf, sizeof(buf)) == ERR_NULL_PTR);
    assert(wire_encode_header(&h, NULL, sizeof(buf)) == ERR_NULL_PTR);

    /* 자리가 모자라면 쓰지 않는다. 1바이트 모자란 경우까지 본다. */
    for (size_t cap = 0; cap < WIRE_HEADER_LEN; cap++) {
        uint8_t probe[WIRE_HEADER_LEN];
        memset(probe, 0x5A, sizeof(probe));
        assert(wire_encode_header(&h, probe, cap) == ERR_INVALID_ARG);
        for (size_t i = 0; i < sizeof(probe); i++) {
            assert(probe[i] == 0x5A); /* 한 바이트도 쓰지 않았다 */
        }
    }

    /* 한도를 넘는 바디 길이. */
    h.body_len = WIRE_BODY_MAX + 1;
    assert(wire_encode_header(&h, buf, sizeof(buf)) == ERR_INVALID_ARG);
    h.body_len = WIRE_BODY_MAX;
    assert(wire_encode_header(&h, buf, sizeof(buf)) == (int)WIRE_HEADER_LEN);
}

static void test_decode_rejects(void)
{
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = 3;
    h.body_len = 8;

    uint8_t buf[WIRE_HEADER_LEN];
    assert(wire_encode_header(&h, buf, sizeof(buf)) == (int)WIRE_HEADER_LEN);

    wire_header_t out;
    assert(wire_decode_header(NULL, sizeof(buf), &out) == ERR_NULL_PTR);
    assert(wire_decode_header(buf, sizeof(buf), NULL) == ERR_NULL_PTR);

    /* 잘린 버퍼 — 1바이트 모자란 것까지 전부 거절한다. */
    for (size_t len = 0; len < WIRE_HEADER_LEN; len++) {
        assert(wire_decode_header(buf, len, &out) == ERR_INVALID_ARG);
    }
    assert(wire_decode_header(buf, WIRE_HEADER_LEN, &out) ==
           (int)WIRE_HEADER_LEN);

    /* magic이 다르면 동기가 깨진 것이다. */
    uint8_t bad[WIRE_HEADER_LEN];
    memcpy(bad, buf, sizeof(bad));
    bad[0] ^= 0xFF;
    assert(wire_decode_header(bad, sizeof(bad), &out) == ERR_INVALID_ARG);

    memcpy(bad, buf, sizeof(bad));
    bad[1] ^= 0xFF;
    assert(wire_decode_header(bad, sizeof(bad), &out) == ERR_INVALID_ARG);

    /*
     * 판이 다르면 해석하지 않는다. 거절 코드도 magic 오류와 구분한다 —
     * "쓰레기가 왔다"와 "상대가 다른 판이다"는 대응이 다르다.
     */
    memcpy(bad, buf, sizeof(bad));
    bad[2] = WIRE_VERSION + 1;
    assert(wire_decode_header(bad, sizeof(bad), &out) == ERR_NOT_SUPPORTED);
    bad[2] = 0;
    assert(wire_decode_header(bad, sizeof(bad), &out) == ERR_NOT_SUPPORTED);

    /* 한도를 넘는 바디 길이 — 4GB를 할당하게 만드는 전문을 막는다. */
    memcpy(bad, buf, sizeof(bad));
    wire_put_u32(bad + 4, WIRE_BODY_MAX + 1);
    assert(wire_decode_header(bad, sizeof(bad), &out) == ERR_INVALID_ARG);

    wire_put_u32(bad + 4, 0xFFFFFFFFu);
    assert(wire_decode_header(bad, sizeof(bad), &out) == ERR_INVALID_ARG);

    wire_put_u32(bad + 4, WIRE_BODY_MAX);
    assert(wire_decode_header(bad, sizeof(bad), &out) == (int)WIRE_HEADER_LEN);
}

int main(void)
{
    test_big_endian_layout();
    test_integer_roundtrip();
    test_str_field();
    test_header_layout_is_exact();
    test_header_len_is_independent_of_struct();
    test_header_roundtrip();
    test_encode_rejects();
    test_decode_rejects();
    return 0;
}
