/*
 * T3-03 원장 리스너.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 포트 0으로 열고 커널이 준 번호를 되물을 수 있다
 *  2. 접속을 받아 **프레임 단위로** 전문을 읽고 훅에 넘긴다
 *  3. SIGTERM에 깨끗이 멈춘다
 *  4. 모르는 종별 / 규격과 다른 길이는 접속을 끊는다
 *
 * **진짜 소켓을 쓴다.** 가짜 fd로 대체하면 이 태스크가 실제로 맡은 일 — bind,
 * accept, 부분 수신, EINTR — 이 전부 빠진다. 대신 자식 프로세스를 fork해서
 * 클라이언트로 쓰고, 부모가 리스너를 돌린다.
 *
 * 부분 수신은 **전문을 한 바이트씩 나눠 보내서** 만든다. 한 번에 보내면 커널이
 * 붙여 주기 때문에 read()가 쪼개지는 경로를 시험할 수 없다.
 */
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"
#include "listener.h"

/* --- 훅 --- */

typedef struct {
    int      calls;
    uint8_t  last_type;
    uint64_t last_seq;
    int64_t  last_ts;
    uint64_t last_cl_ord_id;
    bool     reply;
} probe_t;

/* 받은 주문에 ACK를 돌려주는 훅. */
static int on_frame(const wire_header_t *hdr, const uint8_t *body, uint8_t *out,
                    size_t out_cap, void *ctx)
{
    probe_t *p = ctx;

    if (p == NULL) {
        return 0;
    }

    p->calls++;
    p->last_type = hdr->type;
    p->last_seq = hdr->seq;
    p->last_ts = hdr->ts;

    if (hdr->type == MSG_ORDER_REQ) {
        msg_order_req_t req;
        if (msg_decode_order_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        p->last_cl_ord_id = req.cl_ord_id;

        if (!p->reply) {
            return 0;
        }

        msg_order_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.cl_ord_id = req.cl_ord_id;
        ack.order_id = req.cl_ord_id + 1000;
        ack.status = STATUS_NEW;
        ack.reason = ERR_OK;
        ack.price = req.price;

        /*
         * 응답의 논리 시각은 **요청이 들고 온 것을 그대로 쓴다.** 리스너도 훅도
         * 시스템 시각을 읽지 않는다 — 결정성의 경계를 여기서 지킨다.
         */
        wire_header_t h;
        memset(&h, 0, sizeof(h));
        h.type = MSG_ORDER_ACK;
        h.body_len = MSG_ORDER_ACK_LEN;
        h.seq = hdr->seq;
        h.ts = hdr->ts;

        int n = wire_encode_header(&h, out, out_cap);
        if (n < 0) {
            return -1;
        }
        int m = msg_encode_order_ack(&ack, out + n, out_cap - (size_t)n);
        if (m < 0) {
            return -1;
        }
        return n + m;
    }

    return 0;
}

/* --- 클라이언트 쪽 도구 --- */

static int dial(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);

    if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void send_all(int fd, const uint8_t *buf, size_t n)
{
    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, buf + sent, n - sent);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        sent += (size_t)w;
    }
}

/* 한 바이트씩 보낸다 — read()가 쪼개지는 경로를 만든다. */
static void send_byte_by_byte(int fd, const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (write(fd, buf + i, 1) != 1) {
            return;
        }
    }
}

static size_t build_order_frame(uint8_t *buf, size_t cap, uint64_t cl_ord_id,
                                uint64_t seq, int64_t ts)
{
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = MSG_ORDER_REQ;
    h.body_len = MSG_ORDER_REQ_LEN;
    h.seq = seq;
    h.ts = ts;

    int n = wire_encode_header(&h, buf, cap);
    assert(n == (int)WIRE_HEADER_LEN);

    msg_order_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", "ACC-001");
    snprintf(req.symbol, sizeof(req.symbol), "%s", "005930");
    req.cl_ord_id = cl_ord_id;
    req.side = SIDE_BUY;
    req.type = ORDER_LIMIT;
    req.market = MARKET_KRX;
    req.price = 10000;
    req.qty = 100;

    int m = msg_encode_order_req(&req, buf + n, cap - (size_t)n);
    assert(m == MSG_ORDER_REQ_LEN);

    return (size_t)(n + m);
}

/* --- 1. 열기 --- */

