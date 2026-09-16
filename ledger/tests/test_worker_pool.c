/*
 * T3-04 사전 fork 워커 풀.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 요청 전에 워커 N개를 미리 만든다
 *  2. 워커가 공유 리스닝 소켓에서 각자 accept한다 — **분배가 실제로 일어난다**
 *  3. 워커가 죽으면 다시 띄운다
 *  4. SIGTERM에 전부 정리하고 **좀비를 남기지 않는다**
 *  5. 워커별 처리 건수를 보고한다
 *
 * 2번이 이 태스크의 핵심이다. "워커가 여럿 있다"가 아니라 "**일이 여럿에게
 * 나뉜다**"를 봐야 한다. 그래서 접속을 여러 건 보내고 **두 개 이상의 워커가
 * 실제로 일했는지** 확인한다.
 */
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"
#include "worker_pool.h"

#define STEP(fn)                                                            \
    do {                                                                   \
        fprintf(stderr, "[%s]\n", #fn);                                    \
        fn();                                                              \
    } while (0)

/* --- 훅: 주문에 ACK를 돌려준다 --- */

static int on_frame(const wire_header_t *hdr, const uint8_t *body, uint8_t *out,
                    size_t out_cap, void *ctx)
{
    (void)ctx;

    if (hdr->type != MSG_ORDER_REQ) {
        return 0;
    }

    msg_order_req_t req;
    if (msg_decode_order_req(body, hdr->body_len, &req) < 0) {
        return -1;
    }

    msg_order_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.cl_ord_id = req.cl_ord_id;
    ack.order_id = req.cl_ord_id + 1000;
    ack.status = STATUS_NEW;
    ack.price = req.price;

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

/* --- 클라이언트 --- */

static int dial(uint16_t port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

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

static size_t build_order(uint8_t *buf, size_t cap, uint64_t cl_ord_id)
{
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = MSG_ORDER_REQ;
    h.body_len = MSG_ORDER_REQ_LEN;
    h.seq = cl_ord_id;
    h.ts = 1000 + (int64_t)cl_ord_id;

    int n = wire_encode_header(&h, buf, cap);
    assert(n == (int)WIRE_HEADER_LEN);

    msg_order_req_t req;
    memset(&req, 0, sizeof(req));
    strcpy(req.account, "ACC-001");
    strcpy(req.symbol, "005930");
    req.cl_ord_id = cl_ord_id;
    req.side = SIDE_BUY;
    req.type = ORDER_LIMIT;
    req.market = MARKET_KRX;
    req.price = 10000;
    req.qty = 10;

    int m = msg_encode_order_req(&req, buf + n, cap - (size_t)n);
    assert(m == MSG_ORDER_REQ_LEN);
    return (size_t)(n + m);
}

/* 접속 하나를 열어 주문 하나를 보내고 ACK를 확인한 뒤 끊는다. */
static bool one_round_trip(uint16_t port, uint64_t cl_ord_id)
{
    int fd = dial(port);
    if (fd < 0) {
        return false;
    }

    uint8_t buf[WIRE_HEADER_LEN + MSG_ORDER_REQ_LEN];
    size_t  n = build_order(buf, sizeof(buf), cl_ord_id);

    size_t sent = 0;
    while (sent < n) {
        ssize_t w = write(fd, buf + sent, n - sent);
        if (w <= 0) {
            close(fd);
            return false;
        }
        sent += (size_t)w;
    }

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

    if (got != sizeof(ack)) {
        return false;
    }

    msg_order_ack_t a;
    if (msg_decode_order_ack(ack + WIRE_HEADER_LEN, MSG_ORDER_ACK_LEN, &a) < 0) {
        return false;
    }
    return a.cl_ord_id == cl_ord_id && a.order_id == cl_ord_id + 1000;
}

/* --- 1. 사전 fork --- */

/*
 * 완료 조건 1. **접속이 오기 전에 이미 워커가 있다.**
 *
 * `pool_start()`가 돌아온 직후 각 워커의 pid가 살아 있는지 본다. 요청마다
 * fork하는 구조라면 이 시점에 자식이 하나도 없다.
 */
static void test_prefork(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 64);
    assert(ln != NULL);

    pool_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.worker_count = 4;
    cfg.ln = ln;
    cfg.fn = on_frame;

    worker_pool_t *pool = pool_start(&cfg);
    assert(pool != NULL);
    assert(pool_worker_count(pool) == 4);

    /* 접속을 한 건도 보내지 않았는데 워커 넷이 살아 있다. */
    for (int32_t i = 0; i < 4; i++) {
        pid_t pid = pool_worker_pid(pool, i);
        assert(pid > 0);
        assert(kill(pid, 0) == 0); /* 시그널 0은 존재 확인만 한다 */
    }

    /* 워커끼리 pid가 겹치지 않는다. */
    for (int32_t i = 0; i < 4; i++) {
        for (int32_t j = i + 1; j < 4; j++) {
            assert(pool_worker_pid(pool, i) != pool_worker_pid(pool, j));
        }
    }

    pool_stop(pool);
    pool_destroy(pool);
    listener_close(ln);
}

/* --- 2. 분배 --- */

/*
 * 완료 조건 2·5의 핵심. **일이 여럿에게 나뉘는가.**
 *
 * 접속을 여러 건 보내고, 처리 건수의 합이 보낸 수와 같은지, 그리고 **두 개 이상의
 * 워커가 실제로 일했는지** 본다. 워커가 넷인데 한 워커가 전부 처리했다면 풀이
 * 있으나 마나다.
 *
 * 커널이 나누는 것이라 어느 워커가 몇 건을 받을지는 정해져 있지 않다. 그래서
 * "고르게"가 아니라 "둘 이상"을 본다 — 검사할 수 있는 것만 검사한다.
 */
static void test_work_is_distributed(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 64);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pool_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.worker_count = 4;
    cfg.ln = ln;
    cfg.fn = on_frame;

    worker_pool_t *pool = pool_start(&cfg);
    assert(pool != NULL);

    const int ROUNDS = 40;
    for (int i = 0; i < ROUNDS; i++) {
        assert(one_round_trip(port, (uint64_t)(i + 1)));
    }

    pool_stop(pool);

    /* 보낸 접속이 전부 처리됐다. */
    fprintf(stderr, "  total=%d (기대 %d)  ", pool_handled_total(pool), ROUNDS);
    for (int32_t i = 0; i < pool_worker_count(pool); i++) {
        fprintf(stderr, "w%d=%d ", i, pool_handled(pool, i));
    }
    fprintf(stderr, "\n");
    assert(pool_handled_total(pool) == ROUNDS);

    int32_t busy = 0;
    for (int32_t i = 0; i < pool_worker_count(pool); i++) {
        int32_t n = pool_handled(pool, i);
        assert(n >= 0);
        if (n > 0) {
            busy++;
        }
    }
    /* 한 워커가 전부 가져가지 않았다 — 풀이 실제로 일을 나눈다. */
    assert(busy >= 2);

    assert(pool_handled(pool, -1) == -1);
    assert(pool_handled(pool, 99) == -1);

    pool_destroy(pool);
    listener_close(ln);
}

/* --- 3. 워커가 죽으면 다시 띄운다 --- */

/*
 * 완료 조건 3. 워커 하나를 강제로 죽이고, 감시가 자리를 채우는지 본다.
 *
 * `SIGKILL`을 쓴다 — `SIGTERM`은 워커가 정상 종료로 받아들여 "사고로 죽었다"가
 * 되지 않는다.
 */
static void test_respawn_after_kill(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 64);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pool_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.worker_count = 3;
    cfg.ln = ln;
    cfg.fn = on_frame;

    worker_pool_t *pool = pool_start(&cfg);
    assert(pool != NULL);

    pid_t victim = pool_worker_pid(pool, 1);
    assert(victim > 0);
    assert(kill(victim, SIGKILL) == 0);

    /*
     * 감시를 돌린다. 부모는 `pool_supervise()` 안에서 자식을 거두며 자리를
     * 채운다. 잠깐 뒤에 멈춤을 걸어 루프를 빠져나오게 한다.
     */
    pid_t self = getpid();
    pid_t stopper = fork();
    assert(stopper >= 0);
    if (stopper == 0) {
        struct timespec ts = {0, 400 * 1000 * 1000}; /* 400ms */
        nanosleep(&ts, NULL);
        kill(self, SIGTERM);
        _exit(0);
    }

    assert(listener_install_signals() == ERR_OK);
    int restarts = pool_supervise(pool);

    int status = 0;
    assert(waitpid(stopper, &status, 0) == stopper);

    assert(restarts >= 1);
    assert(pool_restarts(pool) >= 1);

    /* 자리가 채워졌다 — 죽인 워커와 다른 pid가 들어와 있다. */
    pid_t replacement = pool_worker_pid(pool, 1);
    assert(replacement > 0);
    assert(replacement != victim);
    assert(kill(replacement, 0) == 0);

    /* 새 워커도 일한다. */
    listener_reset_stop();
    assert(one_round_trip(port, 7777));

    pool_stop(pool);
    assert(pool_handled_total(pool) >= 1);

    pool_destroy(pool);
    listener_close(ln);
}

