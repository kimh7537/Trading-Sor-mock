/*
 * T5-04 장애 주입 — 진짜로 프로세스를 죽인다.
 *
 * T5-01은 손으로 자른 파일을 먹였고, T5-02는 손으로 만든 `.tmp`를 놓았다.
 * **뒷모습을 흉내 낸 것과 실제로 그 일이 일어난 것은 다르다.** 흉내는 내가
 * 상상한 모양만 만든다 — 상상하지 못한 모양은 시험되지 않는다.
 *
 * 여기서는 자식을 `SIGKILL`로 죽인다. 잡을 수도, 미룰 수도, 정리할 수도
 * 없는 신호다.
 *
 * (계좌 잠금을 쥔 채 죽는 경우는 `ledger/tests/test_account.c`가 이미
 *  다룬다. 겹쳐 쓰지 않는다.)
 *
 * ===========================================================================
 * 이 테스트가 시험하지 **못하는** 것
 * ===========================================================================
 *
 * 변이 검사로 확인한 경계다. 적어 두지 않으면 다음 사람이 "장애 주입이
 * 있으니 괜찮다"고 믿는다.
 *
 *  - **`fsync`를 빼도 이 테스트는 통과한다.** `SIGKILL`은 프로세스만 죽이고
 *    커널 페이지 캐시는 그대로 두므로, 밀어 넣지 않은 바이트도 다음 프로세스가
 *    멀쩡히 읽는다. **프로세스를 죽이는 것과 전원을 끊는 것은 다르다.**
 *    `fsync`의 값은 전원이 끊길 때만 드러나고, 그 상황은 여기서 만들 수 없다.
 *    (T5-02의 S12에서 짐작만 했던 것을 여기서 실제로 확인했다.)
 *    반면 **`fflush`를 빼면 잡힌다** — 그 버퍼는 프로세스 안에 있어서
 *    프로세스와 함께 사라지기 때문이다. 둘의 차이가 정확히 이것이다.
 *
 *  - **저널의 CRC 검사를 빼도 통과한다.** 죽은 쓰기는 **짧은** 레코드를
 *    남기지 **온전하되 틀린** 레코드를 남기지 않는다. 짧은 것은 길이 검사가
 *    먼저 잡는다. CRC가 지키는 것은 나중에 썩는 비트와 깨진 길이 필드이지
 *    중간에 죽은 쓰기가 아니다 — 그쪽은 T5-01이 손으로 만든 파일로 본다.
 */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"
#include "journal.h"
#include "recon.h"
#include "snapshot.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

#define JRN "test_fi.jrn"
#define SNAP "test_fi.snp"

/*
 * 자식과 나눌 자리. `fork` 전에 잡아야 양쪽이 같은 쪽을 본다.
 *
 * 여기 담기는 것은 **자식이 "여기까지는 디스크에 닿았다"고 알리는 수**다.
 * 자식이 죽어도 이 값은 남는다 — 그래서 무엇을 요구할 수 있는지 안다.
 */
typedef struct {
    volatile int32_t durable; /* fsync까지 끝난 레코드 수 */
    volatile int32_t rounds;  /* 스냅샷을 몇 번 다 썼는가 */
} shared_t;

static shared_t *share_new(void)
{
    void *p = mmap(NULL, sizeof(shared_t), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(p != MAP_FAILED);
    memset(p, 0, sizeof(shared_t));
    return p;
}

static void share_free(shared_t *s)
{
    assert(munmap((void *)s, sizeof(shared_t)) == 0);
}

/* 짧게 쉰다. 바쁜 대기로 자식의 몫을 뺏지 않는다. */
static void nap(void)
{
    struct timespec ts = {0, 1000000}; /* 1ms */
    nanosleep(&ts, NULL);
}

/*
 * 조건이 설 때까지 기다리되 **상한을 둔다.**
 * 자식이 영영 안 오면 테스트가 매달린다 — 매달린 테스트는 실패보다 나쁘다.
 */
#define WAIT_UNTIL(cond)                                                    \
    do {                                                                    \
        int tries_ = 0;                                                     \
        while (!(cond) && tries_ < 3000) {                                  \
            nap();                                                          \
            tries_++;                                                       \
        }                                                                   \
        assert(cond);                                                       \
    } while (0)

/* 죽이고 거둔다. SIGKILL은 잡히지 않으므로 반드시 온다. */
static void kill_and_reap(pid_t pid)
{
    assert(kill(pid, SIGKILL) == 0);

    int st = 0;
    assert(waitpid(pid, &st, 0) == pid);
    assert(WIFSIGNALED(st));
    assert(WTERMSIG(st) == SIGKILL);
}

static void wipe(void)
{
    remove(JRN);
    remove(SNAP);
    remove(SNAP ".tmp");
}

/* 파일이 있는가. 자식이 남긴 것을 **발견한 그대로** 보려고 쓴다. */
static bool exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fclose(f);
    return true;
}

