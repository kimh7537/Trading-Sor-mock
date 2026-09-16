/*
 * T3-06 계좌 잔고 관리 (계좌 단위 락).
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 계좌가 공유 메모리에 있고 워커가 같은 계좌를 본다
 *  2. 락이 **계좌 단위**다 — 다른 계좌를 만지면 기다리지 않는다
 *  3. 락이 **프로세스 사이에서** 동작한다
 *  4. **락을 쥔 채 죽어도 회수된다** (PTHREAD_MUTEX_ROBUST + 되돌리기)
 *  5. 불변조건 `0 <= reserved <= cash`. 어기면 거절하고 아무것도 안 바꾼다
 *  6. 묶기·풀기·정산이 원자적이다
 *
 * 4번이 이 태스크에서 가장 말할 만한 부분이라 **진짜로 죽여서** 확인한다.
 * 자식이 락을 쥐고 쓰기 표시를 세운 뒤 `_exit`으로 사라지면, 부모가 잠글 때
 * `EOWNERDEAD`를 받고 직전 값으로 되돌려야 한다.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "account.h"
#include "errors.h"

#define STEP(fn)                                                            \
    do {                                                                   \
        fprintf(stderr, "[%s]\n", #fn);                                    \
        fn();                                                              \
    } while (0)

#define ACCT_CAP 8
#define ORDER_CAP 4

typedef struct {
    shm_segment_t  *seg;
    account_store_t store;
} fixture_t;

static void fx_init(fixture_t *fx)
{
    int32_t counts[SHM_REGION_COUNT] = {ACCT_CAP, ORDER_CAP};
    size_t  sizes[SHM_REGION_COUNT] = {sizeof(account_t), 64};

    fx->seg = shm_create(counts, sizes);
    assert(fx->seg != NULL);
    assert(acct_store_init(&fx->store, fx->seg) == ERR_OK);
}

static void fx_free(fixture_t *fx)
{
    acct_store_destroy(&fx->store);
    shm_destroy(fx->seg);
}

/* --- 1. 계좌 열기 --- */

static void test_open_and_find(void)
{
    fixture_t fx;
    fx_init(&fx);

    assert(acct_capacity(&fx.store) == ACCT_CAP);

    int a = acct_open(&fx.store, "ACC-001");
    assert(a >= 0);

    /* 같은 번호는 같은 자리다 — 두 번 열어도 계좌가 둘이 되지 않는다. */
    assert(acct_open(&fx.store, "ACC-001") == a);
    assert(acct_find(&fx.store, "ACC-001") == a);

    int b = acct_open(&fx.store, "ACC-002");
    assert(b >= 0 && b != a);

    assert(acct_find(&fx.store, "ACC-999") == ERR_NOT_FOUND);

    /* 새 계좌는 비어 있다. */
    int64_t cash = -1;
    int64_t reserved = -1;
    assert(acct_snapshot(&fx.store, a, &cash, &reserved) == ERR_OK);
    assert(cash == 0 && reserved == 0);
    assert(acct_available(&fx.store, a) == 0);

    /* 자리를 다 쓰면 더 못 연다. */
    for (int i = 2; i < ACCT_CAP; i++) {
        char no[ACCT_NO_LEN + 1];
        snprintf(no, sizeof(no), "ACC-%03d", i + 1);
        assert(acct_open(&fx.store, no) >= 0);
    }
    assert(acct_open(&fx.store, "OVERFLOW") == ERR_POOL_EXHAUSTED);

    /* 인자 검사. */
    assert(acct_open(NULL, "X") == ERR_NULL_PTR);
    assert(acct_open(&fx.store, NULL) == ERR_NULL_PTR);
    assert(acct_open(&fx.store, "") == ERR_INVALID_ARG);
    assert(acct_find(NULL, "X") == ERR_NULL_PTR);
    assert(acct_at(&fx.store, -1) == NULL);
    assert(acct_at(&fx.store, ACCT_CAP) == NULL);
    assert(acct_capacity(NULL) == 0);

    fx_free(&fx);
}

/* --- 2. 잔고 불변조건 --- */

/*
 * 완료 조건 5. **어기는 연산은 거절하고 아무것도 바꾸지 않는다.**
 *
 * 거절 뒤에 잔고가 그대로인지까지 본다 — 절반만 반영하고 에러를 돌려주는
 * 구현도 "거절했다"고 주장할 수 있기 때문이다.
 */