/*
 * **재시작해도 이미 센 건수가 사라지지 않는다.**
 *
 * 워커를 하나만 둔다. 그래야 "누가 처리했는지"가 커널 분배에 좌우되지 않아
 * 숫자를 정확히 못 박을 수 있다.
 *
 *   접속 5건 -> 워커를 죽인다 -> 자리가 채워진다 -> 접속 3건 -> 합이 8이어야 한다
 *
 * 죽은 워커가 남긴 5건을 자리 채우기 전에 거두지 않으면, 또는 누적하지 않고
 * 덮어쓰면 여기서 3이 나온다.
 */
static void test_counts_survive_respawn(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 64);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pool_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.worker_count = 1;
    cfg.ln = ln;
    cfg.fn = on_frame;

    worker_pool_t *pool = pool_start(&cfg);
    assert(pool != NULL);

    for (int i = 0; i < 5; i++) {
        assert(one_round_trip(port, (uint64_t)(100 + i)));
    }

    pid_t victim = pool_worker_pid(pool, 0);
    assert(victim > 0);
    assert(kill(victim, SIGKILL) == 0);

    /* 감시를 잠깐 돌려 자리를 채운다. */
    pid_t self = getpid();
    pid_t stopper = fork();
    assert(stopper >= 0);
    if (stopper == 0) {
        struct timespec ts = {0, 400 * 1000 * 1000};
        nanosleep(&ts, NULL);
        kill(self, SIGTERM);
        _exit(0);
    }
    assert(listener_install_signals() == ERR_OK);
    assert(pool_supervise(pool) >= 1);

    int status = 0;
    assert(waitpid(stopper, &status, 0) == stopper);
    listener_reset_stop();

    pid_t replacement = pool_worker_pid(pool, 0);
    assert(replacement > 0 && replacement != victim);

    for (int i = 0; i < 3; i++) {
        assert(one_round_trip(port, (uint64_t)(200 + i)));
    }

    pool_stop(pool);

    /*
     * 죽기 전 5건 + 죽은 뒤 3건 = 8건.
     *
     * **딱 8이 아니라 7 이상을 본다.** `SIGKILL`은 유예가 없으므로, 워커가 막
     * 끝낸 접속의 1바이트를 파이프에 쓰기 직전에 죽으면 그 한 건은 보고되지
     * 못한다. 클라이언트는 응답을 받은 시점에 끝났다고 보지만 워커의 집계는
     * 그보다 조금 뒤에 끝나기 때문이다. **강제 종료가 줄 수 없는 보장을 테스트가
     * 요구하면 안 된다.**
     *
     * 7이라는 하한이 검사를 무디게 만들지는 않는다 — 죽기 전 집계를 통째로
     * 잃거나(3), 누적하지 않고 덮어쓰면(3) 여기서 걸린다.
     */
    fprintf(stderr, "  handled=%d (7~8 기대) restarts=%d\n",
            pool_handled(pool, 0), pool_restarts(pool));
    assert(pool_handled(pool, 0) >= 7);
    assert(pool_handled(pool, 0) <= 8);
    assert(pool_handled_total(pool) == pool_handled(pool, 0));

    pool_destroy(pool);
    listener_close(ln);
}