static void test_open_and_port(void)
{
    listener_reset_stop();

    /* 포트 0 — 커널이 고른다. 고정 포트를 쓰면 CI에서 부딪힌다. */
    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);
    assert(listener_port(ln) != 0);
    assert(listener_fd(ln) >= 0);

    listener_close(ln);

    /* 인자 검사. */
    assert(listener_open(0, 0) == NULL);
    assert(listener_open(0, -1) == NULL);
    assert(listener_port(NULL) == 0);
    assert(listener_fd(NULL) == -1);
    listener_close(NULL);
}

/*
 * `SO_REUSEADDR`이 실제로 일하는지 본다.
 *
 * **접속이 한 번도 없었던 포트를 다시 여는 것으로는 아무것도 확인되지 않는다.**
 * TIME_WAIT이 없으면 옵션이 꺼져 있어도 bind가 성공하기 때문이다.
 *
 * 그래서 진짜 TIME_WAIT을 만든다. 접속을 맺고 **서버가 먼저 끊으면** 그 포트에
 * TIME_WAIT 상태의 연결이 남고, 그때부터 같은 포트로의 bind는 옵션 없이는
 * EADDRINUSE로 막힌다. 그 상태에서 다시 열리는지가 이 옵션의 값어치다.
 */
static void test_reuseaddr_after_connection(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        listener_close(ln);
        int fd = dial(port);
        if (fd < 0) {
            _exit(1);
        }

        /*
         * **서버가 먼저 끊게 만들어야** 서버 쪽이 TIME_WAIT에 들어간다.
         * 클라이언트가 먼저 끊으면 TIME_WAIT은 클라이언트 쪽에 생기고 서버는
         * 그냥 EOF를 받을 뿐이라 이 검사가 성립하지 않는다.
         *
         * 모르는 종별을 보내면 서버가 접속을 끊는다(완료 조건 4). 그것을 쓴다.
         */
        wire_header_t h;
        memset(&h, 0, sizeof(h));
        h.type = 200; /* 목록에 없는 종별 */
        h.seq = 1;
        h.ts = 100;

        uint8_t buf[WIRE_HEADER_LEN];
        if (wire_encode_header(&h, buf, sizeof(buf)) < 0) {
            close(fd);
            _exit(2);
        }
        send_all(fd, buf, sizeof(buf));

        /* 서버의 FIN을 기다린다. */
        uint8_t sink[64];
        ssize_t r = read(fd, sink, sizeof(sink));
        (void)r;
        close(fd);
        _exit(0);
    }

    probe_t p;
    memset(&p, 0, sizeof(p));
    p.reply = true;

    /* 서버가 모르는 종별을 보고 먼저 끊는다. */
    int rc = listener_serve_one(ln, on_frame, &p, NULL);
    assert(rc < 0);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);

    listener_close(ln);

    /* 이제 그 포트에 TIME_WAIT이 남아 있다. SO_REUSEADDR이 있어야 다시 열린다. */
    listener_t *again = listener_open(port, 16);
    assert(again != NULL);
    assert(listener_port(again) == port);
    listener_close(again);
}

/* --- 2. 전문을 받는다 --- */

/*
 * 자식이 클라이언트, 부모가 리스너다. 자식이 전문을 **한 바이트씩** 보내
 * 부분 수신 경로를 강제한다.
 */
