#include "evloop.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "errors.h"

typedef struct {
    ev_fn    fn;
    void    *ctx;
    uint32_t events;
    bool     registered;
} slot_t;

struct evloop {
    int     epfd;
    slot_t  slot[EVLOOP_FD_MAX];
    int32_t count;
};

/*
 * 멈춤 플래그. T3-03의 리스너와 같은 모양이고 이유도 같다 — 시그널 핸들러는
 * 인자를 받지 않으므로 프로세스에 하나뿐인 값으로 둔다.
 */
static volatile sig_atomic_t g_stop = 0;

void evloop_request_stop(void)
{
    g_stop = 1;
}

bool evloop_stopping(void)
{
    return g_stop != 0;
}

void evloop_reset_stop(void)
{
    g_stop = 0;
}

static void on_signal(int sig)
{
    (void)sig;
    g_stop = 1;
}

int evloop_install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; /* SA_RESTART를 켜지 않는다(T3-03과 같은 이유) */

    if (sigaction(SIGTERM, &sa, NULL) != 0 ||
        sigaction(SIGINT, &sa, NULL) != 0) {
        return ERR_INVALID_ARG;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) != 0) {
        return ERR_INVALID_ARG;
    }

    return ERR_OK;
}

/* EV_* -> epoll. EV_ERROR·EV_HANGUP은 등록할 수 없다 — 커널이 얹어 준다. */
static uint32_t to_epoll(uint32_t events)
{
    uint32_t e = 0;

    if (events & EV_READ) {
        e |= EPOLLIN;
    }
    if (events & EV_WRITE) {
        e |= EPOLLOUT;
    }
    return e;
}

static uint32_t from_epoll(uint32_t e)
{
    uint32_t events = 0;

    if (e & EPOLLIN) {
        events |= EV_READ;
    }
    if (e & EPOLLOUT) {
        events |= EV_WRITE;
    }
    if (e & EPOLLERR) {
        events |= EV_ERROR;
    }
    if (e & (EPOLLHUP | EPOLLRDHUP)) {
        events |= EV_HANGUP;
    }
    return events;
}

evloop_t *evloop_create(void)
{
    evloop_t *lp = calloc(1, sizeof(*lp));
    if (lp == NULL) {
        return NULL;
    }

    /*
     * EPOLL_CLOEXEC — exec하는 자식에게 이 fd를 물려주지 않는다.
     * 물려주면 자식이 쓰지도 않을 fd를 들고 있게 된다.
     */
    lp->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (lp->epfd < 0) {
        free(lp);
        return NULL;
    }
    return lp;
}

void evloop_destroy(evloop_t *lp)
{
    if (lp == NULL) {
        return;
    }
    if (lp->epfd >= 0) {
        close(lp->epfd);
    }
    free(lp);
}

int evloop_set_nonblocking(int fd)
{
    if (fd < 0) {
        return ERR_INVALID_ARG;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return ERR_INVALID_ARG;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return ERR_INVALID_ARG;
    }
    return ERR_OK;
}

static bool is_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && (flags & O_NONBLOCK) != 0;
}