/* --- 4. 좀비를 남기지 않는다 --- */

/*
 * 완료 조건 4. `pool_stop()` 뒤에 거둘 자식이 하나도 없어야 한다.
 *
 * `waitpid(-1, WNOHANG)`이 `ECHILD`를 주면 자식이 없다는 뜻이다. 좀비가 남아
 * 있으면 그 pid가 돌아온다 — 거두지 않은 자식이 있다는 증거다.
 */
/*
 * **멈춤 신호는 유실될 수 있다.**
 *
 * 워커는 `listener_stopping()`을 확인하고 나서 `accept()`에 들어간다.
 * 그 사이에 SIGTERM이 도착하면 플래그만 서고 워커는 `accept()`에 잠긴다 —
 * 더 들어올 접속이 없으면 영원히 깨어나지 못하고 부모도 영원히 기다린다.
 *
 * 이 창은 좁아서 한 번 돌려서는 잘 안 걸린다. 고치기 전에 `test_no_zombies`가
 * **6번 중 2번** 멈췄다. 그래서 여기서는 시작과 정지를 스무 번 되풀이해
 * 창을 스무 번 연다 — 고치기 전이라면 거의 반드시 걸린다.
 *
 * 확률에 기대는 테스트라 마음에 들지 않지만, 이 경쟁을 결정적으로 만들려면
 * 워커 안에 시험용 지연을 심어야 한다. **시험을 위해 운영 코드에 구멍을
 * 내는 것**보다는 여러 번 돌리는 편이 낫다고 판단했다.
 *
 * 횟수는 재서 정했다. 고친 코드에서 재전송을 빼고 돌려 보니
 * **20회에서 8번 중 6번(75%)** 잡혔다 — 한 회당 약 6.7%다. 60회면 놓칠
 * 확률이 2% 아래로 떨어진다. 그래도 0은 아니므로, 이 테스트가 한 번
 * 통과했다고 경쟁이 없다고 말하면 안 된다.
 */
