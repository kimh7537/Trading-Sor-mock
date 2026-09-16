/*
 * T3-09 FEP 공통 — 전문 조립.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 헤더 24바이트가 여러 조각으로 나뉘어 와도 조립한다
 *  2. 바디가 나뉘어 와도 완성될 때까지 꺼내지 않는다
 *  3. 한 번에 여러 개가 와도 각각 꺼낸다
 *  4. **1바이트씩 흘려 넣어도 결과가 같다** (경계 전수)
 *  5. `body_len` 한도 초과를 바디를 기다리기 전에 거절한다
 *  6. 어긋난 magic·version은 재동기하지 않고 접속을 버린다
 *
 * 4번이 이 태스크의 핵심이다. 조립 버그는 "대개는 잘 되는데" 특정 경계에서만
 * 틀리므로, 있을 수 있는 모든 쪼개짐을 한 번에 훑는 4번이 가장 많이 잡는다.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "framer.h"
#include "wire.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

/* --- 도구 --- */

/*
 * 전문 하나를 만든다. 바디는 tag + i로 채워 **어느 전문의 바디인지** 알아볼 수
 * 있게 한다 — 두 개가 붙어 올 때 순서가 섞이면 여기서 걸린다.
 */
static size_t build(uint8_t *out, uint8_t type, uint64_t seq, uint32_t body_len,
                    uint8_t tag)
{
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = type;
    h.body_len = body_len;
    h.seq = seq;
    h.ts = (int64_t)seq * 1000;

    int n = wire_encode_header(&h, out, WIRE_HEADER_LEN);
    assert(n == (int)WIRE_HEADER_LEN);

    for (uint32_t i = 0; i < body_len; i++) {
        out[WIRE_HEADER_LEN + i] = (uint8_t)(tag + i);
    }
    return WIRE_HEADER_LEN + body_len;
}

static void check_body(const uint8_t *body, uint32_t body_len, uint8_t tag)
{
    for (uint32_t i = 0; i < body_len; i++) {
        assert(body[i] == (uint8_t)(tag + i));
    }
}

/* --- 1. 헤더가 쪼개져 온다 --- */

static void test_split_header(void)
{
    uint8_t frame[WIRE_HEADER_LEN + 8];
    size_t  n = build(frame, 3, 77, 8, 0x10);

    framer_t fr;
    framer_init(&fr);

    wire_header_t  h;
    const uint8_t *body = NULL;

    /* 헤더 24바이트를 10 + 7 + 7로 쪼갠다. */
    const size_t cuts[3] = {10, 7, 7};
    size_t       at = 0;
    for (size_t i = 0; i < 3; i++) {
        assert(framer_push(&fr, frame + at, cuts[i]) == ERR_OK);
        at += cuts[i];
        /* 헤더가 다 와도 바디가 없으므로 아직 꺼낼 게 없다. */
        assert(framer_next(&fr, &h, &body) == 0);
    }
    assert(at == WIRE_HEADER_LEN);

    assert(framer_push(&fr, frame + at, n - at) == ERR_OK);
    assert(framer_next(&fr, &h, &body) == 1);
    assert(h.type == 3);
    assert(h.seq == 77);
    assert(h.body_len == 8);
    check_body(body, 8, 0x10);

    assert(framer_next(&fr, &h, &body) == 0);
    assert(framer_pending(&fr) == 0);
}

/* --- 2. 바디가 쪼개져 온다 --- */

static void test_split_body(void)
{
    uint8_t frame[WIRE_HEADER_LEN + 40];
    size_t  n = build(frame, 5, 9, 40, 0x20);

    framer_t fr;
    framer_init(&fr);

    wire_header_t  h;
    const uint8_t *body = NULL;

    /* 한 바이트가 모자라면 꺼내지 않는다. */
    assert(framer_push(&fr, frame, n - 1) == ERR_OK);
    assert(framer_next(&fr, &h, &body) == 0);

    assert(framer_push(&fr, frame + n - 1, 1) == ERR_OK);
    assert(framer_next(&fr, &h, &body) == 1);
    assert(h.body_len == 40);
    check_body(body, 40, 0x20);
}

/* --- 3. 한 번에 여러 개 --- */

static void test_many_at_once(void)
{
    uint8_t stream[512];
    size_t  n = 0;
    n += build(stream + n, 1, 100, 4, 0x30);
    n += build(stream + n, 2, 101, 0, 0x00); /* 바디 없는 전문 */
    n += build(stream + n, 3, 102, 16, 0x50);

    framer_t fr;
    framer_init(&fr);
    assert(framer_push(&fr, stream, n) == ERR_OK);

    wire_header_t  h;
    const uint8_t *body = NULL;

    assert(framer_next(&fr, &h, &body) == 1);
    assert(h.seq == 100 && h.body_len == 4);
    check_body(body, 4, 0x30);

    assert(framer_next(&fr, &h, &body) == 1);
    assert(h.seq == 101 && h.body_len == 0);

    assert(framer_next(&fr, &h, &body) == 1);
    assert(h.seq == 102 && h.body_len == 16);
    check_body(body, 16, 0x50);

    assert(framer_next(&fr, &h, &body) == 0);
    assert(framer_pending(&fr) == 0);
}