static void test_invariants(void)
{
    fixture_t fx;
    fx_init(&fx);

    int a = acct_open(&fx.store, "ACC-001");
    assert(a >= 0);

    assert(acct_deposit(&fx.store, a, 1000000) == ERR_OK);
    assert(acct_available(&fx.store, a) == 1000000);

    /* 쓸 수 있는 돈보다 많이 묶을 수 없다. */
    assert(acct_reserve(&fx.store, a, 1000001) == ERR_INVALID_QTY);
    assert(acct_available(&fx.store, a) == 1000000); /* 그대로다 */

    assert(acct_reserve(&fx.store, a, 300000) == ERR_OK);
    assert(acct_available(&fx.store, a) == 700000);

    /* 묶인 것보다 많이 풀 수 없다. */
    assert(acct_release(&fx.store, a, 300001) == ERR_INVALID_QTY);
    assert(acct_available(&fx.store, a) == 700000);

    /* 묶인 돈은 출금할 수 없다 — reserved <= cash가 막는다. */
    assert(acct_withdraw(&fx.store, a, 800000) == ERR_INVALID_QTY);
    assert(acct_withdraw(&fx.store, a, 700000) == ERR_OK);

    int64_t cash = 0;
    int64_t reserved = 0;
    assert(acct_snapshot(&fx.store, a, &cash, &reserved) == ERR_OK);
    assert(cash == 300000 && reserved == 300000);
    assert(acct_available(&fx.store, a) == 0);

    /*
     * **직전 값(pre-image)이 적혀 있다.**
     *
     * 이것이 죽은 주인을 되돌리는 근거다. 바꾸기 전 값을 남기지 않으면 회수할
     * 때 되돌릴 곳이 없다. 연산이 끝난 뒤에도 남아 있으므로 밖에서 볼 수 있다.
     *
     * (`mutating` 표시가 서는 순간은 `begin_write`와 `end_write` 사이 몇
     *  명령어뿐이라 밖에서 관측할 방법이 없다. 그 반쪽은 죽은 주인 테스트가
     *  표시를 직접 세워 대신 확인한다.)
     */
    account_t *rec = acct_at(&fx.store, a);
    assert(rec != NULL);
    assert(rec->mutating == 0); /* 연산이 끝났으면 내려가 있다 */
    int64_t before_cash = rec->cash;
    int64_t before_reserved = rec->reserved;
    assert(acct_deposit(&fx.store, a, 1) == ERR_OK);
    assert(rec->pre_cash == before_cash);
    assert(rec->pre_reserved == before_reserved);
    assert(rec->cash == before_cash + 1);
    assert(acct_withdraw(&fx.store, a, 1) == ERR_OK);

    /* 정산 — 묶인 것을 풀면서 예수금에서도 뺀다. */
    assert(acct_settle(&fx.store, a, 300001) == ERR_INVALID_QTY);
    assert(acct_settle(&fx.store, a, 300000) == ERR_OK);
    assert(acct_snapshot(&fx.store, a, &cash, &reserved) == ERR_OK);
    assert(cash == 0 && reserved == 0);

    /* 0과 음수는 받지 않는다. */
    assert(acct_deposit(&fx.store, a, 0) == ERR_INVALID_QTY);
    assert(acct_deposit(&fx.store, a, -1) == ERR_INVALID_QTY);
    assert(acct_reserve(&fx.store, a, 0) == ERR_INVALID_QTY);
    assert(acct_withdraw(&fx.store, a, -5) == ERR_INVALID_QTY);

    /* 없는 계좌. */
    assert(acct_deposit(&fx.store, 99, 100) == ERR_NULL_PTR);
    assert(acct_deposit(NULL, 0, 100) == ERR_NULL_PTR);

    /* 열지 않은 자리는 계좌가 아니다. */
    int unopened = ACCT_CAP - 1;
    assert(acct_deposit(&fx.store, unopened, 100) == ERR_NOT_FOUND);
    assert(acct_snapshot(&fx.store, unopened, &cash, &reserved) ==
           ERR_NOT_FOUND);

    fx_free(&fx);
}

/* --- 3. 프로세스 사이 --- */

/*
 * 완료 조건 1·3. 자식이 갱신한 잔고를 부모가 본다.
 *
 * `PTHREAD_PROCESS_SHARED`를 빼도 이 검사는 **바로 깨지지 않는다** — 경합이
 * 없으면 락이 동작하지 않아도 값은 맞게 나온다. 그래서 다음 테스트에서 경합을
 * 실제로 만든다.
 */
