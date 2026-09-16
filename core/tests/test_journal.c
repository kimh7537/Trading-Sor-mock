/*
 * T5-01 저널 — 덧붙이기만 하는 입력 기록.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 적은 것이 같은 순서·같은 내용으로 돌아온다
 *  2. **찢어진 꼬리**를 멀쩡한 레코드로 읽지 않는다
 *  3. 빈 파일과 남의 파일을 구분해 다룬다
 *  4. 실을 것이 없는 레코드와 가장 큰 레코드
 *  5. **바이트 단위로 자른 파일 전수** — 어디서 잘려도 앞까지는 온전하다
 *
 * 5번이 이 태스크의 핵심이다. 죽는 시점은 고를 수 없으므로 **모든 자리에서
 * 잘려 봐야** 한다. 한 자리만 시험하면 나머지에서 조용히 틀린다.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "journal.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

#define PATH "test_journal.jrn"

typedef struct {
    int      calls;
    uint8_t  types[64];
    uint64_t seqs[64];
    int64_t  tss[64];
    uint32_t lens[64];
    uint8_t  first[64];
    int      stop_after; /* 0이면 안 멈춘다 */
} sink_t;

static int on_rec(const jrec_t *r, void *ctx)
{
    sink_t *s = ctx;
    if (s->calls < 64) {
        s->types[s->calls] = r->type;
        s->seqs[s->calls] = r->seq;
        s->tss[s->calls] = r->ts;
        s->lens[s->calls] = r->len;
        s->first[s->calls] = (r->len > 0) ? r->payload[0] : 0;
    }
    s->calls++;
    if (s->stop_after > 0 && s->calls >= s->stop_after) {
        return 1; /* 여기서 멈춘다 */
    }
    return 0;
}

static void fill(uint8_t *p, uint32_t n, uint8_t tag)
{
    for (uint32_t i = 0; i < n; i++) {
        p[i] = (uint8_t)(tag + i);
    }
}

static void wipe(void)
{
    remove(PATH);
}

