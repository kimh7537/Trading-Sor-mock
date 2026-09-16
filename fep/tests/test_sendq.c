/*
 * T3-10 FEP 공통 — 송신 큐와 EAGAIN 처리.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. **실제로 부분 쓰기를 만든다.** 소켓 버퍼를 작게 잡고 큰 덩어리를 보낸다
 *  2. 남은 것을 이어 보내면 전부, 순서대로 도착한다
 *  3. 자리가 모자라면 **전문을 반만 넣지 않고 통째로 거절한다**
 *  4. `EV_WRITE`는 보낼 것이 있을 때만 켠다
 *  5. 상대가 끊긴 것(EPIPE)과 자리가 없는 것(EAGAIN)을 구분한다
 *
 * 1번이 이 태스크의 핵심이다. 전문이 70바이트인 동안에는 `write`가 늘 한 번에
 * 다 쓰므로 **부분 쓰기 경로가 한 번도 실행되지 않는다.** 그 경로가 T3-03
 * 이후로 시험되지 못한 채 남아 있었다 — 여기서 처음으로 관측 가능해진다.
 */
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "errors.h"
#include "evloop.h"
#include "sendq.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

/*
 * `sendq_t`가 256KB라 스택에 두지 않는다. 테스트끼리 섞이지 않도록 매번
 * `sendq_init()`으로 되돌린다.
 */
static sendq_t g_q;

/* --- 도구 --- */

/*
 * 양끝이 있는 소켓 한 쌍. `small`이면 커널 버퍼를 최소로 줄여 **부분 쓰기가
 * 반드시 일어나게** 만든다. 리눅스는 준 값을 두 배로 잡고 하한이 있으므로
 * 정확한 크기에 기대지 않고 "충분히 작다"에만 기댄다.
 */
static void make_pair(int *wr, int *rd, bool small)
{
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    if (small) {
        int sz = 4096;
        (void)setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
        (void)setsockopt(sv[1], SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
    }

    assert(evloop_set_nonblocking(sv[0]) == ERR_OK);
    assert(evloop_set_nonblocking(sv[1]) == ERR_OK);
    *wr = sv[0];
    *rd = sv[1];
}

/* 251은 소수라 어떤 2의 거듭제곱 경계와도 어긋난다 — 밀린 바이트가 눈에 띈다. */
static uint8_t pat(size_t i)
{
    return (uint8_t)(i % 251u);
}

static void fill(uint8_t *p, size_t n, size_t base)
{
    for (size_t i = 0; i < n; i++) {
        p[i] = pat(base + i);
    }
}

/* 읽을 수 있는 만큼 읽어 `out`의 `at` 뒤에 잇는다. 읽은 바이트 수를 돌려준다. */
static size_t drain(int fd, uint8_t *out, size_t cap, size_t at)
{
    size_t got = 0;
    for (;;) {
        if (at + got >= cap) {
            break;
        }
        ssize_t r = read(fd, out + at + got, cap - at - got);
        if (r > 0) {
            got += (size_t)r;
            continue;
        }
        break; /* 0(EOF)이거나 EAGAIN */
    }
    return got;
}

/* --- 1·2. 부분 쓰기와 이어 보내기 --- */

static void test_partial_write(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, true);

    sendq_init(&g_q);

    /*
     * 커널 송신 버퍼(수 KB)보다 훨씬 큰 덩어리다. 한 번의 `write`로 다 나갈 수
     * 없다.
     */
    static uint8_t src[200000];
    fill(src, sizeof(src), 0);
    assert(sendq_push(&g_q, src, sizeof(src)) == ERR_OK);
    assert(sendq_pending(&g_q) == sizeof(src));

    /* **첫 flush는 반드시 일부만 보낸다.** 이것이 이 테스트의 전부다. */
    int first = sendq_flush(&g_q, wr);
    assert(first > 0);
    assert((size_t)first < sizeof(src));
    assert(sendq_pending(&g_q) == sizeof(src) - (size_t)first);

    /* 받는 쪽을 비워 주며 이어 보낸다. */
    static uint8_t got[200000];
    size_t         at = 0;
    int            rounds = 0;

    while (sendq_pending(&g_q) > 0) {
        at += drain(rd, got, sizeof(got), at);
        int w = sendq_flush(&g_q, wr);
        assert(w >= 0); /* 0(EAGAIN)도 정상이다 */
        rounds++;
        assert(rounds < 10000); /* 안 줄어들면 여기서 멈춘다 */
    }
    at += drain(rd, got, sizeof(got), at);

    assert(at == sizeof(src));
    assert(memcmp(got, src, sizeof(src)) == 0); /* 순서도 내용도 그대로 */

    /*
     * 한 번에 다 나갔다면 이 테스트는 아무것도 확인하지 못한 것이다.
     * 실제로 여러 번에 걸쳐 나갔는지 여기서 못 박는다.
     */
    assert(rounds > 1);

    close(wr);
    close(rd);
}