/* --- 4. 1바이트씩 흘려 넣기 (경계 전수) --- */

static void test_byte_at_a_time(void)
{
    uint8_t stream[512];
    size_t  ends[3];
    size_t  n = 0;
    n += build(stream + n, 1, 200, 12, 0x60);
    ends[0] = n;
    n += build(stream + n, 2, 201, 0, 0x00);
    ends[1] = n;
    n += build(stream + n, 3, 202, 33, 0x70);
    ends[2] = n;

    framer_t fr;
    framer_init(&fr);

    wire_header_t  h;
    const uint8_t *body = NULL;

    int got = 0;
    for (size_t i = 0; i < n; i++) {
        assert(framer_push(&fr, stream + i, 1) == ERR_OK);

        int rc = framer_next(&fr, &h, &body);
        if (rc == 1) {
            /*
             * **전문은 마지막 바이트가 들어온 바로 그때 나와야 한다.**
             * 한 바이트라도 이르면 덜 받은 것을 꺼낸 것이고, 늦으면 조립이
             * 한 박자씩 밀린 것이다. 둘 다 여기서 걸린다.
             */
            assert(i + 1 == ends[got]);
            assert(h.seq == (uint64_t)(200 + got));
            got++;
        } else {
            assert(rc == 0);
        }
    }
    assert(got == 3);
    assert(framer_pending(&fr) == 0);
}

/* --- 5. 한도 초과 --- */

static void test_body_too_large(void)
{
    /*
     * `wire_encode_header`가 한도를 넘는 body_len을 거절하므로 손으로 만든다.
     * 받는 쪽은 상대가 규격대로 보낸다고 믿을 수 없다 — 그것이 이 검사의 이유다.
     */
    uint8_t hdr[WIRE_HEADER_LEN];
    memset(hdr, 0, sizeof(hdr));
    wire_put_u16(hdr + 0, WIRE_MAGIC);
    wire_put_u8(hdr + 2, WIRE_VERSION);
    wire_put_u8(hdr + 3, 1);
    wire_put_u32(hdr + 4, WIRE_BODY_MAX + 1);
    wire_put_u64(hdr + 8, 1);
    wire_put_i64(hdr + 16, 0);

    framer_t fr;
    framer_init(&fr);
    assert(framer_push(&fr, hdr, sizeof(hdr)) == ERR_OK);

    wire_header_t  h;
    const uint8_t *body = NULL;

    /* 바디를 기다리지 않고 헤더만 보고 거절한다. */
    assert(framer_next(&fr, &h, &body) < 0);
    assert(framer_broken(&fr));
}

/* --- 6. 어긋난 magic·version --- */

static void test_bad_magic(void)
{
    uint8_t frame[WIRE_HEADER_LEN + 4];
    build(frame, 1, 1, 4, 0x80);
    frame[0] = 'X'; /* MS가 아니다 */

    framer_t fr;
    framer_init(&fr);
    assert(framer_push(&fr, frame, sizeof(frame)) == ERR_OK);

    wire_header_t  h;
    const uint8_t *body = NULL;

    int rc = framer_next(&fr, &h, &body);
    assert(rc < 0);
    assert(framer_broken(&fr));

    /* **재동기하지 않는다.** 다시 물어도 같은 답이고, 더 넣어도 소용없다. */
    assert(framer_next(&fr, &h, &body) == rc);
    assert(framer_push(&fr, frame, sizeof(frame)) == ERR_NOT_SUPPORTED);
    assert(framer_next(&fr, &h, &body) == rc);
}

static void test_bad_version(void)
{
    uint8_t frame[WIRE_HEADER_LEN + 4];
    build(frame, 1, 1, 4, 0x90);
    frame[2] = WIRE_VERSION + 1;

    framer_t fr;
    framer_init(&fr);
    assert(framer_push(&fr, frame, sizeof(frame)) == ERR_OK);

    wire_header_t  h;
    const uint8_t *body = NULL;

    assert(framer_next(&fr, &h, &body) == ERR_NOT_SUPPORTED);
    assert(framer_broken(&fr));
}

/*
 * 뒤이어 온 멀쩡한 전문도 꺼내지 않는다. 앞이 어긋났으면 그 뒤의 경계도
 * 믿을 수 없다 — 0x4D53은 가격 필드 안에서도 나온다.
 */