static void test_receives_frames(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        /* 자식 — 클라이언트 */
        listener_close(ln);

        int fd = dial(port);
        if (fd < 0) {
            _exit(1);
        }

        uint8_t f1[WIRE_HEADER_LEN + MSG_ORDER_REQ_LEN];
        uint8_t f2[WIRE_HEADER_LEN + MSG_ORDER_REQ_LEN];
        size_t  n1 = build_order_frame(f1, sizeof(f1), 11, 1, 5000);
        size_t  n2 = build_order_frame(f2, sizeof(f2), 22, 2, 6000);

        /* 첫 전문은 한 바이트씩, 둘째는 한 번에. 두 경로를 다 밟는다. */
        send_byte_by_byte(fd, f1, n1);
        send_all(fd, f2, n2);

        /* ACK 둘을 받는다. */
        uint8_t ack[2 * (WIRE_HEADER_LEN + MSG_ORDER_ACK_LEN)];
        size_t  got = 0;
        while (got < sizeof(ack)) {
            ssize_t r = read(fd, ack + got, sizeof(ack) - got);
            if (r <= 0) {
                break;
            }
            got += (size_t)r;
        }
        if (got != sizeof(ack)) {
            close(fd);
            _exit(2);
        }

        /* 첫 ACK의 내용을 확인한다. */
        wire_header_t h;
        if (wire_decode_header(ack, sizeof(ack), &h) < 0 ||
            h.type != MSG_ORDER_ACK || h.seq != 1 || h.ts != 5000) {
            close(fd);
            _exit(3);
        }
        msg_order_ack_t a;
        if (msg_decode_order_ack(ack + WIRE_HEADER_LEN, MSG_ORDER_ACK_LEN,
                                 &a) < 0 ||
            a.cl_ord_id != 11 || a.order_id != 1011) {
            close(fd);
            _exit(4);
        }

        close(fd);
        _exit(0);
    }

    /* 부모 — 리스너 */
    probe_t p;
    memset(&p, 0, sizeof(p));
    p.reply = true;

    int handled = listener_serve_one(ln, on_frame, &p, NULL);
    assert(handled == 2);
    assert(p.calls == 2);
    assert(p.last_type == MSG_ORDER_REQ);
    assert(p.last_seq == 2);
    assert(p.last_ts == 6000);
    assert(p.last_cl_ord_id == 22);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    listener_close(ln);
}

/* --- 3. 잘못된 전문은 접속을 끊는다 --- */

/*
 * 완료 조건 4. **조용히 넘기지 않는다.**
 *
 * 모르는 종별을 건너뛰려면 얼마를 건너뛸지 알아야 하는데, 모르는 종별은 길이도
 * 모른다. 규격과 다른 길이도 마찬가지다 — 그 지점부터 스트림 동기가 깨진다.
 * 끊는 것이 유일하게 정직한 선택이다.
 */
static void run_bad_frame_case(uint8_t type, uint32_t body_len, int want_rc)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        listener_close(ln);
        int fd = dial(port);
        if (fd < 0) {
            _exit(1);
        }

        wire_header_t h;
        memset(&h, 0, sizeof(h));
        h.type = type;
        h.body_len = body_len;
        h.seq = 1;
        h.ts = 100;

        uint8_t buf[WIRE_HEADER_LEN + 64];
        memset(buf, 0, sizeof(buf));
        int n = wire_encode_header(&h, buf, sizeof(buf));
        if (n < 0) {
            close(fd);
            _exit(2);
        }
        send_all(fd, buf, (size_t)n + body_len);

        /*
         * 서버가 끊을 때까지 기다린다. 결과를 안 쓰지만 `(void)`로는 부족하다 —
         * Release(-O2 + _FORTIFY_SOURCE)에서 read()에 warn_unused_result가 붙어
         * -Werror로 막힌다. 값을 실제로 받아 둔다.
         */
        uint8_t sink[16];
        ssize_t ignored = read(fd, sink, sizeof(sink));
        (void)ignored;
        close(fd);
        _exit(0);
    }

    probe_t p;
    memset(&p, 0, sizeof(p));
    p.reply = true;

    int rc = listener_serve_one(ln, on_frame, &p, NULL);
    /* 훅이 불리기 전에 끊겼다. */
    assert(p.calls == 0);

    /*
     * **거절 이유까지 대조한다.** `rc < 0`만 보면 두 검사가 서로를 가린다 —
     * 모르는 종별은 길이 대조에서도 걸리므로(모르는 종별의 규격 길이는 -1),
     * 종별 검사를 통째로 지워도 여전히 음수가 나온다. 그러면 "모르는 종별이다"와
     * "길이가 틀렸다"를 구분하지 않는 코드가 테스트를 통과한다.
     */
    assert(rc == want_rc);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);

    listener_close(ln);
}

static void test_rejects_bad_frames(void)
{
    /* 모르는 종별 — 목록에 없는 코드. 길이 오류가 아니라 종별 오류다. */
    run_bad_frame_case(200, 0, ERR_NOT_SUPPORTED);
    run_bad_frame_case(MSG_UNKNOWN, 0, ERR_NOT_SUPPORTED);

    /* 아는 종별인데 길이가 규격과 다르다. */
    run_bad_frame_case(MSG_ORDER_REQ, MSG_ORDER_REQ_LEN - 1, ERR_INVALID_ARG);
    run_bad_frame_case(MSG_ORDER_REQ, MSG_ORDER_REQ_LEN + 1, ERR_INVALID_ARG);
}