/* --- 1. fsync된 것은 SIGKILL로도 사라지지 않는다 --- */

static int count_recs(const jrec_t *rec, void *ctx)
{
    (void)rec;
    (*(int *)ctx)++;
    return 0;
}

/*
 * **`fsync`까지 끝난 레코드는 죽여도 남는다.**
 *
 * 자식은 레코드를 하나 적을 때마다 `durable`을 올린다. `journal_append`가
 * 돌아왔다는 것은 `fsync`가 끝났다는 뜻이므로, 부모가 그 수를 보고 죽이면
 * **그 수만큼은 반드시 읽혀야 한다.**
 *
 * 이것이 T5-01의 "매번 fsync" 판단이 값을 하는 자리다. 묶어서 밀었다면
 * 여기서 잃었을 것이다.
 */
static void test_fsynced_records_survive_sigkill(void)
{
    wipe();
    shared_t *sh = share_new();

    const int32_t want = 37; /* 맞아떨어지지 않는 수로 고른다 */

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        journal_t *w = journal_create(JRN);
        if (w == NULL) {
            _exit(1);
        }
        for (uint64_t i = 1;; i++) {
            uint8_t b[8];
            memset(b, (int)(i & 0xFF), sizeof(b));
            if (journal_append(w, 2, i, (int64_t)i * 3, b, sizeof(b)) !=
                ERR_OK) {
                _exit(1);
            }
            /* **적힌 뒤에 올린다.** 순서가 뒤집히면 이 테스트가 거짓말을 한다 */
            sh->durable = (int32_t)i;

            if ((int32_t)i >= want) {
                nap(); /* 부모가 죽일 틈을 준다 */
            }
        }
    }

    WAIT_UNTIL(sh->durable >= want);
    int32_t seen = sh->durable;
    kill_and_reap(pid);

    /* 자식은 여기까지 적었다고 말했다. 그 말이 지켜지는지 본다. */
    journal_t *rd = journal_open_read(JRN);
    assert(rd != NULL);
    int  n = 0;
    bool torn = false;
    int  played = journal_replay(rd, count_recs, &n, &torn);
    journal_close(rd);

    assert(played >= 0);
    assert(n == played);

    /*
     * **약속한 만큼은 반드시 있다.** 죽인 뒤에 자식이 한 건 더 적었을 수는
     * 있으므로 더 많은 것은 괜찮다. 적은 것은 절대 안 된다.
     */
    assert(played >= seen);

    fprintf(stderr, "  자식이 약속한 %d건, 읽힌 %d건, 꼬리 찢어짐 %s\n", seen,
            played, torn ? "있음" : "없음");

    share_free(sh);
    wipe();
}

/*
 * **진짜로 찢어진 꼬리를 만든다.**
 *
 * 위 테스트는 `nap()` 중에 죽으므로 레코드가 온전한 채로 끝난다. 큰 레코드를
 * 쉬지 않고 적게 하면 `fwrite` 한가운데에서 죽는 일이 생긴다 — T5-01이
 * 손으로 잘라서 흉내 냈던 그 모양이 실제로 만들어진다.
 *
 * 요구하는 것은 둘이다. 약속한 것은 **하나도 잃지 않고**, 약속하지 않은 것을
 * **하나도 지어내지 않는다.**
 */