static void test_no_resync(void)
{
    uint8_t stream[256];
    size_t  n = build(stream, 1, 1, 4, 0xA0);
    stream[0] = 'X';
    n += build(stream + n, 2, 2, 4, 0xB0);

    framer_t fr;
    framer_init(&fr);
    assert(framer_push(&fr, stream, n) == ERR_OK);

    wire_header_t  h;
    const uint8_t *body = NULL;

    assert(framer_next(&fr, &h, &body) < 0);
    assert(framer_next(&fr, &h, &body) < 0);
}

/* --- 버퍼 관리 --- */

/*
 * 꺼내지 않고 넣기만 하면 언젠가 찬다. 그때 조용히 덮어쓰지 않고 거절한다.
 */
static void test_full(void)
{
    framer_t fr;
    framer_init(&fr);

    uint8_t chunk[4096];
    memset(chunk, 0, sizeof(chunk));

    size_t pushed = 0;
    int    rc = ERR_OK;
    while (rc == ERR_OK) {
        rc = framer_push(&fr, chunk, sizeof(chunk));
        if (rc == ERR_OK) {
            pushed += sizeof(chunk);
        }
    }
    assert(rc == ERR_POOL_EXHAUSTED);
    assert(pushed <= WIRE_FRAME_MAX);
    assert(framer_pending(&fr) == pushed);
}

/*
 * **앞의 빈자리를 쓴다.** 전문 하나를 꺼내고 나면 그 앞부분은 쓸모가 없다.
 * 되돌리지 않으면 뒤가 모자랄 때 아직 자리가 있는데도 거절한다.
 */
static void test_compaction(void)
{
    uint8_t frame[WIRE_HEADER_LEN + 100];
    size_t  n = build(frame, 1, 1, 100, 0xC0);

    framer_t fr;
    framer_init(&fr);

    /* 전문 하나 + 다음 전문의 앞 10바이트. */
    assert(framer_push(&fr, frame, n) == ERR_OK);
    assert(framer_push(&fr, frame, 10) == ERR_OK);

    wire_header_t  h;
    const uint8_t *body = NULL;
    assert(framer_next(&fr, &h, &body) == 1);
    assert(framer_pending(&fr) == 10);

    /*
     * 남은 10바이트를 앞으로 되돌리면 딱 들어간다. 되돌리지 않으면
     * 124바이트가 낭비돼 거절당한다.
     */
    static uint8_t big[WIRE_FRAME_MAX];
    memset(big, 0, sizeof(big));
    assert(framer_push(&fr, big, WIRE_FRAME_MAX - 10) == ERR_OK);
    assert(framer_pending(&fr) == WIRE_FRAME_MAX);
}

/*
 * 같은 프레이머로 오래 쓴다. 되돌리기를 빼먹으면 여기서 찬다.
 */
static void test_long_run(void)
{
    uint8_t frame[WIRE_HEADER_LEN + 32];
    size_t  n = build(frame, 4, 0, 32, 0xD0);

    framer_t fr;
    framer_init(&fr);

    wire_header_t  h;
    const uint8_t *body = NULL;

    for (int i = 0; i < 5000; i++) {
        assert(framer_push(&fr, frame, n) == ERR_OK);
        assert(framer_next(&fr, &h, &body) == 1);
        assert(h.body_len == 32);
        check_body(body, 32, 0xD0);
        assert(framer_next(&fr, &h, &body) == 0);
    }
}

/* --- 인자 --- */

static void test_args(void)
{
    framer_t fr;
    framer_init(&fr);

    wire_header_t  h;
    const uint8_t *body = NULL;
    uint8_t        one = 0;

    assert(framer_push(NULL, &one, 1) == ERR_NULL_PTR);
    assert(framer_push(&fr, NULL, 1) == ERR_NULL_PTR);
    assert(framer_push(&fr, NULL, 0) == ERR_OK); /* 0바이트는 아무 일도 아니다 */
    assert(framer_next(NULL, &h, &body) == ERR_NULL_PTR);
    assert(framer_next(&fr, NULL, &body) == ERR_NULL_PTR);
    assert(framer_next(&fr, &h, NULL) == ERR_NULL_PTR);
    assert(framer_pending(NULL) == 0);
    assert(!framer_broken(NULL));
    assert(!framer_broken(&fr));

    framer_init(NULL); /* 죽지 않는다 */
}

int main(void)
{
    STEP(test_split_header);
    STEP(test_split_body);
    STEP(test_many_at_once);
    STEP(test_byte_at_a_time);
    STEP(test_body_too_large);
    STEP(test_bad_magic);
    STEP(test_bad_version);
    STEP(test_no_resync);
    STEP(test_full);
    STEP(test_compaction);
    STEP(test_long_run);
    STEP(test_args);
    return 0;
}