static void test_stop_is_not_racy(void)
{
    for (int32_t round = 0; round < 60; round++) {
        listener_reset_stop();

        listener_t *ln = listener_open(0, 64);
        assert(ln != NULL);
        uint16_t port = listener_port(ln);

        pool_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.worker_count = 3;
        cfg.ln = ln;
        cfg.fn = on_frame;

        worker_pool_t *pool = pool_start(&cfg);
        assert(pool != NULL);

        /*
         * 접속을 하나 처리하게 한다. **방금 접속을 끝낸 워커**가 멈춤 확인과
         * `accept()` 사이로 들어가는 바로 그 워커다.
         */
        assert(one_round_trip(port, 1));

        pool_stop(pool); /* 여기서 매달리면 ctest 시간 제한에 걸린다 */

        /*
         * **유예 종료로 끝나야 한다.** 매달리지 않는 것만으로는 부족하다 —
         * SIGKILL까지 가도 매달리지는 않기 때문이다. 그런데 SIGKILL당한 워커는
         * 방금 끝낸 접속의 집계 바이트를 잃을 수 있다. "안 멈춘다"와
         * "깨끗하게 멈춘다"는 다른 요구다.
         */
        assert(pool_forced_kills(pool) == 0);

        pool_destroy(pool);
        listener_close(ln);
    }
}

static void test_no_zombies(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 64);
    assert(ln != NULL);
    uint16_t port = listener_port(ln);

    pool_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.worker_count = 5;
    cfg.ln = ln;
    cfg.fn = on_frame;

    worker_pool_t *pool = pool_start(&cfg);
    assert(pool != NULL);

    pid_t pids[5];
    for (int32_t i = 0; i < 5; i++) {
        pids[i] = pool_worker_pid(pool, i);
    }

    assert(one_round_trip(port, 1));

    pool_stop(pool);

    /* 워커가 전부 사라졌다. */
    for (int32_t i = 0; i < 5; i++) {
        errno = 0;
        assert(kill(pids[i], 0) != 0);
        assert(errno == ESRCH);
    }

    /* 거둘 자식이 하나도 없다. */
    int   status = 0;
    errno = 0;
    pid_t leftover = waitpid(-1, &status, WNOHANG);
    assert(leftover == -1);
    assert(errno == ECHILD);

    /* 두 번 멈춰도 안전하다. */
    pool_stop(pool);
    pool_destroy(pool);
    listener_close(ln);
}

/* --- 5. 인자 --- */

static void test_args(void)
{
    listener_reset_stop();

    listener_t *ln = listener_open(0, 16);
    assert(ln != NULL);

    pool_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.ln = ln;
    cfg.fn = on_frame;

    assert(pool_start(NULL) == NULL);

    cfg.worker_count = 0;
    assert(pool_start(&cfg) == NULL);

    cfg.worker_count = -1;
    assert(pool_start(&cfg) == NULL);

    cfg.worker_count = POOL_WORKERS_MAX + 1;
    assert(pool_start(&cfg) == NULL);

    cfg.worker_count = 2;
    cfg.ln = NULL;
    assert(pool_start(&cfg) == NULL);

    assert(pool_worker_count(NULL) == 0);
    assert(pool_handled(NULL, 0) == -1);
    assert(pool_handled_total(NULL) == 0);
    assert(pool_restarts(NULL) == 0);
    assert(pool_worker_pid(NULL, 0) == -1);
    assert(pool_supervise(NULL) == ERR_NULL_PTR);
    pool_stop(NULL);
    pool_destroy(NULL);

    listener_close(ln);
}

int main(void)
{
    STEP(test_prefork);
    STEP(test_work_is_distributed);
    STEP(test_respawn_after_kill);
    STEP(test_counts_survive_respawn);
    STEP(test_no_zombies);
    STEP(test_stop_is_not_racy);
    STEP(test_args);
    return 0;
}
