/*
 * T5-02 스냅샷과 재기동 복구.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 스냅샷 왕복 — 담은 지점(`upto_seq`)까지 같이 돌아온다
 *  2. **이어 재생** — 스냅샷 이후의 저널만 반영한다. 두 번 반영하지 않는다
 *  3. 스냅샷이 깨졌으면 무시하고 저널만으로 복구한다
 *  4. 스냅샷이 없어도 복구된다
 *  5. **쓰다 죽어도 원래 자리에는 반쪽이 남지 않는다**
 *
 * 2번이 이 태스크의 핵심이다. 건너뛰기를 빼먹으면 같은 주문이 두 번 반영되고,
 * 그 틀림은 장이 끝난 뒤 잔고가 안 맞을 때에야 드러난다.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "journal.h"
#include "snapshot.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

#define SNAP "test_snap.snp"
#define JRN "test_snap.jrn"

/* 되살아나는 상태. */
typedef struct {
    int      loaded; /* load가 불린 횟수 */
    uint32_t loaded_len;
    uint8_t  loaded_first;
    int      applied;
    uint64_t seqs[64];
} state_t;

static int st_load(const uint8_t *data, uint32_t len, void *ctx)
{
    state_t *s = ctx;
    s->loaded++;
    s->loaded_len = len;
    s->loaded_first = (len > 0) ? data[0] : 0;
    return 0;
}

static int st_apply(const jrec_t *rec, void *ctx)
{
    state_t *s = ctx;
    if (s->applied < 64) {
        s->seqs[s->applied] = rec->seq;
    }
    s->applied++;
    return 0;
}

static snap_apply_fn fns_for(state_t *s)
{
    snap_apply_fn f;
    f.load = st_load;
    f.apply = st_apply;
    f.ctx = s;
    return f;
}

static void wipe(void)
{
    remove(SNAP);
    remove(SNAP ".tmp");
    remove(JRN);
}

/* 저널에 seq 1..n을 적는다. */
static void write_journal(uint64_t n)
{
    journal_t *w = journal_create(JRN);
    assert(w != NULL);
    for (uint64_t i = 1; i <= n; i++) {
        uint8_t b[4] = {(uint8_t)i, 0, 0, 0};
        assert(journal_append(w, 1, i, (int64_t)i, b, 4) == ERR_OK);
    }
    journal_close(w);
}

/* --- 1. 스냅샷 왕복 --- */

static void test_snapshot_round_trip(void)
{
    wipe();

    uint8_t body[32];
    for (int i = 0; i < 32; i++) {
        body[i] = (uint8_t)(0x10 + i);
    }

    assert(snapshot_write(SNAP, 42, body, 32) == ERR_OK);

    uint64_t seq = 0;
    uint8_t  got[64];
    assert(snapshot_read(SNAP, &seq, got, sizeof(got)) == 32);
    assert(seq == 42); /* **담은 지점이 함께 온다** */
    assert(memcmp(got, body, 32) == 0);

    /* 본문이 없어도 된다 — 빈 상태도 상태다. */
    assert(snapshot_write(SNAP, 7, NULL, 0) == ERR_OK);
    assert(snapshot_read(SNAP, &seq, got, sizeof(got)) == 0);
    assert(seq == 7);

    wipe();
}

