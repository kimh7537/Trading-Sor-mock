#include "listener.h"

#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "errors.h"

struct listener {
    int      fd;
    uint16_t port;
};

/*
 * 멈춤 플래그.
 *
 * 전역 가변 상태는 CLAUDE.md가 금지하지만 **여기는 예외다.** 시그널 핸들러는
 * 인자를 받지 않으므로 컨텍스트를 넘길 방법이 없다. 그래서 프로세스에 하나뿐인
 * 값으로 두고, 핸들러가 하는 일은 여기에 1을 쓰는 것뿐이다.
 *
 * `volatile sig_atomic_t`인 이유: 이 타입만이 시그널 핸들러와 주 흐름 사이에서
 * 쪼개지지 않고 읽고 쓰인다고 표준이 보장한다.
 */
static volatile sig_atomic_t g_stop = 0;

void listener_request_stop(void)
{
    g_stop = 1;
}

bool listener_stopping(void)
{
    return g_stop != 0;
}

void listener_reset_stop(void)
{
    g_stop = 0;
}

static void on_signal(int sig)
{
    (void)sig;
    /*
     * 하는 일이 이것뿐이다. printf도, free도, 로그도 부르지 않는다 —
     * 거의 모든 라이브러리 함수가 비동기 시그널 안전하지 않다.
     */
    g_stop = 1;
}

int listener_install_signals(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    /*
     * SA_RESTART를 **켜지 않는다.** 켜면 accept()가 시그널 뒤 자동으로 재시작되어
     * 멈춤 플래그를 볼 기회가 없다. EINTR로 깨어나야 루프가 빠져나올 수 있다.
     */
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        return ERR_INVALID_ARG;
    }
    if (sigaction(SIGINT, &sa, NULL) != 0) {
        return ERR_INVALID_ARG;
    }

    /*
     * 상대가 먼저 끊은 소켓에 쓰면 SIGPIPE로 프로세스가 죽는다. 접속이 끊기는 것은
     * 오류가 아니라 일상이므로 무시하고 write()의 EPIPE로 받는다.
     */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, NULL) != 0) {
        return ERR_INVALID_ARG;
    }

    return ERR_OK;
}

listener_t *listener_open(uint16_t port, int backlog)
{
    if (backlog <= 0) {
        return NULL;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return NULL;
    }

    /*
     * 재시작할 때 TIME_WAIT 때문에 bind가 실패하지 않게 한다. 없으면 운영에서
     * "조금 기다렸다 다시 켜세요"가 되고, 테스트에서는 연달아 돌릴 수 없다.
     */
    int on = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) != 0) {
        close(fd);
        return NULL;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (bind(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return NULL;
    }
    if (listen(fd, backlog) != 0) {
        close(fd);
        return NULL;
    }

    /* 포트 0을 줬으면 커널이 고른 번호를 되묻는다. */
    struct sockaddr_in bound;
    socklen_t          blen = sizeof(bound);
    memset(&bound, 0, sizeof(bound));
    if (getsockname(fd, (struct sockaddr *)&bound, &blen) != 0) {
        close(fd);
        return NULL;
    }

    listener_t *ln = calloc(1, sizeof(*ln));
    if (ln == NULL) {
        close(fd);
        return NULL;
    }
    ln->fd = fd;
    ln->port = ntohs(bound.sin_port);

    return ln;
}

uint16_t listener_port(const listener_t *ln)
{
    return (ln != NULL) ? ln->port : 0;
}

int listener_fd(const listener_t *ln)
{
    return (ln != NULL) ? ln->fd : -1;
}

void listener_close(listener_t *ln)
{
    if (ln == NULL) {
        return;
    }
    if (ln->fd >= 0) {
        close(ln->fd);
    }
    free(ln);
}

/*
 * n바이트를 채울 때까지 읽는다.
 *
 * read()는 요청한 만큼 준다고 약속하지 않는다 — TCP는 바이트 스트림이라 전문 하나가
 * 여러 번에 나뉘어 올 수 있다. 한 번 읽고 끝내면 길이가 긴 전문에서만 가끔 깨진다.
 *
 * 반환: n(성공), 0(상대가 깨끗이 끊음), 음수 에러.
 */
static int read_exact(int fd, uint8_t *buf, size_t n)
{
    size_t got = 0;

    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r == 0) {
            return (got == 0) ? 0 : ERR_INVALID_ARG; /* 전문 도중에 끊겼다 */
        }
        if (r < 0) {
            if (errno == EINTR) {
                if (listener_stopping()) {
                    return ERR_INVALID_ARG;
                }
                continue;
            }
            return ERR_INVALID_ARG;
        }
        got += (size_t)r;
    }
    return (int)n;
}