static void test_visible_across_processes(void)
{
    fixture_t fx;
    fx_init(&fx);

    int a = acct_open(&fx.store, "ACC-001");
    assert(a >= 0);
    assert(acct_deposit(&fx.store, a, 500000) == ERR_OK);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        if (acct_reserve(&fx.store, a, 200000) != ERR_OK) {
            _exit(1);
        }
        if (acct_deposit(&fx.store, a, 100000) != ERR_OK) {
            _exit(2);
        }
        _exit(0);
    }

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    int64_t cash = 0;
    int64_t reserved = 0;
    assert(acct_snapshot(&fx.store, a, &cash, &reserved) == ERR_OK);
    assert(cash == 600000);
    assert(reserved == 200000);
    assert(acct_available(&fx.store, a) == 400000);

    fx_free(&fx);
}

/*
 * 완료 조건 3의 진짜 검사 — **경합을 만든다.**
 *
 * 자식 넷이 같은 계좌에 각각 1000번씩 1원을 입금한다. 락이 프로세스 사이에서
 * 동작하지 않으면 읽고-더하고-쓰기가 겹쳐 **합계가 모자란다.**
 * 정확히 4000이 나와야 한다.
 */
static void test_cross_process_lock_serializes(void)
{
    fixture_t fx;
    fx_init(&fx);

    int a = acct_open(&fx.store, "ACC-RACE");
    assert(a >= 0);

    const int KIDS = 4;
    const int ROUNDS = 1000;

    for (int k = 0; k < KIDS; k++) {
        pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            for (int i = 0; i < ROUNDS; i++) {
                if (acct_deposit(&fx.store, a, 1) != ERR_OK) {
                    _exit(1);
                }
            }
            _exit(0);
        }
    }

    for (int k = 0; k < KIDS; k++) {
        int status = 0;
        assert(wait(&status) > 0);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    int64_t cash = 0;
    assert(acct_snapshot(&fx.store, a, &cash, NULL) == ERR_OK);
    assert(cash == (int64_t)KIDS * ROUNDS);

    fx_free(&fx);
}

/*
 * **락이 실제로 막아 세우는가.**
 *
 * 앞의 경합 시험만으로는 `PTHREAD_PROCESS_SHARED`를 빼도 잡히지 않는다.
 * 임계 구역이 몇 나노초라 거의 모든 획득이 **경합 없는 빠른 경로**로 끝나고,
 * 그 경로는 프로세스 공유 속성과 무관하게 원자적 연산만 쓰기 때문이다.
 *
 * 그래서 **기다릴 수밖에 없는 상황**을 만든다. 자식이 계좌를 잠근 채 한참
 * 머무르는 동안 부모가 같은 계좌를 잠그려 한다. 부모는 자식이 풀 때까지
 * 들어가지 못해야 한다.
 *
 * 시간을 재지 않고 **순서**로 확인한다 — 자식은 락을 풀기 직전에 표식을 남기고,
 * 부모는 락을 잡은 뒤 그 표식이 있는지 본다. 상호 배제가 깨지면 부모가 먼저
 * 들어가 표식을 못 본다.
 */
static void test_lock_blocks_across_processes(void)
{
    fixture_t fx;
    fx_init(&fx);

    int a = acct_open(&fx.store, "ACC-WAIT");
    assert(a >= 0);

    account_t *rec = acct_at(&fx.store, a);
    assert(rec != NULL);
    rec->version = 0;

    int held[2];
    assert(pipe(held) == 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(held[0]);

        if (acct_lock(&fx.store, a, NULL) != ERR_OK) {
            _exit(1);
        }

        uint8_t one = 1;
        if (write(held[1], &one, 1) != 1) {
            _exit(2);
        }

        /* 부모가 잠그려 시도하도록 충분히 머무른다. */
        struct timespec ts = {0, 250 * 1000 * 1000}; /* 250ms */
        nanosleep(&ts, NULL);

        /* 풀기 직전에 표식을 남긴다. */
        rec->version = 4242;
        acct_unlock(&fx.store, a);
        _exit(0);
    }

    close(held[1]);

    uint8_t one = 0;
    assert(read(held[0], &one, 1) == 1);

    /* 자식이 쥐고 있다. 여기서 막혀야 한다. */
    assert(acct_lock(&fx.store, a, NULL) == ERR_OK);

    /*
     * 자식이 풀기 직전에 남긴 표식이 보여야 한다. 상호 배제가 동작하지 않아
     * 부모가 먼저 들어갔다면 아직 0이다.
     */
    assert(rec->version == 4242);
    assert(acct_unlock(&fx.store, a) == ERR_OK);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    close(held[0]);
    fx_free(&fx);
}