/* 임시 파일을 남기지 않는다 */
static void test_no_temp_left_behind(void)
{
    wipe();

    uint8_t body[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    assert(snapshot_write(SNAP, 1, body, 8) == ERR_OK);

    FILE *t = fopen(SNAP ".tmp", "rb");
    assert(t == NULL); /* 이름을 바꿨으므로 임시 파일은 없다 */

    wipe();
}

/* --- 2. 이어 재생 --- */

/*
 * **스냅샷 이후만 반영한다.** 이것이 이 태스크의 전부다.
 */
static void test_replay_resumes_after_snapshot(void)
{
    wipe();
    write_journal(10);

    uint8_t body[4] = {0xAA, 0, 0, 0};
    assert(snapshot_write(SNAP, 6, body, 4) == ERR_OK); /* 6번까지 담았다 */

    state_t s;
    memset(&s, 0, sizeof(s));
    snap_apply_fn    f = fns_for(&s);
    recover_result_t r;
    assert(recover(SNAP, JRN, &f, &r) == ERR_OK);

    assert(r.used_snapshot);
    assert(!r.snapshot_bad);
    assert(r.from_seq == 6);

    /* **7..10만 반영했다.** 1..6은 이미 스냅샷에 있다. */
    assert(r.replayed == 4);
    assert(s.applied == 4);
    assert(s.seqs[0] == 7);
    assert(s.seqs[3] == 10);

    /* 스냅샷 본문이 상태에 실려 들어갔다. */
    assert(s.loaded == 1);
    assert(s.loaded_len == 4);
    assert(s.loaded_first == 0xAA);

    wipe();
}

/* 스냅샷이 저널 끝까지 담았으면 재생할 것이 없다 */
static void test_snapshot_covers_everything(void)
{
    wipe();
    write_journal(5);
    assert(snapshot_write(SNAP, 5, NULL, 0) == ERR_OK);

    state_t s;
    memset(&s, 0, sizeof(s));
    snap_apply_fn    f = fns_for(&s);
    recover_result_t r;
    assert(recover(SNAP, JRN, &f, &r) == ERR_OK);

    assert(r.used_snapshot);
    assert(r.replayed == 0);
    assert(s.applied == 0);

    wipe();
}

/* --- 3·4. 스냅샷이 없거나 깨졌을 때 --- */

static void test_no_snapshot_replays_all(void)
{
    wipe();
    write_journal(5);

    state_t s;
    memset(&s, 0, sizeof(s));
    snap_apply_fn    f = fns_for(&s);
    recover_result_t r;
    assert(recover(SNAP, JRN, &f, &r) == ERR_OK); /* 스냅샷 파일이 없다 */

    assert(!r.used_snapshot);
    assert(!r.snapshot_bad); /* **없는 것과 깨진 것은 다르다** */
    assert(r.replayed == 5);
    assert(s.loaded == 0); /* 없으면 load를 부르지 않는다 */
    assert(s.seqs[0] == 1);

    wipe();
}

/*
 * **깨진 스냅샷은 버리고 저널만으로 간다.** 저널이 진실이므로 느릴 뿐
 * 틀리지 않는다. 다만 그 사실을 숨기지 않는다.
 */
static void test_bad_snapshot_falls_back(void)
{
    wipe();
    write_journal(5);

    uint8_t body[16];
    memset(body, 0x55, sizeof(body));
    assert(snapshot_write(SNAP, 3, body, 16) == ERR_OK);

    /* 본문 한 바이트를 뒤집는다 — CRC가 안 맞게 된다. */
    FILE *f = fopen(SNAP, "r+b");
    assert(f != NULL);
    assert(fseek(f, (long)SNAPSHOT_HEADER_LEN + 2, SEEK_SET) == 0);
    int c = fgetc(f);
    assert(c != EOF);
    assert(fseek(f, (long)SNAPSHOT_HEADER_LEN + 2, SEEK_SET) == 0);
    assert(fputc(c ^ 0x01, f) != EOF);
    fclose(f);

    uint64_t seq = 0;
    uint8_t  got[64];
    assert(snapshot_read(SNAP, &seq, got, sizeof(got)) == ERR_IO);

    state_t s;
    memset(&s, 0, sizeof(s));
    snap_apply_fn    fn = fns_for(&s);
    recover_result_t r;
    assert(recover(SNAP, JRN, &fn, &r) == ERR_OK);

    assert(!r.used_snapshot);
    assert(r.snapshot_bad); /* **숨기지 않는다** */
    assert(r.from_seq == 0);
    /* 처음부터 다 재생했다 — 느릴 뿐 틀리지 않는다. */
    assert(r.replayed == 5);
    assert(s.loaded == 0);
    assert(s.seqs[0] == 1);

    wipe();
}

/* --- 5. 쓰다 죽은 모양 --- */

/*
 * **반쪽 스냅샷이 원래 자리에 남지 않는다.**
 *
 * 쓰다 죽는 것을 흉내 내기 위해, 임시 파일만 만들어 두고 이름 바꾸기가
 * 일어나지 않은 상태를 만든다. 원래 자리에는 옛 스냅샷이 그대로 있어야 한다.
 */
static void test_crash_before_rename_keeps_old(void)
{
    wipe();

    uint8_t old_body[4] = {0x11, 0x22, 0x33, 0x44};
    assert(snapshot_write(SNAP, 100, old_body, 4) == ERR_OK);

    /* 쓰다 죽은 임시 파일을 손으로 만든다. */
    FILE *t = fopen(SNAP ".tmp", "wb");
    assert(t != NULL);
    const char *half = "반쪽";
    assert(fwrite(half, 1, strlen(half), t) == strlen(half));
    fclose(t);

    /* **원래 자리는 멀쩡하다.** 옛 스냅샷이 그대로 읽힌다. */
    uint64_t seq = 0;
    uint8_t  got[16];
    assert(snapshot_read(SNAP, &seq, got, sizeof(got)) == 4);
    assert(seq == 100);
    assert(memcmp(got, old_body, 4) == 0);

    wipe();
}

/* 잘린 스냅샷은 읽히지 않는다 */
static void test_truncated_snapshot_rejected(void)
{
    wipe();

    uint8_t body[64];
    memset(body, 0x77, sizeof(body));
    assert(snapshot_write(SNAP, 9, body, 64) == ERR_OK);

    FILE          *f = fopen(SNAP, "rb");
    static uint8_t all[512];
    size_t         n = fread(all, 1, sizeof(all), f);
    fclose(f);
    assert(n > 20);

    for (size_t cut = 0; cut < n; cut++) {
        f = fopen(SNAP, "wb");
        assert(f != NULL);
        assert(fwrite(all, 1, cut, f) == cut);
        fclose(f);

        uint64_t seq = 12345;
        uint8_t  got[128];
        int      rc = snapshot_read(SNAP, &seq, got, sizeof(got));
        /* 어디서 잘려도 온전한 스냅샷으로 읽히지 않는다. */
        assert(rc < 0);
        assert(seq == 12345); /* 건드리지 않았다 */
    }

    wipe();
}

/*
 * **남의 파일은 CRC가 맞아도 거절한다.**
 *
 * 깨진 파일은 CRC가 잡는다. 잡지 못하는 것은 **온전하지만 우리 것이 아닌**
 * 파일이다 — 다른 판의 스냅샷, 남이 같은 이름으로 쓴 파일. 그 자리를 지키는
 * 것은 magic뿐이라서, CRC까지 맞춰 놓고 magic만 틀린 입력으로 확인한다.
 * (이것을 안 보고 있어서 magic 검사 삭제 변이가 살아남았다 — S4)
 */
static void test_foreign_file_rejected(void)
{
    wipe();

    uint8_t img[SNAPSHOT_HEADER_LEN + 4 + 4];
    memset(img, 0, sizeof(img));
    /* magic만 "MSS2" — 나머지는 온전한 스냅샷이다. */
    img[0] = 0x4D;
    img[1] = 0x53;
    img[2] = 0x53;
    img[3] = 0x32;
    img[7] = 4;  /* len = 4 */
    img[15] = 5; /* upto_seq = 5 */
    img[SNAPSHOT_HEADER_LEN + 0] = 0xDE;
    img[SNAPSHOT_HEADER_LEN + 1] = 0xAD;
    img[SNAPSHOT_HEADER_LEN + 2] = 0xBE;
    img[SNAPSHOT_HEADER_LEN + 3] = 0xEF;

    uint32_t crc = journal_crc32(img, SNAPSHOT_HEADER_LEN + 4);
    for (unsigned i = 0; i < 4; i++) {
        img[SNAPSHOT_HEADER_LEN + 4 + i] = (uint8_t)(crc >> (24u - 8u * i));
    }

    FILE *f = fopen(SNAP, "wb");
    assert(f != NULL);
    assert(fwrite(img, 1, sizeof(img), f) == sizeof(img));
    fclose(f);

    uint64_t seq = 999;
    uint8_t  got[16];
    assert(snapshot_read(SNAP, &seq, got, sizeof(got)) == ERR_IO);
    assert(seq == 999); /* 손대지 않았다 */

    wipe();
}

/*
 * 길이 필드가 터무니없어도 넘치지 않는다.
 *
 * 이 테스트는 길이 검사를 지운 변이(S5)를 잡지 못한다 — 넘쳐 쓴 뒤에도
 * CRC가 안 맞아 겉보기 결과가 같기 때문이다. 그래도 남긴다: 이 입력에
 * 무엇이 나와야 하는지를 못 박아 둔다.
 */
static void test_absurd_length_does_not_overflow(void)
{
    wipe();

    uint8_t hdr[SNAPSHOT_HEADER_LEN];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 0x4D;
    hdr[1] = 0x53;
    hdr[2] = 0x53;
    hdr[3] = 0x31;
    memset(hdr + 4, 0xFF, 4); /* len = 4294967295 */

    FILE *f = fopen(SNAP, "wb");
    assert(f != NULL);
    assert(fwrite(hdr, 1, sizeof(hdr), f) == sizeof(hdr));
    uint8_t junk[4096];
    memset(junk, 0xCC, sizeof(junk));
    assert(fwrite(junk, 1, sizeof(junk), f) == sizeof(junk));
    fclose(f);

    uint64_t seq = 999;
    uint8_t  got[16];
    assert(snapshot_read(SNAP, &seq, got, sizeof(got)) == ERR_IO);
    assert(seq == 999);

    wipe();
}

/* --- 인자 --- */

static void test_args(void)
{
    wipe();

    uint8_t  body[4] = {1, 2, 3, 4};
    uint64_t seq = 0;
    uint8_t  got[8];

    assert(snapshot_write(NULL, 1, body, 4) == ERR_NULL_PTR);
    assert(snapshot_write(SNAP, 1, NULL, 4) == ERR_NULL_PTR);
    assert(snapshot_write(SNAP, 1, body, SNAPSHOT_MAX + 1) == ERR_INVALID_ARG);

    assert(snapshot_read(NULL, &seq, got, sizeof(got)) == ERR_NULL_PTR);
    assert(snapshot_read(SNAP, NULL, got, sizeof(got)) == ERR_NULL_PTR);
    assert(snapshot_read("__없는_스냅샷__.snp", &seq, got, sizeof(got)) ==
           ERR_NOT_FOUND);

    /* 받을 자리가 모자라면 넘치지 않고 거절한다. */
    assert(snapshot_write(SNAP, 1, body, 4) == ERR_OK);
    assert(snapshot_read(SNAP, &seq, got, 2) == ERR_INVALID_ARG);

    state_t s;
    memset(&s, 0, sizeof(s));
    snap_apply_fn f = fns_for(&s);
    assert(recover(NULL, NULL, &f, NULL) == ERR_NULL_PTR);
    assert(recover(SNAP, JRN, NULL, NULL) == ERR_NULL_PTR);

    /* 저널도 스냅샷도 없으면 되살릴 것이 없다. */
    remove(JRN);
    remove(SNAP);
    assert(recover(SNAP, JRN, &f, NULL) == ERR_NOT_FOUND);

    wipe();
}

int main(void)
{
    STEP(test_snapshot_round_trip);
    STEP(test_no_temp_left_behind);
    STEP(test_replay_resumes_after_snapshot);
    STEP(test_snapshot_covers_everything);
    STEP(test_no_snapshot_replays_all);
    STEP(test_bad_snapshot_falls_back);
    STEP(test_crash_before_rename_keeps_old);
    STEP(test_truncated_snapshot_rejected);
    STEP(test_foreign_file_rejected);
    STEP(test_absurd_length_does_not_overflow);
    STEP(test_args);
    return 0;
}