static void test_torn_tail_from_real_kill(void)
{
    const int32_t ROUNDS = 25;
    int32_t       torn_seen = 0;

    for (int32_t r = 0; r < ROUNDS; r++) {
        wipe();
        shared_t *sh = share_new();

        pid_t pid = fork();
        assert(pid >= 0);

        if (pid == 0) {
            journal_t *w = journal_create(JRN);
            if (w == NULL) {
                _exit(1);
            }
            static uint8_t big[60000];
            memset(big, 0x3C, sizeof(big));
            for (uint64_t i = 1;; i++) {
                if (journal_append(w, 4, i, (int64_t)i, big, sizeof(big)) !=
                    ERR_OK) {
                    _exit(1);
                }
                sh->durable = (int32_t)i;
            }
        }

        /* 쉬지 않고 적는 중에 죽인다. 라운드마다 조금씩 다른 때에. */
        WAIT_UNTIL(sh->durable >= 1);
        for (int32_t i = 0; i < r % 5; i++) {
            nap();
        }
        kill_and_reap(pid);
        int32_t seen = sh->durable;

        journal_t *rd = journal_open_read(JRN);
        assert(rd != NULL);
        int  n = 0;
        bool torn = false;
        int  played = journal_replay(rd, count_recs, &n, &torn);
        journal_close(rd);

        /* 약속한 것은 잃지 않는다 */
        assert(played >= seen);
        /*
         * 약속하지 않은 것을 지어내지 않는다. 죽는 순간 자식이 한 건을
         * 쓰는 중이었을 수 있으므로 **많아야 하나**다. 찢어진 꼬리를 멀쩡한
         * 레코드로 읽으면 여기서 걸린다.
         */
        assert(played <= seen + 1);

        if (torn) {
            torn_seen++;
            /* 찢어졌다면 그 반쪽은 세지 않았다 */
            assert(played == seen);
        }

        share_free(sh);
        wipe();
    }

    fprintf(stderr, "  %d회 중 꼬리가 실제로 찢어진 것 %d회\n", ROUNDS,
            torn_seen);
}

/* --- 2. 죽는 시점을 고를 수 없으므로 여러 번 죽여 본다 --- */

/*
 * **스냅샷은 반쪽인 적이 없다.**
 *
 * 자식이 스냅샷을 쉬지 않고 덮어쓰는 동안 서로 다른 시점에 반복해 죽인다.
 * 어느 시점에 죽든 원래 자리는 **온전한 스냅샷**이어야 한다 — 새것이든
 * 옛것이든. `rename`이 원자적이라는 주장이 여기서 시험된다.
 *
 * T5-01의 "바이트 단위 절단 전수"와 같은 생각이다. 죽는 시점을 고를 수
 * 없으면 여러 번 죽여서 고른 것과 비슷하게 만든다.
 */