/* --- 4. 계좌 단위 락 --- */

/*
 * 완료 조건 2. **다른 계좌를 만지면 기다리지 않는다.**
 *
 * 자식이 계좌 A를 잠근 채 부모의 신호를 기다린다. 그동안 부모는 계좌 B를
 * 잠그고 갱신할 수 있어야 한다 — 저장소 전체에 락 하나였다면 여기서 멈춘다.
 */
static void test_lock_is_per_account(void)
{
    fixture_t fx;
    fx_init(&fx);

    int a = acct_open(&fx.store, "ACC-A");
    int b = acct_open(&fx.store, "ACC-B");
    assert(a >= 0 && b >= 0);

    int held[2];
    int go[2];
    assert(pipe(held) == 0);
    assert(pipe(go) == 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(held[0]);
        close(go[1]);

        if (acct_lock(&fx.store, a, NULL) != ERR_OK) {
            _exit(1);
        }
        uint8_t one = 1;
        if (write(held[1], &one, 1) != 1) {
            _exit(2);
        }
        /* 부모가 B를 끝낼 때까지 A를 쥐고 있는다. */
        uint8_t ack = 0;
        if (read(go[0], &ack, 1) != 1) {
            _exit(3);
        }
        acct_unlock(&fx.store, a);
        _exit(0);
    }

    close(held[1]);
    close(go[0]);

    /* 자식이 A를 잠갔다. */
    uint8_t one = 0;
    assert(read(held[0], &one, 1) == 1);

    /* A가 잠긴 동안 B는 자유롭다. 여기서 멈추면 락이 계좌 단위가 아니다. */
    assert(acct_deposit(&fx.store, b, 12345) == ERR_OK);
    assert(acct_available(&fx.store, b) == 12345);

    uint8_t ack = 1;
    assert(write(go[1], &ack, 1) == 1);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    close(held[0]);
    close(go[1]);
    fx_free(&fx);
}

/* --- 5. 락을 쥔 채 죽는 경우 --- */

/*
 * 완료 조건 4. **이 태스크의 핵심이다.**
 *
 * 자식이 계좌를 잠그고, 쓰기 표시를 세우고, 값을 반쯤 바꾼 뒤 `_exit`으로
 * 사라진다. 락은 풀리지 않는다.
 *
 * 부모가 그다음 잠그면 `EOWNERDEAD`를 받아야 하고, 쓰기 표시를 보고 **직전
 * 값으로 되돌려야** 한다. 되돌리지 않으면 "증거금은 묶였는데 주문은 없는"
 * 계좌가 남는다.
 *
 * `PTHREAD_MUTEX_ROBUST`가 없으면 부모가 그냥 **영원히 멈춘다** — 이 테스트가
 * 실패가 아니라 멈춤으로 나타나는 이유다.
 */