int evloop_add(evloop_t *lp, int fd, uint32_t events, ev_fn fn, void *ctx)
{
    if (lp == NULL || fn == NULL) {
        return ERR_NULL_PTR;
    }
    if (fd < 0 || fd >= EVLOOP_FD_MAX) {
        return ERR_INVALID_ARG;
    }
    if ((events & (EV_READ | EV_WRITE)) == 0) {
        return ERR_INVALID_ARG; /* 아무것도 안 볼 거면 등록할 이유가 없다 */
    }
    if (lp->slot[fd].registered) {
        return ERR_DUPLICATE;
    }
    /*
     * **블로킹 fd는 받지 않는다.** 섞이면 루프가 어디선가 멈추는데, 그 원인을
     * 찾는 것이 이 계층에서 가장 어려운 일이다. 등록할 때 막는 편이 훨씬 싸다.
     */
    if (!is_nonblocking(fd)) {
        return ERR_INVALID_ARG;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = to_epoll(events) | EPOLLRDHUP;
    ev.data.fd = fd;

    if (epoll_ctl(lp->epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
        return ERR_INVALID_ARG;
    }

    lp->slot[fd].fn = fn;
    lp->slot[fd].ctx = ctx;
    lp->slot[fd].events = events;
    lp->slot[fd].registered = true;
    lp->count++;

    return ERR_OK;
}

int evloop_mod(evloop_t *lp, int fd, uint32_t events)
{
    if (lp == NULL) {
        return ERR_NULL_PTR;
    }
    if (fd < 0 || fd >= EVLOOP_FD_MAX || !lp->slot[fd].registered) {
        return ERR_NOT_FOUND;
    }
    if ((events & (EV_READ | EV_WRITE)) == 0) {
        return ERR_INVALID_ARG;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = to_epoll(events) | EPOLLRDHUP;
    ev.data.fd = fd;

    if (epoll_ctl(lp->epfd, EPOLL_CTL_MOD, fd, &ev) != 0) {
        return ERR_INVALID_ARG;
    }

    lp->slot[fd].events = events;
    return ERR_OK;
}

int evloop_del(evloop_t *lp, int fd)
{
    if (lp == NULL) {
        return ERR_NULL_PTR;
    }
    if (fd < 0 || fd >= EVLOOP_FD_MAX || !lp->slot[fd].registered) {
        return ERR_NOT_FOUND;
    }

    /*
     * 실패해도 표에서는 지운다. 콜백이 fd를 **먼저 닫고** 이 함수를 부르면
     * `epoll_ctl`이 EBADF로 실패하는데, 그렇다고 등록이 남아 있으면 그 fd
     * 번호가 재사용될 때 엉뚱한 콜백이 불린다.
     */
    (void)epoll_ctl(lp->epfd, EPOLL_CTL_DEL, fd, NULL);

    memset(&lp->slot[fd], 0, sizeof(lp->slot[fd]));
    lp->count--;

    return ERR_OK;
}

int32_t evloop_count(const evloop_t *lp)
{
    return (lp != NULL) ? lp->count : 0;
}

int evloop_once(evloop_t *lp, int timeout_ms)
{
    if (lp == NULL || lp->epfd < 0) {
        return ERR_NULL_PTR;
    }

    struct epoll_event evs[EVLOOP_EVENTS_MAX];

    int n = epoll_wait(lp->epfd, evs, EVLOOP_EVENTS_MAX, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) {
            return 0; /* 시그널에 깨어났다. 호출부가 멈춤 여부를 본다 */
        }
        return ERR_INVALID_ARG;
    }

    int handled = 0;
    for (int i = 0; i < n; i++) {
        int fd = evs[i].data.fd;

        if (fd < 0 || fd >= EVLOOP_FD_MAX) {
            continue;
        }
        /*
         * **여기서 다시 본다.** 앞선 콜백이 이 fd를 끊었을 수 있다. 그대로
         * 부르면 이미 닫힌 fd로 콜백이 불리고, fd 번호는 곧바로 재사용되므로
         * 엉뚱한 접속의 콜백이 될 수도 있다.
         */
        if (!lp->slot[fd].registered) {
            continue;
        }

        /*
         * `fn`이 NULL인지는 보지 않는다. `evloop_add`가 NULL을 거절하므로
         * registered가 참이면 fn은 반드시 있다. 한 번 더 보면 안전해 보이지만
         * **위 재확인과 구분이 안 된다** — 해제하면 슬롯 전체가 0이 되므로
         * 재확인을 지워도 NULL 검사가 대신 막아 버린다. 그러면 정작 중요한
         * 재확인이 없어졌는지를 테스트가 알 수 없다. 검사를 하나로 둔다.
         */
        ev_fn fn = lp->slot[fd].fn;
        void *ctx = lp->slot[fd].ctx;

        fn(fd, from_epoll(evs[i].events), ctx);
        handled++;
    }

    return handled;
}

int evloop_run(evloop_t *lp, int tick_ms)
{
    if (lp == NULL || lp->epfd < 0) {
        return ERR_NULL_PTR;
    }
    if (tick_ms < 0) {
        return ERR_INVALID_ARG; /* 무한 대기는 멈춤 신호를 못 본다 */
    }

    int total = 0;
    while (!evloop_stopping()) {
        int rc = evloop_once(lp, tick_ms);
        if (rc < 0) {
            return rc;
        }
        total += rc;
    }
    return total;
}
