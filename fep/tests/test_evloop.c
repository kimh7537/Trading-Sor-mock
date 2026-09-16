/*
 * T3-08 FEP 공통 — epoll 이벤트 루프.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 여러 fd를 **동시에** 감시하고 fd마다 콜백이 불린다
 *  2. 레벨 트리거다 — **덜 읽어도 다음에 또 알려 준다**
 *  3. 등록·변경·해제가 되고, 해제한 fd는 콜백을 받지 않는다
 *  4. 블로킹 fd는 등록을 거절한다
 *  5. **콜백 안에서 fd를 닫아도 루프가 깨지지 않는다**
 *  6. 멈춤 신호에 빠져나온다
 *
 * 2번과 5번이 이 태스크의 핵심이다. 둘 다 "돌아는 가는데 가끔 틀리는" 종류라
 * 일부러 그 상황을 만들어 확인한다.
 *
 * `socketpair`를 쓴다 — 양쪽 끝이 다 있어서 한쪽에 쓰고 다른 쪽에서 읽는
 * 상황을 프로세스 하나로 만들 수 있다.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "errors.h"
#include "evloop.h"

#define STEP(fn)                                                            \
    do {                                                                   \
        fprintf(stderr, "[%s]\n", #fn);                                    \
        fn();                                                              \
    } while (0)

/* --- 도구 --- */

/* 양끝이 있는 소켓 한 쌍. 읽는 쪽은 논블로킹으로 만든다. */
static void make_pair(int *rd, int *wr)
{
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(evloop_set_nonblocking(sv[0]) == ERR_OK);
    *rd = sv[0];
    *wr = sv[1];
}

typedef struct {
    int      calls;
    uint32_t last_events;
    int      last_fd;
    int      bytes_read;
    /* 한 번에 몇 바이트만 읽을지. 0이면 있는 대로 다 읽는다. */
    int       read_chunk;
    evloop_t *lp;
    bool      close_in_callback;
} probe_t;

static void on_event(int fd, uint32_t events, void *ctx)
{
    probe_t *p = ctx;

    p->calls++;
    p->last_events = events;
    p->last_fd = fd;

    if (events & EV_READ) {
        uint8_t buf[256];
        size_t  want = (p->read_chunk > 0) ? (size_t)p->read_chunk : sizeof(buf);
        ssize_t r = read(fd, buf, want);
        if (r > 0) {
            p->bytes_read += (int)r;
        }
    }

    if (p->close_in_callback) {
        assert(evloop_del(p->lp, fd) == ERR_OK);
        close(fd);
    }
}

/* --- 1. 여러 fd를 동시에 --- */

static void test_watches_many_fds(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);
    assert(evloop_count(lp) == 0);

    const int N = 4;
    int       rd[4];
    int       wr[4];
    probe_t   probes[4];

    for (int i = 0; i < N; i++) {
        make_pair(&rd[i], &wr[i]);
        memset(&probes[i], 0, sizeof(probes[i]));
        probes[i].lp = lp;
        assert(evloop_add(lp, rd[i], EV_READ, on_event, &probes[i]) == ERR_OK);
    }
    assert(evloop_count(lp) == N);

    /* 넷 모두에 쓴다. 한 번의 epoll_wait으로 넷 다 잡혀야 한다. */
    for (int i = 0; i < N; i++) {
        assert(write(wr[i], "hello", 5) == 5);
    }

    int handled = evloop_once(lp, 1000);
    assert(handled == N);

    for (int i = 0; i < N; i++) {
        assert(probes[i].calls == 1);
        assert(probes[i].last_fd == rd[i]);
        assert(probes[i].last_events & EV_READ);
        assert(probes[i].bytes_read == 5);
    }

    for (int i = 0; i < N; i++) {
        assert(evloop_del(lp, rd[i]) == ERR_OK);
        close(rd[i]);
        close(wr[i]);
    }
    assert(evloop_count(lp) == 0);

    evloop_destroy(lp);
}

/* --- 2. 레벨 트리거인가 --- */

/*
 * 완료 조건 2. **덜 읽어도 다음에 또 알려 준다.**
 *
 * 10바이트를 써 놓고 콜백이 한 번에 3바이트만 읽게 한다. 레벨 트리거면 남은
 * 바이트 때문에 계속 알려 주므로 네 번 만에 다 읽는다.
 *
 * 에지 트리거였다면 **첫 번째 알림 뒤로 아무 일도 일어나지 않는다** — 그
 * 접속은 조용히 멈춘다. 이 테스트가 그 차이를 잡는다.
 */