static void test_recovers_from_dead_owner(void)
{
    fixture_t fx;
    fx_init(&fx);

    int a = acct_open(&fx.store, "ACC-DEAD");
    assert(a >= 0);
    assert(acct_deposit(&fx.store, a, 1000000) == ERR_OK);
    assert(acct_reserve(&fx.store, a, 200000) == ERR_OK);

    int held[2];
    assert(pipe(held) == 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(held[0]);

        if (acct_lock(&fx.store, a, NULL) != ERR_OK) {
            _exit(1);
        }

        /*
         * 갱신을 시작하고 **끝내지 않는다.** 직전 값을 적어 두고 값을 바꾼 뒤
         * 표시를 내리기 전에 죽는다 — 워커가 한창 일하다 죽는 모습이다.
         */
        account_t *rec = acct_at(&fx.store, a);
        if (rec == NULL) {
            _exit(2);
        }
        rec->pre_cash = rec->cash;
        rec->pre_reserved = rec->reserved;
        rec->mutating = 1;
        rec->cash = 999; /* 말이 안 되는 값으로 바꿔 둔다 */
        rec->reserved = 888;

        uint8_t one = 1;
        if (write(held[1], &one, 1) != 1) {
            _exit(3);
        }

        /* 락을 쥔 채로 사라진다. */
        _exit(0);
    }

    close(held[1]);

    uint8_t one = 0;
    assert(read(held[0], &one, 1) == 1);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);

    /*
     * 자식은 죽었고 락은 그대로다. 잠그면 EOWNERDEAD를 받아 되돌려야 한다.
     * 여기서 멈추면 robust 속성이 꺼져 있다는 뜻이다.
     */
    bool recovered = false;
    assert(acct_lock(&fx.store, a, &recovered) == ERR_OK);
    assert(recovered);

    account_t *rec = acct_at(&fx.store, a);
    assert(rec != NULL);
    assert(rec->mutating == 0);
    assert(rec->cash == 1000000); /* 되돌아왔다 */
    assert(rec->reserved == 200000);

    assert(acct_unlock(&fx.store, a) == ERR_OK);

    /* 회수 뒤에도 계좌가 멀쩡히 동작한다 — 락이 못 쓰게 되지 않았다. */
    assert(acct_deposit(&fx.store, a, 1) == ERR_OK);
    assert(acct_available(&fx.store, a) == 800001);

    /* 다시 잠가도 회수 표시가 서지 않는다 — 한 번으로 끝났다. */
    bool again = true;
    assert(acct_lock(&fx.store, a, &again) == ERR_OK);
    assert(!again);
    assert(acct_unlock(&fx.store, a) == ERR_OK);

    close(held[0]);
    fx_free(&fx);
}

/*
 * 쓰기 중이 아닐 때 죽으면 **되돌릴 것이 없다.**
 *
 * 락만 쥐고 있다가 죽은 경우다. 이때 값을 건드리면 멀쩡한 잔고를 망친다 —
 * 회수가 곧 되돌리기는 아니라는 것을 못 박는다.
 */
static void test_dead_owner_without_write(void)
{
    fixture_t fx;
    fx_init(&fx);

    int a = acct_open(&fx.store, "ACC-IDLE");
    assert(a >= 0);
    assert(acct_deposit(&fx.store, a, 777) == ERR_OK);

    int held[2];
    assert(pipe(held) == 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(held[0]);
        if (acct_lock(&fx.store, a, NULL) != ERR_OK) {
            _exit(1);
        }
        uint8_t one = 1;
        if (write(held[1], &one, 1) != 1) {
            _exit(2);
        }
        _exit(0); /* 값은 건드리지 않고 쥔 채로 죽는다 */
    }

    close(held[1]);
    uint8_t one = 0;
    assert(read(held[0], &one, 1) == 1);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);

    bool recovered = true;
    assert(acct_lock(&fx.store, a, &recovered) == ERR_OK);
    assert(!recovered); /* 되돌릴 것이 없었다 */
    assert(acct_unlock(&fx.store, a) == ERR_OK);

    /* 잔고가 그대로다. */
    assert(acct_available(&fx.store, a) == 777);

    close(held[0]);
    fx_free(&fx);
}

/* --- 6. 저장소 인자 --- */

static void test_store_args(void)
{
    account_store_t store;
    memset(&store, 0, sizeof(store));

    assert(acct_store_init(NULL, NULL) == ERR_NULL_PTR);
    assert(acct_store_init(&store, NULL) == ERR_NULL_PTR);

    /* 레코드가 계좌를 담기에 좁으면 거절한다 — 이웃을 밟느니 안 여는 게 낫다. */
    int32_t        counts[SHM_REGION_COUNT] = {4, 4};
    size_t         tiny[SHM_REGION_COUNT] = {8, 8};
    shm_segment_t *seg = shm_create(counts, tiny);
    assert(seg != NULL);
    assert(acct_store_init(&store, seg) == ERR_INVALID_ARG);
    shm_destroy(seg);

    acct_store_destroy(NULL);
    assert(acct_lock(NULL, 0, NULL) == ERR_NULL_PTR);
    assert(acct_unlock(NULL, 0) == ERR_NULL_PTR);
    assert(acct_snapshot(NULL, 0, NULL, NULL) == ERR_NULL_PTR);
    assert(acct_available(NULL, 0) == 0);
}

int main(void)
{
    STEP(test_open_and_find);
    STEP(test_invariants);
    STEP(test_visible_across_processes);
    STEP(test_cross_process_lock_serializes);
    STEP(test_lock_blocks_across_processes);
    STEP(test_lock_is_per_account);
    STEP(test_recovers_from_dead_owner);
    STEP(test_dead_owner_without_write);
    STEP(test_store_args);
    return 0;
}