/* n바이트를 전부 쓴다. write()도 부분 쓰기를 한다. */
static int write_exact(int fd, const uint8_t *buf, size_t n)
{
    size_t sent = 0;

    while (sent < n) {
        ssize_t w = write(fd, buf + sent, n - sent);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ERR_INVALID_ARG;
        }
        sent += (size_t)w;
    }
    return (int)n;
}

/* 접속 하나를 끝까지 다룬다. 처리한 전문 수를 반환한다. */
static int serve_conn(int fd, frame_handler_fn fn, void *ctx)
{
    static uint8_t body[WIRE_BODY_MAX];
    static uint8_t out[WIRE_FRAME_MAX];
    uint8_t        hbuf[WIRE_HEADER_LEN];
    int            handled = 0;

    for (;;) {
        int r = read_exact(fd, hbuf, WIRE_HEADER_LEN);
        if (r == 0) {
            return handled; /* 상대가 깨끗이 끊었다 */
        }
        if (r < 0) {
            return r;
        }

        wire_header_t hdr;
        if (wire_decode_header(hbuf, WIRE_HEADER_LEN, &hdr) < 0) {
            return ERR_INVALID_ARG;
        }

        /*
         * **헤더가 말하는 길이를 종별 표와 대조한다.** 헤더만 믿으면 규격과 다른
         * 길이를 그대로 읽어 필드가 밀린 값을 그럴듯하게 돌려준다(T3-02).
         * 모르는 종별도 여기서 끊는다 — 조용히 넘기면 스트림 동기가 깨진다.
         */
        if (!msg_is_known(hdr.type)) {
            return ERR_NOT_SUPPORTED;
        }
        if ((int32_t)hdr.body_len != msg_body_len(hdr.type)) {
            return ERR_INVALID_ARG;
        }

        if (hdr.body_len > 0) {
            r = read_exact(fd, body, hdr.body_len);
            if (r <= 0) {
                return (r == 0) ? ERR_INVALID_ARG : r;
            }
        }

        handled++;

        if (fn == NULL) {
            continue;
        }

        int n = fn(&hdr, body, out, sizeof(out), ctx);
        if (n < 0) {
            return handled; /* 훅이 끊으라고 했다 */
        }
        if (n > 0) {
            if (write_exact(fd, out, (size_t)n) < 0) {
                return handled;
            }
        }
    }
}

int listener_serve_one(listener_t *ln, frame_handler_fn fn, void *ctx,
                       bool *accepted)
{
    if (accepted != NULL) {
        *accepted = false;
    }
    if (ln == NULL || ln->fd < 0) {
        return ERR_NULL_PTR;
    }
    if (listener_stopping()) {
        return 0;
    }

    int cfd;
    for (;;) {
        cfd = accept(ln->fd, NULL, NULL);
        if (cfd >= 0) {
            break;
        }
        if (errno == EINTR) {
            /* 시그널에 깨어났다. 멈추라는 것이면 여기서 나간다. */
            if (listener_stopping()) {
                return 0;
            }
            continue;
        }
        return ERR_INVALID_ARG;
    }

    /*
     * 여기부터는 접속을 받은 것이 확정이다. **처리 결과와 무관하게 표시한다** —
     * 이 뒤에 멈춤 시그널이 와도 이미 한 일은 한 일이다. 표시를 뒤로 미루면
     * 종료 직전에 끝난 접속이 집계에서 사라진다.
     */
    if (accepted != NULL) {
        *accepted = true;
    }

    int handled = serve_conn(cfd, fn, ctx);
    close(cfd);

    return handled;
}

int listener_run(listener_t *ln, frame_handler_fn fn, void *ctx)
{
    if (ln == NULL || ln->fd < 0) {
        return ERR_NULL_PTR;
    }

    int conns = 0;
    while (!listener_stopping()) {
        bool accepted = false;
        int  rc = listener_serve_one(ln, fn, ctx, &accepted);
        if (rc == ERR_NULL_PTR) {
            return rc;
        }
        if (!accepted) {
            break; /* 멈추라고 해서 돌아온 것이다 */
        }
        /*
         * 접속 하나가 잘못된 전문을 보내도 리스너는 살아 있다. 그 접속만 끊고
         * 다음을 받는다 — 상대 하나가 서버를 세울 수 있으면 안 된다.
         */
        conns++;
    }

    return conns;
}