static void test_level_triggered(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);

    int rd;
    int wr;
    make_pair(&rd, &wr);

    probe_t p;
    memset(&p, 0, sizeof(p));
    p.lp = lp;
    p.read_chunk = 3; /* 한 번에 3바이트만 */

    assert(evloop_add(lp, rd, EV_READ, on_event, &p) == ERR_OK);
    assert(write(wr, "0123456789", 10) == 10);

    /* 10바이트를 3바이트씩 -> 네 번이면 다 읽는다. 넉넉히 여섯 번 돌린다. */
    for (int i = 0; i < 6 && p.bytes_read < 10; i++) {
        evloop_once(lp, 500);
    }

    assert(p.bytes_read == 10);
    assert(p.calls >= 4); /* 한 번에 다 읽지 않았다 */

    /* 다 읽었으면 더는 알려 주지 않는다. */
    int before = p.calls;
    assert(evloop_once(lp, 100) == 0);
    assert(p.calls == before);

    assert(evloop_del(lp, rd) == ERR_OK);
    close(rd);
    close(wr);
    evloop_destroy(lp);
}

/* --- 3. 등록·변경·해제 --- */

static void test_add_mod_del(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);

    int rd;
    int wr;
    make_pair(&rd, &wr);

    probe_t p;
    memset(&p, 0, sizeof(p));
    p.lp = lp;

    assert(evloop_add(lp, rd, EV_READ, on_event, &p) == ERR_OK);
    /* 두 번 등록할 수 없다. */
    assert(evloop_add(lp, rd, EV_READ, on_event, &p) == ERR_DUPLICATE);

    /* 쓰기도 보게 바꾼다 — 소켓은 대개 바로 쓸 수 있으므로 즉시 잡힌다. */
    assert(evloop_mod(lp, rd, EV_READ | EV_WRITE) == ERR_OK);
    assert(evloop_once(lp, 500) == 1);
    assert(p.last_events & EV_WRITE);

    /* 다시 읽기만. */
    assert(evloop_mod(lp, rd, EV_READ) == ERR_OK);
    assert(evloop_once(lp, 100) == 0); /* 읽을 것이 없다 */

    /* 해제하면 더 이상 불리지 않는다. */
    assert(evloop_del(lp, rd) == ERR_OK);
    assert(evloop_count(lp) == 0);
    assert(write(wr, "x", 1) == 1);
    int calls_before = p.calls;
    assert(evloop_once(lp, 100) == 0);
    assert(p.calls == calls_before);

    /* 없는 fd. */
    assert(evloop_del(lp, rd) == ERR_NOT_FOUND);
    assert(evloop_mod(lp, rd, EV_READ) == ERR_NOT_FOUND);

    close(rd);
    close(wr);
    evloop_destroy(lp);
}

/* --- 4. 인자 --- */

static void test_args(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);

    int rd;
    int wr;
    make_pair(&rd, &wr);

    probe_t p;
    memset(&p, 0, sizeof(p));
    p.lp = lp;

    assert(evloop_add(NULL, rd, EV_READ, on_event, &p) == ERR_NULL_PTR);
    assert(evloop_add(lp, rd, EV_READ, NULL, &p) == ERR_NULL_PTR);
    assert(evloop_add(lp, -1, EV_READ, on_event, &p) == ERR_INVALID_ARG);
    assert(evloop_add(lp, EVLOOP_FD_MAX, EV_READ, on_event, &p) ==
           ERR_INVALID_ARG);
    /* 아무것도 안 볼 거면 등록할 이유가 없다. */
    assert(evloop_add(lp, rd, 0, on_event, &p) == ERR_INVALID_ARG);

    /*
     * 완료 조건 4 — **블로킹 fd는 거절한다.** `wr`은 논블로킹으로 만들지
     * 않았다. 섞이면 루프가 어디선가 멈추는데 원인 찾기가 가장 어렵다.
     */
    assert(evloop_add(lp, wr, EV_READ, on_event, &p) == ERR_INVALID_ARG);
    assert(evloop_set_nonblocking(wr) == ERR_OK);
    assert(evloop_add(lp, wr, EV_READ, on_event, &p) == ERR_OK);

    assert(evloop_count(NULL) == 0);
    assert(evloop_once(NULL, 0) == ERR_NULL_PTR);
    assert(evloop_run(NULL, 10) == ERR_NULL_PTR);
    /* 무한 대기는 멈춤 신호를 못 본다. */
    assert(evloop_run(lp, -1) == ERR_INVALID_ARG);
    assert(evloop_set_nonblocking(-1) == ERR_INVALID_ARG);
    evloop_destroy(NULL);

    assert(evloop_del(lp, wr) == ERR_OK);
    close(rd);
    close(wr);
    evloop_destroy(lp);
}