static void test_snapshot_never_half_written(void)
{
    const int32_t ROUNDS = 40;
    int32_t       kept_old = 0; /* 새것이 아직 안 걸린 횟수 */
    int32_t       tmp_left = 0; /* .tmp가 남은 횟수 */

    for (int32_t r = 0; r < ROUNDS; r++) {
        wipe();
        shared_t *sh = share_new();

        /* 죽기 전에 이미 있던 스냅샷. 최악이어도 이것으로 돌아가야 한다. */
        uint8_t old_body[16];
        memset(old_body, 0x5A, sizeof(old_body));
        assert(snapshot_write(SNAP, 1000, old_body, sizeof(old_body)) ==
               ERR_OK);

        pid_t pid = fork();
        assert(pid >= 0);

        if (pid == 0) {
            /* 크기를 바꿔 가며 덮어쓴다. 쓰는 시간이 들쭉날쭉해야 여러
               시점에 걸린다. */
            static uint8_t body[65536];
            for (uint64_t k = 1;; k++) {
                uint32_t len = (uint32_t)((k * 4099u) % 60000u) + 17u;
                memset(body, (int)(k & 0xFF), len);
                if (snapshot_write(SNAP, 2000 + k, body, len) != ERR_OK) {
                    _exit(1);
                }
                sh->rounds = (int32_t)k;
            }
        }

        /*
         * 앞 절반은 **첫 쓰기가 끝나기를 기다리지 않고** 죽인다. 그래야
         * "옛 스냅샷이 그대로 남는" 가지에 닿는다 — `rename`이 지키는 것이
         * 바로 그 가지다. 뒤 절반은 한 바퀴 돈 뒤에 죽여 "새것으로 통째로
         * 바뀌는" 가지를 본다.
         *
         * 한쪽만 보면 반대쪽 주장은 시험되지 않는다.
         */
        if (r >= ROUNDS / 2) {
            WAIT_UNTIL(sh->rounds >= 1);
        }
        for (int32_t i = 0; i < r; i++) {
            nap();
        }
        kill_and_reap(pid);

        /*
         * **죽은 뒤에 읽는다.** 죽이기 전에 읽으면 그 사이에 자식이 몇 번 더
         * 쓸 수 있어 아래 상한이 틀린다. 자식이 죽은 뒤의 값은 최종값이다.
         */
        int32_t rounds_done = sh->rounds;

        /* **원래 자리는 언제나 읽힌다.** */
        static uint8_t got[1 << 20];
        uint64_t       seq = 0;
        int            n = snapshot_read(SNAP, &seq, got, sizeof(got));
        if (n < 0) {
            fprintf(stderr, "  라운드 %d: 스냅샷이 깨졌다 (rc=%d)\n", r, n);
        }
        assert(n >= 0);

        /* 옛것이거나 자식이 쓴 것 중 하나다. 그 사이의 무엇도 아니다. */
        if (seq == 1000) {
            assert(n == (int)sizeof(old_body));
            assert(memcmp(got, old_body, sizeof(old_body)) == 0);
            kept_old++;
        } else {
            /*
             * `rename`이 `rounds` 올리기보다 먼저다. 그래서 마지막으로
             * 자리를 바꾼 판은 **마지막으로 센 판보다 많아야 하나 앞선다.**
             */
            assert(seq > 2000);
            assert(seq <= 2000 + (uint64_t)rounds_done + 1);
            /* 본문이 한 바이트로 채워져 있다 — 서로 다른 판이 섞이지 않았다 */
            assert(n > 0);
            for (int i = 1; i < n; i++) {
                assert(got[i] == got[0]);
            }
        }

        /*
         * **치워 주는 사람이 없다는 사실을 숨기지 않는다.**
         * SIGKILL은 정리할 틈을 주지 않으므로 `.tmp`가 남을 수 있다.
         * 남아도 원래 자리는 멀쩡하다 — 그것이 이 설계의 요점이다.
         */
        if (exists(SNAP ".tmp")) {
            tmp_left++;
        }

        share_free(sh);
        wipe();
    }

    fprintf(stderr,
            "  %d회 중 옛 스냅샷으로 남은 것 %d회, .tmp가 남은 것 %d회\n",
            ROUNDS, kept_old, tmp_left);
}

/* --- 3. 죽은 뒤 복구가 선다 --- */

/* 되살아나는 상태. */
typedef struct {
    uint64_t last_seq;
    int64_t  total;
} rebuilt_t;

static int rb_load(const uint8_t *data, uint32_t len, void *ctx)
{
    rebuilt_t *s = ctx;
    assert(len == sizeof(*s));
    memcpy(s, data, sizeof(*s));
    return 0;
}

static int rb_apply(const jrec_t *rec, void *ctx)
{
    rebuilt_t *s = ctx;
    assert(rec->len == 8);
    int64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (int64_t)(((uint64_t)v << 8) | rec->payload[i]);
    }
    s->total += v;
    s->last_seq = rec->seq;
    return 0;
}

/*
 * **죽인 뒤에 되살린 상태가 죽기 전과 같다.**
 *
 * 자식은 저널에 적고, 중간에 한 번 스냅샷을 남기고, 계속 적다가 죽는다.
 * 부모는 스냅샷 + 그 이후 저널로 되살려, 자식이 공유 자리에 남긴
 * "여기까지 적었다"와 맞춰 본다. 마지막으로 그 결과를 T5-03의 대사에 건다.
 *
 * 세 태스크(T5-01·02·03)가 각자 옳다고 주장한 것을 여기서 한 번에 건다.
 */