/* 한 번에 다 나가는 보통의 경우도 확인한다. */
static void test_small_frame_goes_at_once(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, false);

    sendq_init(&g_q);

    uint8_t frame[70];
    fill(frame, sizeof(frame), 7);

    assert(sendq_push(&g_q, frame, sizeof(frame)) == ERR_OK);
    assert(sendq_flush(&g_q, wr) == (int)sizeof(frame));
    assert(sendq_pending(&g_q) == 0);

    uint8_t got[70];
    memset(got, 0, sizeof(got));
    assert(drain(rd, got, sizeof(got), 0) == sizeof(frame));
    assert(memcmp(got, frame, sizeof(frame)) == 0);

    close(wr);
    close(rd);
}

/* --- 3. 전부 아니면 전무 --- */

static void test_all_or_nothing(void)
{
    sendq_init(&g_q);

    /* 자리를 100바이트만 남기고 채운다. */
    static uint8_t big[SENDQ_CAP - 100];
    fill(big, sizeof(big), 0);
    assert(sendq_push(&g_q, big, sizeof(big)) == ERR_OK);
    assert(sendq_pending(&g_q) == sizeof(big));

    /* 100바이트는 들어가고 101바이트는 안 들어간다. */
    uint8_t frame[101];
    fill(frame, sizeof(frame), 1000);

    assert(sendq_push(&g_q, frame, 101) == ERR_POOL_EXHAUSTED);
    /* **아무것도 넣지 않았다.** 반쯤 들어갔으면 여기서 걸린다. */
    assert(sendq_pending(&g_q) == sizeof(big));

    assert(sendq_push(&g_q, frame, 100) == ERR_OK);
    assert(sendq_pending(&g_q) == SENDQ_CAP);

    /* 앞서 넣은 것이 덮이지 않았다. */
    assert(memcmp(g_q.buf, big, sizeof(big)) == 0);
    assert(memcmp(g_q.buf + sizeof(big), frame, 100) == 0);
}

/* --- 4. EV_WRITE 켜짐·꺼짐 --- */

static void test_want_write(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, false);

    sendq_init(&g_q);

    /* 보낼 것이 없으면 켜지 않는다 — 켜 두면 루프가 헛돈다. */
    assert(!sendq_want_write(&g_q));

    uint8_t frame[64];
    fill(frame, sizeof(frame), 3);
    assert(sendq_push(&g_q, frame, sizeof(frame)) == ERR_OK);
    assert(sendq_want_write(&g_q));

    assert(sendq_flush(&g_q, wr) == (int)sizeof(frame));
    assert(!sendq_want_write(&g_q)); /* 다 나갔으면 내린다 */

    close(wr);
    close(rd);
}

/* 다 못 나갔으면 켜진 채로 둔다. */
static void test_want_write_stays_on_partial(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, true);

    sendq_init(&g_q);

    static uint8_t src[200000];
    fill(src, sizeof(src), 0);
    assert(sendq_push(&g_q, src, sizeof(src)) == ERR_OK);

    int w = sendq_flush(&g_q, wr);
    assert(w > 0 && (size_t)w < sizeof(src));
    assert(sendq_want_write(&g_q));

    close(wr);
    close(rd);
}

/* --- 5. 상대가 끊긴 것과 자리가 없는 것 --- */

static void test_peer_closed(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, false);

    sendq_init(&g_q);

    uint8_t frame[64];
    fill(frame, sizeof(frame), 5);
    assert(sendq_push(&g_q, frame, sizeof(frame)) == ERR_OK);

    close(rd); /* 상대가 사라졌다 */

    /*
     * EPIPE는 ERR_IO다. **EAGAIN처럼 0을 돌려주면 안 된다** — 그러면 호출부가
     * 끊긴 접속에 영원히 재시도한다.
     */
    assert(sendq_flush(&g_q, wr) == ERR_IO);

    close(wr);
}