static long file_size(void)
{
    FILE *f = fopen(PATH, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

/* --- 1. 왕복 --- */

static void test_round_trip(void)
{
    wipe();

    journal_t *w = journal_create(PATH);
    assert(w != NULL);

    uint8_t buf[32];
    for (int i = 0; i < 5; i++) {
        fill(buf, 8, (uint8_t)(0x30 + i));
        assert(journal_append(w, (uint8_t)(i + 1), (uint64_t)(100 + i),
                              (int64_t)(900 + i), buf, 8) == ERR_OK);
    }
    assert(journal_count(w) == 5);
    journal_close(w);

    journal_t *r = journal_open_read(PATH);
    assert(r != NULL);

    sink_t s;
    memset(&s, 0, sizeof(s));
    bool torn = true;
    assert(journal_replay(r, on_rec, &s, &torn) == 5);

    /* **끝까지 온전했다.** 찢어졌다고 말하면 안 된다. */
    assert(!torn);
    assert(s.calls == 5);

    for (int i = 0; i < 5; i++) {
        assert(s.types[i] == (uint8_t)(i + 1));
        assert(s.seqs[i] == (uint64_t)(100 + i));
        assert(s.tss[i] == (int64_t)(900 + i));
        assert(s.lens[i] == 8);
        assert(s.first[i] == (uint8_t)(0x30 + i));
    }

    journal_close(r);
    wipe();
}

/* --- 4. 실을 것이 없는 레코드와 가장 큰 레코드 --- */

static void test_empty_and_max_payload(void)
{
    wipe();

    journal_t *w = journal_create(PATH);
    assert(w != NULL);

    /* 실을 것이 없어도 레코드다 — 머리만으로 뜻이 서는 사건이 있다. */
    assert(journal_append(w, 7, 1, 11, NULL, 0) == ERR_OK);

    static uint8_t big[JOURNAL_PAYLOAD_MAX + 1];
    fill(big, JOURNAL_PAYLOAD_MAX, 0xA0);
    assert(journal_append(w, 8, 2, 22, big, JOURNAL_PAYLOAD_MAX) == ERR_OK);

    /* 한도를 넘으면 **조용히 자르지 않고 거절한다.** */
    assert(journal_append(w, 9, 3, 33, big, JOURNAL_PAYLOAD_MAX + 1) ==
           ERR_INVALID_ARG);

    journal_close(w);

    journal_t *r = journal_open_read(PATH);
    sink_t     s;
    memset(&s, 0, sizeof(s));
    bool torn = true;
    assert(journal_replay(r, on_rec, &s, &torn) == 2);
    assert(!torn);
    assert(s.lens[0] == 0);
    assert(s.lens[1] == JOURNAL_PAYLOAD_MAX);
    assert(s.first[1] == 0xA0);

    journal_close(r);
    wipe();
}

/* --- 3. 빈 파일과 남의 파일 --- */

static void test_empty_file(void)
{
    wipe();

    journal_t *w = journal_create(PATH);
    assert(w != NULL);
    journal_close(w);

    journal_t *r = journal_open_read(PATH);
    assert(r != NULL);

    sink_t s;
    memset(&s, 0, sizeof(s));
    bool torn = true;
    assert(journal_replay(r, on_rec, &s, &torn) == 0);
    /* **빈 것과 찢어진 것은 다르다.** 빈 파일은 정상이다. */
    assert(!torn);
    assert(s.calls == 0);

    journal_close(r);
    wipe();
}

static void test_foreign_file(void)
{
    wipe();

    FILE *f = fopen(PATH, "wb");
    assert(f != NULL);
    const char *junk = "이것은 저널이 아니다. 아무 파일이나 읽히면 안 된다.";
    assert(fwrite(junk, 1, strlen(junk), f) == strlen(junk));
    fclose(f);

    journal_t *r = journal_open_read(PATH);
    sink_t     s;
    memset(&s, 0, sizeof(s));
    bool torn = false;
    assert(journal_replay(r, on_rec, &s, &torn) == 0);
    /* 한 건도 읽지 않았고, 정상이라고 하지도 않았다. */
    assert(torn);
    assert(s.calls == 0);

    journal_close(r);
    wipe();
}

/* --- 2·5. 찢어진 꼬리 — 바이트 단위 전수 --- */

/*
 * **어디서 잘려도 앞까지는 온전해야 한다.**
 *
 * 죽는 시점은 고를 수 없다. 한 자리만 시험하면 나머지 자리에서 조용히
 * 틀린다 — 특히 길이 필드가 반만 써진 경우가 위험하다.
 */
static void test_every_truncation(void)
{
    wipe();

    /* 레코드 셋. 크기를 달리해 경계가 겹치지 않게 한다. */
    const uint32_t lens[3] = {0, 5, 40};
    journal_t     *w = journal_create(PATH);
    assert(w != NULL);

    uint8_t buf[64];
    for (int i = 0; i < 3; i++) {
        fill(buf, lens[i], (uint8_t)(0x50 + i));
        assert(journal_append(w, (uint8_t)(i + 1), (uint64_t)(i + 1),
                              (int64_t)(i + 1), lens[i] ? buf : NULL,
                              lens[i]) == ERR_OK);
    }
    journal_close(w);

    long full = file_size();

    /* 온전한 파일을 따로 둔다. 자를 때마다 되돌려야 한다. */
    static uint8_t backup[4096];
    assert(full <= (long)sizeof(backup));
    FILE  *f = fopen(PATH, "rb");
    size_t n = fread(backup, 1, sizeof(backup), f);
    fclose(f);
    assert((long)n == full);

    /* 레코드가 끝나는 자리. */
    long ends[3];
    long at = 0;
    for (int i = 0; i < 3; i++) {
        at += (long)(JOURNAL_HEADER_LEN + lens[i] + JOURNAL_CRC_LEN);
        ends[i] = at;
    }

    for (long cut = 0; cut <= full; cut++) {
        f = fopen(PATH, "wb");
        assert(f != NULL);
        assert(fwrite(backup, 1, (size_t)cut, f) == (size_t)cut);
        fclose(f);

        journal_t *r = journal_open_read(PATH);
        assert(r != NULL);

        sink_t s;
        memset(&s, 0, sizeof(s));
        bool torn = false;
        int  got = journal_replay(r, on_rec, &s, &torn);
        journal_close(r);

        /* 잘린 자리까지 완전히 들어간 레코드가 몇 개인지 센다. */
        int whole = 0;
        for (int i = 0; i < 3; i++) {
            if (cut >= ends[i]) {
                whole++;
            }
        }

        assert(got == whole);
        assert(s.calls == whole);

        /*
         * **레코드 경계에서 정확히 잘렸으면 온전한 것이고, 아니면 찢어진
         * 것이다.** 이 둘을 구분하지 못하면 "원래 거기까지였다"와
         * "쓰다 죽었다"가 같아진다.
         */
        bool on_boundary = (cut == 0);
        for (int i = 0; i < 3; i++) {
            if (cut == ends[i]) {
                on_boundary = true;
            }
        }
        assert(torn == !on_boundary);

        /* 읽어 낸 것들은 내용까지 원본과 같다. */
        for (int i = 0; i < whole; i++) {
            assert(s.types[i] == (uint8_t)(i + 1));
            assert(s.seqs[i] == (uint64_t)(i + 1));
            assert(s.lens[i] == lens[i]);
        }
    }

    wipe();
}

/*
 * **길이가 깨진 레코드가 버퍼를 넘지 않는다.**
 *
 * 잘린 파일만으로는 이것을 시험할 수 없다. 잘린 파일은 곧 끝나므로 아무리
 * 큰 길이를 읽으려 해도 짧게 읽히고 만다 — 넘칠 자리가 없다.
 *
 * 넘치려면 **터무니없는 길이 뒤에 실제로 그만큼의 바이트가 있어야** 한다.
 * 디스크가 망가지거나 남의 파일을 열면 그런 모양이 나온다. 손으로 만든다.
 */
static void test_absurd_length_does_not_overflow(void)
{
    wipe();

    FILE *f = fopen(PATH, "wb");
    assert(f != NULL);

    /* 머리는 멀쩡해 보이게 만든다 — magic도 맞다. */
    uint8_t head[JOURNAL_HEADER_LEN];
    memset(head, 0, sizeof(head));
    head[0] = 0x4D; /* M */
    head[1] = 0x53; /* S */
    head[2] = 0x4A; /* J */
    head[3] = 0x31; /* 1 */
    head[4] = 1;    /* type */
    /* 길이를 한도보다 크게 적는다(빅엔디언). */
    uint32_t bogus = JOURNAL_PAYLOAD_MAX + 4096u;
    head[8] = (uint8_t)(bogus >> 24);
    head[9] = (uint8_t)(bogus >> 16);
    head[10] = (uint8_t)(bogus >> 8);
    head[11] = (uint8_t)bogus;
    assert(fwrite(head, 1, sizeof(head), f) == sizeof(head));

    /* **적어 둔 길이만큼 실제로 채운다.** 이게 있어야 넘칠 수 있다. */
    static uint8_t filler[JOURNAL_PAYLOAD_MAX + 8192u];
    memset(filler, 0xCC, sizeof(filler));
    assert(fwrite(filler, 1, bogus + JOURNAL_CRC_LEN, f) ==
           (size_t)bogus + JOURNAL_CRC_LEN);
    fclose(f);

    journal_t *r = journal_open_read(PATH);
    assert(r != NULL);

    sink_t s;
    memset(&s, 0, sizeof(s));
    bool torn = false;

    /* 한 건도 읽지 않고, 넘치지도 않는다. */
    assert(journal_replay(r, on_rec, &s, &torn) == 0);
    assert(torn);
    assert(s.calls == 0);

    journal_close(r);
    wipe();
}

/* CRC가 한 비트만 뒤집혀도 잡는다 */
static void test_bit_flip_caught(void)
{
    wipe();

    journal_t *w = journal_create(PATH);
    uint8_t    buf[16];
    fill(buf, 16, 0x70);
    assert(journal_append(w, 1, 1, 1, buf, 16) == ERR_OK);
    assert(journal_append(w, 2, 2, 2, buf, 16) == ERR_OK);
    journal_close(w);

    /* 첫 레코드의 실을 것 한 바이트를 뒤집는다. */
    FILE *f = fopen(PATH, "r+b");
    assert(f != NULL);
    assert(fseek(f, (long)JOURNAL_HEADER_LEN + 3, SEEK_SET) == 0);
    int c = fgetc(f);
    assert(c != EOF);
    assert(fseek(f, (long)JOURNAL_HEADER_LEN + 3, SEEK_SET) == 0);
    assert(fputc(c ^ 0x01, f) != EOF);
    fclose(f);

    journal_t *r = journal_open_read(PATH);
    sink_t     s;
    memset(&s, 0, sizeof(s));
    bool torn = false;
    /* 첫 레코드에서 걸리므로 하나도 못 읽는다. */
    assert(journal_replay(r, on_rec, &s, &torn) == 0);
    assert(torn);

    journal_close(r);
    wipe();
}

/* 재생을 도중에 멈출 수 있다 */
static void test_callback_can_stop(void)
{
    wipe();

    journal_t *w = journal_create(PATH);
    uint8_t    buf[4] = {1, 2, 3, 4};
    for (int i = 0; i < 5; i++) {
        assert(journal_append(w, 1, (uint64_t)i, 0, buf, 4) == ERR_OK);
    }
    journal_close(w);

    journal_t *r = journal_open_read(PATH);
    sink_t     s;
    memset(&s, 0, sizeof(s));
    s.stop_after = 3;
    assert(journal_replay(r, on_rec, &s, NULL) == 1); /* 콜백이 돌려준 값 */
    assert(s.calls == 3);

    journal_close(r);
    wipe();
}

/* 읽기용으로 연 것에는 적을 수 없다 */
static void test_read_only_rejects_append(void)
{
    wipe();

    journal_t *w = journal_create(PATH);
    uint8_t    buf[4] = {1, 2, 3, 4};
    assert(journal_append(w, 1, 1, 1, buf, 4) == ERR_OK);
    journal_close(w);

    journal_t *r = journal_open_read(PATH);
    assert(journal_append(r, 1, 2, 2, buf, 4) == ERR_NOT_SUPPORTED);
    journal_close(r);
    wipe();
}

static void test_args(void)
{
    uint8_t buf[4] = {1, 2, 3, 4};

    assert(journal_create(NULL) == NULL);
    assert(journal_open_read(NULL) == NULL);
    assert(journal_open_read("__없는_파일__.jrn") == NULL);

    assert(journal_append(NULL, 1, 1, 1, buf, 4) == ERR_NULL_PTR);
    assert(journal_sync(NULL) == ERR_NULL_PTR);
    assert(journal_replay(NULL, on_rec, NULL, NULL) == ERR_NULL_PTR);
    assert(journal_count(NULL) == 0);

    wipe();
    journal_t *w = journal_create(PATH);
    assert(journal_append(w, 1, 1, 1, NULL, 4) == ERR_NULL_PTR);
    journal_close(w);
    journal_close(NULL); /* 죽지 않는다 */
    wipe();
}

/* CRC가 실제로 IEEE CRC32인지 — 공개된 검증 벡터로 확인한다 */
static void test_crc32_known_value(void)
{
    const char *s = "123456789";
    assert(journal_crc32((const uint8_t *)s, 9) == 0xCBF43926u);
    assert(journal_crc32((const uint8_t *)"", 0) == 0u);
}

int main(void)
{
    STEP(test_crc32_known_value);
    STEP(test_round_trip);
    STEP(test_empty_and_max_payload);
    STEP(test_empty_file);
    STEP(test_foreign_file);
    STEP(test_every_truncation);
    STEP(test_absurd_length_does_not_overflow);
    STEP(test_bit_flip_caught);
    STEP(test_callback_can_stop);
    STEP(test_read_only_rejects_append);
    STEP(test_args);
    return 0;
}