/* 전문 도중에 끊긴 접속도 조용히 넘어가지 않는다. */
static void test_truncated_frame(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        listener_close(ln);
        int fd = dial(port);
        if (fd < 0) {
            _exit(1);
        }

        uint8_t f[WIRE_HEADER_LEN + MSG_ORDER_REQ_LEN];
        (void)build_order_frame(f, sizeof(f), 1, 1, 100);
        /* 헤더는 다 보내고 바디는 절반만 보낸 뒤 끊는다. */
        send_all(fd, f, WIRE_HEADER_LEN + MSG_ORDER_REQ_LEN / 2);
        close(fd);
        _exit(0);
    }

    probe_t p;
    memset(&p, 0, sizeof(p));

    int rc = listener_serve_one(ln, on_frame, &p, NULL);
    assert(rc < 0);
    assert(p.calls == 0);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    listener_close(ln);
}

/*
 * **헤더 도중에 끊긴 경우**를 따로 본다.
 *
 * 바디가 잘린 경우는 뒤따르는 검사가 한 번 더 걸러 주므로, read_exact가
 * "도중에 끊김"을 정상 종료로 착각해도 겉으로는 똑같이 음수가 나온다.
 * 헤더가 잘리면 그 두 번째 그물이 없다 — 착각한 코드는 이것을 **깨끗한 접속
 * 종료**로 보고하고, 반쯤 온 전문을 조용히 버린다.
 */
static void test_truncated_header(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        listener_close(ln);
        int fd = dial(port);
        if (fd < 0) {
            _exit(1);
        }
        uint8_t f[WIRE_HEADER_LEN + MSG_ORDER_REQ_LEN];
        (void)build_order_frame(f, sizeof(f), 1, 1, 100);
        /* 헤더 24바이트 중 10바이트만 보내고 끊는다. */
        send_all(fd, f, 10);
        close(fd);
        _exit(0);
    }

    probe_t p;
    memset(&p, 0, sizeof(p));

    int rc = listener_serve_one(ln, on_frame, &p, NULL);
    /* 깨끗한 종료(0)가 아니라 오류여야 한다. */
    assert(rc < 0);
    assert(p.calls == 0);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    listener_close(ln);
}

/* 아무것도 안 보내고 끊으면 오류가 아니다 — 흔한 일이다. */
static void test_clean_disconnect(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        listener_close(ln);
        int fd = dial(port);
        if (fd >= 0) {
            close(fd);
        }
        _exit(0);
    }

    probe_t p;
    memset(&p, 0, sizeof(p));

    int rc = listener_serve_one(ln, on_frame, &p, NULL);
    assert(rc == 0);
    assert(p.calls == 0);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    listener_close(ln);
}

/* --- 4. 종료 --- */

/*
 * 완료 조건 3. accept()에서 기다리는 중에 SIGTERM이 오면 EINTR로 깨어나
 * 멈춤 플래그를 보고 빠져나온다.
 *
 * `SA_RESTART`가 켜져 있으면 accept()가 자동 재시작되어 **영원히 돌아오지 않는다.**
 * 이 테스트가 그것을 잡는다 — 걸리면 통과가 아니라 멈춤으로 나타난다.
 */
static void test_signal_stops_accept(void)
{
    listener_reset_stop();
    assert(listener_install_signals() == ERR_OK);

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);

    pid_t self = getpid();
    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        /* 자식이 부모에게 SIGTERM을 보낸다. 부모는 accept()에서 기다리는 중. */
        listener_close(ln);
        struct timespec ts = {0, 100 * 1000 * 1000}; /* 100ms */
        nanosleep(&ts, NULL);
        kill(self, SIGTERM);
        _exit(0);
    }

    /* accept()에서 기다리다 시그널에 깨어나 돌아와야 한다. */
    int rc = listener_serve_one(ln, on_frame, NULL, NULL);
    assert(rc == 0);
    assert(listener_stopping());

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);

    listener_close(ln);
    listener_reset_stop();
}

/* 이미 멈춤 요청이 있으면 accept조차 하지 않는다. */
/*
 * T8-01 — 기다리는 동안 틱을 칠 틈이 있는가.
 *
 * `accept()`도 `read()`도 무한히 기다리므로 실시세 모드에서는 호가창을 움직일 곳이
 * 없었다. `poll()` 타임아웃이 만료될 때마다 훅이 불리는지 두 자리에서 본다.
 */