static void test_recovery_after_sigkill(void)
{
    wipe();
    shared_t *sh = share_new();

    const int32_t snap_at = 23;
    const int32_t want = 61;

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        journal_t *w = journal_create(JRN);
        if (w == NULL) {
            _exit(1);
        }
        rebuilt_t st = {0, 0};

        for (uint64_t i = 1;; i++) {
            int64_t v = (int64_t)(i * 7 + 3);
            uint8_t b[8];
            for (int k = 0; k < 8; k++) {
                b[k] = (uint8_t)((uint64_t)v >> (56 - 8 * k));
            }
            if (journal_append(w, 3, i, (int64_t)i, b, sizeof(b)) != ERR_OK) {
                _exit(1);
            }
            st.total += v;
            st.last_seq = i;

            if ((int32_t)i == snap_at) {
                /* 여기까지 반영한 상태를 남긴다 */
                if (snapshot_write(SNAP, i, (const uint8_t *)&st,
                                   sizeof(st)) != ERR_OK) {
                    _exit(1);
                }
                sh->rounds = 1;
            }
            /* **저널이 닿은 뒤에 올린다.** 스냅샷보다 나중이어도 된다 */
            sh->durable = (int32_t)i;

            if ((int32_t)i >= want) {
                nap();
            }
        }
    }

    WAIT_UNTIL(sh->rounds == 1 && sh->durable >= want);
    int32_t seen = sh->durable;
    kill_and_reap(pid);

    rebuilt_t        st = {0, 0};
    snap_apply_fn    fns = {rb_load, rb_apply, &st};
    recover_result_t res;
    assert(recover(SNAP, JRN, &fns, &res) == ERR_OK);

    assert(res.used_snapshot);
    assert(!res.snapshot_bad);
    assert(res.from_seq == (uint64_t)snap_at);

    /*
     * 죽인 뒤에 자식이 몇 건 더 적었을 수 있으므로 `>=`로 본다.
     * 되살린 합은 그 수까지의 합과 **정확히** 같아야 한다 —
     * 하나도 빠지지 않고, 스냅샷에 든 것을 두 번 세지도 않았다.
     */
    assert(st.last_seq >= (uint64_t)seen);

    int64_t recomputed = 0;
    for (uint64_t i = 1; i <= st.last_seq; i++) {
        recomputed += (int64_t)(i * 7 + 3);
    }
    assert(st.total == recomputed);

    /* 스냅샷 이후만 재생했다 */
    assert(res.replayed == (int)(st.last_seq - (uint64_t)snap_at));

    fprintf(stderr, "  약속 %d건, 되살린 %llu건, 스냅샷 이후 재생 %d건%s\n",
            seen, (unsigned long long)st.last_seq, res.replayed,
            res.journal_torn ? " (꼬리 찢어짐)" : "");

    /*
     * **되살린 상태가 대사를 통과한다.** 되살렸다는 것만으로는 부족하다 —
     * 되살린 것이 앞뒤가 맞는지는 따로 물어야 한다.
     */
    recon_account_t a;
    memset(&a, 0, sizeof(a));
    strncpy(a.account_no, "31940771", sizeof(a.account_no) - 1);
    a.cash_start = 0;
    a.deposits = st.total; /* 되살린 합을 들어온 돈으로 본다 */
    a.cash_now = st.total;
    a.reserved_now = 0;
    a.open_reserved = 0;

    recon_finding_t buf[8];
    recon_report_t  rep;
    recon_report_init(&rep, buf, 8);
    assert(recon_account(&a, &rep) == ERR_OK);
    assert(recon_clean(&rep));

    share_free(sh);
    wipe();
}

int main(void)
{
    STEP(test_fsynced_records_survive_sigkill);
    STEP(test_torn_tail_from_real_kill);
    STEP(test_snapshot_never_half_written);
    STEP(test_recovery_after_sigkill);
    return 0;
}