/* --- 5. 콜백 안에서 닫기 --- */

/*
 * 완료 조건 5. **이 태스크에서 가장 틀리기 쉬운 지점이다.**
 *
 * `epoll_wait`은 이벤트를 한 묶음으로 준다. 여러 fd에 동시에 일이 생기면 그
 * 묶음에 다 들어 있고, 앞의 콜백이 자기 fd를 닫는 동안 **뒤의 fd 번호가
 * 재사용될 수** 있다. 등록 여부를 다시 보지 않으면 엉뚱한 콜백이 불린다.
 *
 * 여기서는 넷을 동시에 깨우고 **전부 콜백 안에서 닫는다.** 넷 다 정확히 한 번씩
 * 불리고, 루프가 깨지지 않아야 한다.
 */
static void test_close_inside_callback(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);

    const int N = 4;
    int       rd[4];
    int       wr[4];
    probe_t   probes[4];

    for (int i = 0; i < N; i++) {
        make_pair(&rd[i], &wr[i]);
        memset(&probes[i], 0, sizeof(probes[i]));
        probes[i].lp = lp;
        probes[i].close_in_callback = true;
        assert(evloop_add(lp, rd[i], EV_READ, on_event, &probes[i]) == ERR_OK);
    }

    for (int i = 0; i < N; i++) {
        assert(write(wr[i], "z", 1) == 1);
    }

    int handled = evloop_once(lp, 1000);
    assert(handled == N);

    /* 넷 다 정확히 한 번씩 — 두 번 불린 fd도, 안 불린 fd도 없다. */
    for (int i = 0; i < N; i++) {
        assert(probes[i].calls == 1);
    }

    /* 콜백이 전부 해제했으므로 표가 비었다. */
    assert(evloop_count(lp) == 0);

    /* 루프는 멀쩡하다 — 새 fd를 등록해 계속 쓸 수 있다. */
    int rd2;
    int wr2;
    make_pair(&rd2, &wr2);
    probe_t p2;
    memset(&p2, 0, sizeof(p2));
    p2.lp = lp;
    assert(evloop_add(lp, rd2, EV_READ, on_event, &p2) == ERR_OK);
    assert(write(wr2, "ok", 2) == 2);
    assert(evloop_once(lp, 500) == 1);
    assert(p2.bytes_read == 2);

    assert(evloop_del(lp, rd2) == ERR_OK);
    close(rd2);
    close(wr2);
    for (int i = 0; i < N; i++) {
        close(wr[i]); /* 읽는 쪽은 콜백이 이미 닫았다 */
    }
    evloop_destroy(lp);
}

/* 상대가 끊으면 EV_HANGUP이 온다 — 등록하지 않아도 커널이 얹어 준다. */
/*
 * --- 5-2. 콜백이 *다른* fd까지 닫는 경우 ---
 *
 * 위 5번은 콜백이 **자기 fd만** 닫았다. 그러면 묶음 안의 다른 이벤트는 다른
 * fd라서 재확인이 걸릴 일이 없다. 실제로 위험한 모양은 따로 있다 —
 * **한 접속에서 난 일이 다른 접속까지 끊는 경우**다. FEP에서는 흔하다.
 * 세션이 끊기면 그 거래소로 향하던 접속을 다 정리한다.
 *
 * 넷 다 읽을 게 있는 상태로 한 묶음에 올라오고, 맨 처음 불린 콜백이 넷 다
 * 끊는다. 재확인이 없으면 나머지 셋은 **이미 닫힌 fd로** 불린다.
 */
typedef struct {
    evloop_t *lp;
    int       fds[4];
    int       n;
    bool      torn_down;
    int       total_calls;
} teardown_t;

static void on_teardown(int fd, uint32_t events, void *ctx)
{
    teardown_t *t = ctx;

    (void)fd;
    (void)events;
    t->total_calls++;

    if (t->torn_down) {
        return; /* 이미 끊었다. 여기 오면 안 된다 — 아래에서 잡는다 */
    }
    t->torn_down = true;

    for (int i = 0; i < t->n; i++) {
        assert(evloop_del(t->lp, t->fds[i]) == ERR_OK);
        close(t->fds[i]);
    }
}