static int g_idle_calls = 0;

static void count_idle(void *ctx)
{
    int *limit = ctx;
    g_idle_calls++;
    if (*limit > 0 && g_idle_calls >= *limit) {
        listener_request_stop();
    }
}

/* 아무도 붙지 않는 동안 훅이 돈다. */
static void test_idle_while_accepting(void)
{
    listener_reset_stop();
    g_idle_calls = 0;

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);

    int limit = 3;
    listener_set_idle(ln, count_idle, &limit, 5);

    bool accepted = true;
    assert(listener_serve_one(ln, on_frame, NULL, &accepted) == 0);
    assert(!accepted); /* 접속을 받은 것이 아니라 멈춘 것이다 */
    assert(g_idle_calls >= 3);

    listener_close(ln);
    listener_reset_stop();
}

/* 접속이 열려 있어도 전문과 전문 사이에서 훅이 돈다. */
static void test_idle_between_frames(void)
{
    listener_reset_stop();
    g_idle_calls = 0;

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        listener_close(ln);
        int fd = dial(port);
        if (fd < 0) {
            _exit(1);
        }
        /* 붙기만 하고 잠시 가만히 있는다 — 그 사이에 훅이 돌아야 한다 */
        struct timespec nap = {.tv_sec = 0, .tv_nsec = 120000000};
        nanosleep(&nap, NULL);

        uint8_t f[WIRE_HEADER_LEN + MSG_ORDER_REQ_LEN];
        size_t  n = build_order_frame(f, sizeof(f), 77, 1, 9000);
        send_all(fd, f, n);

        uint8_t ack[WIRE_HEADER_LEN + MSG_ORDER_ACK_LEN];
        size_t  got = 0;
        while (got < sizeof(ack)) {
            ssize_t r = read(fd, ack + got, sizeof(ack) - got);
            if (r <= 0) {
                break;
            }
            got += (size_t)r;
        }
        close(fd);
        _exit(got == sizeof(ack) ? 0 : 2);
    }

    int limit = 0; /* 멈추라고 하지 않는다 — 자식이 끊으면 돌아온다 */
    listener_set_idle(ln, count_idle, &limit, 10);

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe.reply = true;

    int handled = listener_serve_one(ln, on_frame, &probe, NULL);
    assert(handled == 1);
    assert(g_idle_calls > 0);

    int status = 0;
    waitpid(pid, &status, 0);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    listener_close(ln);
    listener_reset_stop();
}

static void test_stop_before_serve(void)
{
    listener_reset_stop();
    listener_request_stop();
    assert(listener_stopping());

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);

    assert(listener_serve_one(ln, on_frame, NULL, NULL) == 0);
    assert(listener_run(ln, on_frame, NULL) == 0);

    listener_close(ln);
    listener_reset_stop();
    assert(!listener_stopping());
}

static void test_args(void)
{
    assert(listener_serve_one(NULL, on_frame, NULL, NULL) == ERR_NULL_PTR);
    assert(listener_run(NULL, on_frame, NULL) == ERR_NULL_PTR);
}

/*
 * 어느 단계에서 멈췄는지 바로 보이게 이름을 찍는다.
 *
 * 이 파일의 실패는 대부분 **멈춤**으로 나타난다 — 소켓 양쪽이 서로를 기다리면
 * assert가 터지지 않고 그냥 돌아오지 않는다. 그때 표준 출력이 비어 있으면
 * 어디서 멈췄는지 알 방법이 없다.
 */
#define STEP(fn)                                                            \
    do {                                                                   \
        fprintf(stderr, "[%s]\n", #fn);                                    \
        fn();                                                              \
    } while (0)

int main(void)
{
    STEP(test_open_and_port);
    STEP(test_reuseaddr_after_connection);
    STEP(test_receives_frames);
    STEP(test_rejects_bad_frames);
    STEP(test_truncated_frame);
    STEP(test_truncated_header);
    STEP(test_clean_disconnect);
    STEP(test_signal_stops_accept);
    STEP(test_idle_while_accepting);
    STEP(test_idle_between_frames);
    STEP(test_stop_before_serve);
    STEP(test_args);
    return 0;
}