static void test_eagain_is_not_error(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, true);

    sendq_init(&g_q);

    static uint8_t src[200000];
    fill(src, sizeof(src), 0);
    assert(sendq_push(&g_q, src, sizeof(src)) == ERR_OK);

    /*
     * 받는 쪽을 비우지 않고 계속 보낸다. 커널 버퍼가 차면 EAGAIN이 나는데,
     * **그때 0이 나와야 하고 음수가 나오면 안 된다.**
     */
    bool saw_zero = false;
    for (int i = 0; i < 100; i++) {
        int w = sendq_flush(&g_q, wr);
        assert(w >= 0);
        if (w == 0) {
            saw_zero = true;
            break;
        }
    }
    assert(saw_zero);
    assert(sendq_pending(&g_q) > 0); /* 큐는 그대로 남아 있다 */

    close(wr);
    close(rd);
}

/* --- 버퍼 관리 --- */

static void test_flush_empty(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, false);

    sendq_init(&g_q);
    assert(sendq_flush(&g_q, wr) == 0); /* 보낼 게 없는 것은 에러가 아니다 */
    assert(sendq_pending(&g_q) == 0);

    close(wr);
    close(rd);
}

/*
 * 같은 큐로 오래 쓴다. 되돌리기를 빼먹으면 `off`만 앞으로 가다가 찬다.
 */
static void test_long_run(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, false);

    sendq_init(&g_q);

    uint8_t frame[70];
    uint8_t got[70];

    for (int i = 0; i < 20000; i++) {
        fill(frame, sizeof(frame), (size_t)i);
        assert(sendq_push(&g_q, frame, sizeof(frame)) == ERR_OK);
        assert(sendq_flush(&g_q, wr) == (int)sizeof(frame));
        assert(sendq_pending(&g_q) == 0);

        memset(got, 0, sizeof(got));
        assert(drain(rd, got, sizeof(got), 0) == sizeof(frame));
        assert(memcmp(got, frame, sizeof(frame)) == 0);
    }

    close(wr);
    close(rd);
}

/*
 * 보내고 난 뒤 큐가 모자라면 **앞의 빈자리를 쓴다.**
 * 되돌리지 않으면 자리가 있는데도 거절한다.
 */
static void test_compaction(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, false);

    sendq_init(&g_q);

    /* 전문 하나를 넣어 보낸다. off가 1000까지 밀린다. */
    uint8_t first[1000];
    fill(first, sizeof(first), 0);
    assert(sendq_push(&g_q, first, sizeof(first)) == ERR_OK);
    assert(sendq_flush(&g_q, wr) == (int)sizeof(first));
    assert(sendq_pending(&g_q) == 0);

    uint8_t small[10];
    fill(small, sizeof(small), 1);
    assert(sendq_push(&g_q, small, sizeof(small)) == ERR_OK);

    /* 남은 10바이트를 앞으로 되돌리면 딱 들어간다. */
    static uint8_t huge[SENDQ_CAP - 10];
    fill(huge, sizeof(huge), 2);
    assert(sendq_push(&g_q, huge, sizeof(huge)) == ERR_OK);
    assert(sendq_pending(&g_q) == SENDQ_CAP);

    close(wr);
    close(rd);
}

/* --- 인자 --- */

static void test_args(void)
{
    int wr;
    int rd;
    make_pair(&wr, &rd, false);

    sendq_init(&g_q);

    uint8_t one = 0;

    assert(sendq_push(NULL, &one, 1) == ERR_NULL_PTR);
    assert(sendq_push(&g_q, NULL, 1) == ERR_NULL_PTR);
    assert(sendq_push(&g_q, &one, 0) == ERR_INVALID_ARG);
    assert(sendq_push(&g_q, &one, SENDQ_CAP + 1) == ERR_INVALID_ARG);

    assert(sendq_flush(NULL, wr) == ERR_NULL_PTR);
    assert(sendq_flush(&g_q, -1) == ERR_INVALID_ARG);

    assert(sendq_pending(NULL) == 0);
    assert(!sendq_want_write(NULL));

    sendq_init(NULL); /* 죽지 않는다 */

    close(wr);
    close(rd);
}

int main(void)
{
    /*
     * SIGPIPE로 죽지 않게 한다. 운영에서는 `evloop_install_signals()`가 한다 —
     * 여기서는 그 한 가지만 필요하므로 직접 건다.
     */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    assert(sigaction(SIGPIPE, &sa, NULL) == 0);

    STEP(test_partial_write);
    STEP(test_small_frame_goes_at_once);
    STEP(test_all_or_nothing);
    STEP(test_want_write);
    STEP(test_want_write_stays_on_partial);
    STEP(test_peer_closed);
    STEP(test_eagain_is_not_error);
    STEP(test_flush_empty);
    STEP(test_long_run);
    STEP(test_compaction);
    STEP(test_args);
    return 0;
}