static void test_cascade_close(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);

    teardown_t t;
    memset(&t, 0, sizeof(t));
    t.lp = lp;
    t.n = 4;

    int wr[4];
    for (int i = 0; i < t.n; i++) {
        make_pair(&t.fds[i], &wr[i]);
        assert(evloop_add(lp, t.fds[i], EV_READ, on_teardown, &t) == ERR_OK);
    }
    for (int i = 0; i < t.n; i++) {
        assert(write(wr[i], "z", 1) == 1);
    }

    /*
     * 넷 다 이벤트가 올라왔지만 처리된 것은 하나다. 나머지 셋은 첫 콜백이
     * 해제했으므로 건너뛴다.
     */
    int handled = evloop_once(lp, 1000);
    assert(handled == 1);
    assert(t.total_calls == 1);
    assert(t.torn_down);
    assert(evloop_count(lp) == 0);

    for (int i = 0; i < t.n; i++) {
        close(wr[i]); /* 읽는 쪽은 콜백이 닫았다 */
    }
    evloop_destroy(lp);
}

/*
 * --- 6-2. 상대가 쓰기 쪽만 닫은 경우(half-close) ---
 *
 * 아래 test_hangup은 상대가 **완전히** 닫는다. 그때는 커널이 EPOLLHUP을
 * 얹어 주므로 EPOLLRDHUP을 등록하지 않아도 알 수 있다.
 *
 * 구분이 생기는 곳은 `shutdown(SHUT_WR)`이다. 상대는 아직 읽을 수 있고
 * 소켓도 살아 있다. EPOLLRDHUP을 등록하지 않으면 "읽을 게 있다"(EPOLLIN,
 * 실제로는 EOF)만 오고, **끊겼다는 사실은 0바이트를 읽어 봐야** 안다.
 *
 * 거래소 쪽이 응답만 마저 보내고 수신을 닫는 모양이 이것이다. 알아채는
 * 시점이 한 박자 늦으면 그 사이에 보낸 주문은 갈 곳이 없다.
 */
static void test_half_close(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);

    int rd;
    int wr;
    make_pair(&rd, &wr);

    probe_t p;
    memset(&p, 0, sizeof(p));
    p.lp = lp;

    assert(evloop_add(lp, rd, EV_READ, on_event, &p) == ERR_OK);

    /* 쓰기 쪽만 닫는다. wr은 아직 열려 있다. */
    assert(shutdown(wr, SHUT_WR) == 0);

    assert(evloop_once(lp, 500) == 1);
    assert(p.last_events & EV_HANGUP);

    assert(evloop_del(lp, rd) == ERR_OK);
    close(rd);
    close(wr);
    evloop_destroy(lp);
}

static void test_hangup(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);

    int rd;
    int wr;
    make_pair(&rd, &wr);

    probe_t p;
    memset(&p, 0, sizeof(p));
    p.lp = lp;

    assert(evloop_add(lp, rd, EV_READ, on_event, &p) == ERR_OK);
    close(wr); /* 상대가 끊었다 */

    assert(evloop_once(lp, 500) == 1);
    assert(p.last_events & EV_HANGUP);

    assert(evloop_del(lp, rd) == ERR_OK);
    close(rd);
    evloop_destroy(lp);
}

/* --- 6. 멈춤 --- */

static void test_stop(void)
{
    evloop_reset_stop();

    evloop_t *lp = evloop_create();
    assert(lp != NULL);

    /* 이미 멈춤이면 한 바퀴도 안 돈다. */
    evloop_request_stop();
    assert(evloop_stopping());
    assert(evloop_run(lp, 10) == 0);

    evloop_reset_stop();
    assert(!evloop_stopping());

    int rd;
    int wr;
    make_pair(&rd, &wr);

    probe_t p;
    memset(&p, 0, sizeof(p));
    p.lp = lp;
    assert(evloop_add(lp, rd, EV_READ, on_event, &p) == ERR_OK);
    assert(write(wr, "bye", 3) == 3);

    /* 한 번 처리한 뒤 멈춘다. 여기서 안 돌아오면 멈춤 확인이 빠진 것이다. */
    assert(evloop_once(lp, 500) == 1);
    evloop_request_stop();
    assert(evloop_run(lp, 10) == 0);

    assert(evloop_install_signals() == ERR_OK);

    evloop_reset_stop();
    assert(evloop_del(lp, rd) == ERR_OK);
    close(rd);
    close(wr);
    evloop_destroy(lp);
}

int main(void)
{
    STEP(test_watches_many_fds);
    STEP(test_level_triggered);
    STEP(test_add_mod_del);
    STEP(test_args);
    STEP(test_close_inside_callback);
    STEP(test_cascade_close);
    STEP(test_half_close);
    STEP(test_hangup);
    STEP(test_stop);
    return 0;
}
