/*
 * T3-11 FEP — 세션 로그인·하트비트·재접속.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 로그인 전에는 업무 전문을 보내지 않는다
 *  2. 로그인 왕복이 된다 (LOGIN_REQ 나가고 LOGIN_ACK 받으면 READY)
 *  3. 조용하면 하트비트가 나간다. **기준은 "보낸 지"다**
 *  4. 상대가 조용하면 끊는다. **기준은 "받은 지"다**
 *  5. 백오프가 커지고 상한에서 멈춘다
 *  6. 재접속 뒤 다시 주고받는다
 *
 * ===========================================================================
 * 이 파일에 `sleep`이 없다
 * ===========================================================================
 *
 * 세션이 시각을 **주입받기** 때문이다. 15초 뒤의 무응답 단절을 확인하려고
 * 15초를 기다리지 않는다 — `now`에 15000을 더해 넣으면 된다.
 *
 * 그래서 이 파일 전체가 한순간에 끝나고, 시계가 끼지 않으므로 **같은 입력에
 * 늘 같은 결과**가 나온다. 시계에 기대는 테스트는 부하가 있는 기계에서 가끔
 * 실패하고, 가끔 실패하는 테스트는 결국 아무도 안 믿게 된다.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "errors.h"
#include "evloop.h"
#include "session.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

/* 시작 시각. 0이 아니어야 "안 채워진 값"과 구분된다. */
#define T0 1000000

/*
 * `session_t`는 프레이머(64KB)와 송신 큐(256KB)를 품어 크다. 스택에 두지 않는다.
 */
static session_t g_s;

/* --- 도구 --- */

static void make_pair(int *ours, int *peer)
{
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    assert(evloop_set_nonblocking(sv[0]) == ERR_OK);
    assert(evloop_set_nonblocking(sv[1]) == ERR_OK);
    *ours = sv[0];
    *peer = sv[1];
}

/* 상대 쪽에서 전문 하나를 만들어 보낸다. */
static void peer_send(int peer, uint8_t type, const uint8_t *body,
                      uint32_t body_len, uint64_t seq)
{
    uint8_t frame[WIRE_HEADER_LEN + 256];
    assert(body_len <= sizeof(frame) - WIRE_HEADER_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = type;
    h.body_len = body_len;
    h.seq = seq;
    h.ts = 0;

    int n = wire_encode_header(&h, frame, sizeof(frame));
    assert(n == (int)WIRE_HEADER_LEN);
    if (body_len > 0) {
        memcpy(frame + WIRE_HEADER_LEN, body, body_len);
    }

    size_t  total = WIRE_HEADER_LEN + body_len;
    ssize_t w = write(peer, frame, total);
    assert(w == (ssize_t)total);
}

static void peer_send_login_ack(int peer, int32_t result)
{
    msg_login_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.result = result;

    uint8_t body[MSG_LOGIN_ACK_LEN];
    int     n = msg_encode_login_ack(&ack, body, sizeof(body));
    assert(n == MSG_LOGIN_ACK_LEN);

    peer_send(peer, MSG_LOGIN_ACK, body, (uint32_t)n, 1);
}

/* 상대가 받은 전문 하나를 꺼낸다. 없으면 0, 있으면 1. */
static int peer_recv(int peer, wire_header_t *hdr, uint8_t *body,
                     size_t body_cap)
{
    uint8_t hbuf[WIRE_HEADER_LEN];
    ssize_t r = read(peer, hbuf, sizeof(hbuf));
    if (r <= 0) {
        return 0;
    }
    assert(r == (ssize_t)WIRE_HEADER_LEN);
    assert(wire_decode_header(hbuf, (size_t)r, hdr) == (int)WIRE_HEADER_LEN);

    if (hdr->body_len > 0) {
        assert(hdr->body_len <= body_cap);
        r = read(peer, body, hdr->body_len);
        assert(r == (ssize_t)hdr->body_len);
    }
    return 1;
}

/* 세션을 READY까지 데려간다. */
static void bring_up(int *ours, int *peer, int64_t now)
{
    make_pair(ours, peer);

    assert(session_init(&g_s, NULL, "FEP-KRX-01", now) == ERR_OK);
    assert(session_state(&g_s) == SESSION_DOWN);
    assert(session_should_connect(&g_s, now)); /* 첫 접속은 기다리지 않는다 */

    assert(session_on_connected(&g_s, *ours, now) == ERR_OK);
    assert(session_state(&g_s) == SESSION_LOGGING_IN);

    /* LOGIN_REQ가 큐에 들어갔고, 내보내면 상대가 받는다. */
    assert(session_want_write(&g_s));
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       body[256];
    assert(peer_recv(*peer, &hdr, body, sizeof(body)) == 1);
    assert(hdr.type == MSG_LOGIN_REQ);

    msg_login_req_t req;
    assert(msg_decode_login_req(body, hdr.body_len, &req) == MSG_LOGIN_REQ_LEN);
    assert(strcmp(req.session_id, "FEP-KRX-01") == 0);

    peer_send_login_ack(*peer, ERR_OK);
    assert(session_on_readable(&g_s, NULL, NULL, now) == 1);
    assert(session_state(&g_s) == SESSION_READY);
}

/* 업무 전문 하나를 만든다(주문 요청). */
static size_t make_order_body(uint8_t *body, size_t cap, uint64_t cl_ord_id)
{
    msg_order_req_t m;
    memset(&m, 0, sizeof(m));
    /*
     * `strncpy`를 쓰면 12자를 12바이트 필드에 넣을 때 널이 안 들어가
     * `-Wstringop-truncation`에 걸린다. 여기서는 널 종료가 필요하므로
     * `snprintf`를 쓴다 — 전문에 실릴 때는 `wire_put_str`이 고정 길이로 자른다.
     */
    snprintf(m.account, sizeof(m.account), "%s", "123456789012");
    snprintf(m.symbol, sizeof(m.symbol), "%s", "005930");
    m.cl_ord_id = cl_ord_id;
    m.side = 1;
    m.type = 1;
    m.market = 1;
    m.price = 70000;
    m.qty = 10;

    int n = msg_encode_order_req(&m, body, cap);
    assert(n == MSG_ORDER_REQ_LEN);
    return (size_t)n;
}

/* --- 1. 로그인 전에는 업무 전문을 보내지 않는다 --- */

static void test_rejects_before_login(void)
{
    int ours;
    int peer;
    make_pair(&ours, &peer);

    int64_t now = T0;
    assert(session_init(&g_s, NULL, "FEP-KRX-01", now) == ERR_OK);

    uint8_t body[MSG_ORDER_REQ_LEN];
    size_t  n = make_order_body(body, sizeof(body), 1);

    /* 아직 붙지도 않았다. */
    assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) ==
           ERR_NOT_LOGGED_IN);

    /* 붙었지만 로그인 응답 전이다. **여기가 핵심이다.** */
    assert(session_on_connected(&g_s, ours, now) == ERR_OK);
    assert(session_state(&g_s) == SESSION_LOGGING_IN);
    assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) ==
           ERR_NOT_LOGGED_IN);

    /* 로그인되면 그때 받아 준다. */
    assert(session_on_writable(&g_s, now) > 0);
    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer, &hdr, got, sizeof(got)) == 1);
    peer_send_login_ack(peer, ERR_OK);
    assert(session_on_readable(&g_s, NULL, NULL, now) == 1);

    assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) == ERR_OK);

    close(ours);
    close(peer);
}

/* 상대가 로그인을 거절하면 READY가 되지 않는다. */
static void test_login_rejected(void)
{
    int ours;
    int peer;
    make_pair(&ours, &peer);

    int64_t now = T0;
    assert(session_init(&g_s, NULL, "FEP-KRX-01", now) == ERR_OK);
    assert(session_on_connected(&g_s, ours, now) == ERR_OK);
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer, &hdr, got, sizeof(got)) == 1);

    peer_send_login_ack(peer, ERR_NOT_SUPPORTED); /* 거절 */
    assert(session_on_readable(&g_s, NULL, NULL, now) == ERR_NOT_LOGGED_IN);
    assert(session_state(&g_s) == SESSION_DOWN);

    /* 거절도 실패다 — 백오프가 잡힌다. */
    assert(session_retry_in(&g_s, now) > 0);

    close(ours);
    close(peer);
}

/* --- 2. 로그인 왕복과 업무 전문 주고받기 --- */

typedef struct {
    int      calls;
    uint8_t  last_type;
    uint64_t last_cl_ord_id;
} sink_t;

static void on_frame(const wire_header_t *hdr, const uint8_t *body, void *ctx)
{
    sink_t *k = ctx;
    k->calls++;
    k->last_type = hdr->type;

    if (hdr->type == MSG_ORDER_ACK) {
        msg_order_ack_t ack;
        assert(msg_decode_order_ack(body, hdr->body_len, &ack) ==
               MSG_ORDER_ACK_LEN);
        k->last_cl_ord_id = ack.cl_ord_id;
    }
}

static void test_login_and_exchange(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    /* 업무 전문을 보낸다. */
    uint8_t body[MSG_ORDER_REQ_LEN];
    size_t  n = make_order_body(body, sizeof(body), 4242);
    assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) == ERR_OK);
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer, &hdr, got, sizeof(got)) == 1);
    assert(hdr.type == MSG_ORDER_REQ);
    /* 로그인 요청이 seq 1이었으므로 이것은 2다. */
    assert(hdr.seq == 2);

    /* 응답을 받는다. 업무 전문만 콜백까지 온다. */
    msg_order_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.cl_ord_id = 4242;
    ack.order_id = 7;
    ack.status = 1;
    ack.reason = ERR_OK;

    uint8_t abody[MSG_ORDER_ACK_LEN];
    assert(msg_encode_order_ack(&ack, abody, sizeof(abody)) ==
           MSG_ORDER_ACK_LEN);
    peer_send(peer, MSG_ORDER_ACK, abody, MSG_ORDER_ACK_LEN, 2);

    sink_t k;
    memset(&k, 0, sizeof(k));
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(k.calls == 1);
    assert(k.last_type == MSG_ORDER_ACK);
    assert(k.last_cl_ord_id == 4242);

    close(ours);
    close(peer);
}

/* 하트비트는 콜백까지 오지 않는다 — 세션이 삼킨다. */
static void test_heartbeat_not_delivered(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    peer_send(peer, MSG_HEARTBEAT, NULL, 0, 2);

    sink_t k;
    memset(&k, 0, sizeof(k));
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(k.calls == 0); /* 처리는 됐지만 위로 올리지 않았다 */
    assert(session_state(&g_s) == SESSION_READY);

    close(ours);
    close(peer);
}

/* --- 3. 하트비트는 "보낸 지"가 기준이다 --- */

static void test_heartbeat_on_idle(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    session_config_t cfg;
    session_config_default(&cfg);

    /* 아직 이르다. */
    assert(session_tick(&g_s, now + cfg.heartbeat_ms - 1) == 0);
    assert(!session_want_write(&g_s));

    /* 이제 때가 됐다. */
    now += cfg.heartbeat_ms;
    assert(session_tick(&g_s, now) == 1);
    assert(session_want_write(&g_s));
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer, &hdr, got, sizeof(got)) == 1);
    assert(hdr.type == MSG_HEARTBEAT);
    assert(hdr.body_len == 0); /* 바디 없는 전문이 왕복한다 */

    /* 보냈으니 시계가 다시 돌아간다. */
    assert(session_tick(&g_s, now + 1) == 0);

    close(ours);
    close(peer);
}

/*
 * **업무 전문을 보내는 중이면 하트비트를 덧붙이지 않는다.**
 * 기준이 "받은 지"였다면 여기서 쓸데없는 하트비트가 나간다.
 */
static void test_no_heartbeat_when_sending(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    session_config_t cfg;
    session_config_default(&cfg);

    uint8_t body[MSG_ORDER_REQ_LEN];
    size_t  n = make_order_body(body, sizeof(body), 1);

    /*
     * 하트비트 주기에 거의 닿을 만큼 시간을 보내되, 그 사이 계속 주문을 보낸다.
     * 상대는 아무 말도 하지 않지만 무응답 한계 안쪽이다.
     */
    for (int i = 0; i < 3; i++) {
        now += cfg.heartbeat_ms - 1;
        assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) == ERR_OK);
        assert(session_tick(&g_s, now) == 0); /* 하트비트가 나가지 않는다 */
        assert(session_on_writable(&g_s, now) > 0);

        /* 상대가 살아 있음을 알려 무응답 단절을 피한다. */
        peer_send(peer, MSG_HEARTBEAT, NULL, 0, (uint64_t)(i + 2));
        assert(session_on_readable(&g_s, NULL, NULL, now) == 1);
    }

    /* 나간 것은 주문뿐이다. 하트비트는 하나도 없다. */
    wire_header_t hdr;
    uint8_t       got[256];
    int           orders = 0;
    while (peer_recv(peer, &hdr, got, sizeof(got)) == 1) {
        assert(hdr.type != MSG_HEARTBEAT);
        if (hdr.type == MSG_ORDER_REQ) {
            orders++;
        }
    }
    assert(orders == 3);

    close(ours);
    close(peer);
}

/* --- 4. 무응답 단절은 "받은 지"가 기준이다 --- */

static void test_idle_timeout(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    session_config_t cfg;
    session_config_default(&cfg);

    /* 한계 직전까지는 살아 있다(하트비트는 나간다). */
    int rc = session_tick(&g_s, now + cfg.idle_timeout_ms - 1);
    assert(rc >= 0);
    assert(session_state(&g_s) == SESSION_READY);

    /* 한계를 넘으면 끊는다. */
    now += cfg.idle_timeout_ms;
    assert(session_tick(&g_s, now) == ERR_IO);
    assert(session_state(&g_s) == SESSION_DOWN);

    close(ours);
    close(peer);
}

/*
 * **내가 아무리 보내도 상대가 조용하면 끊긴다.**
 * 기준이 "보낸 지"였다면 죽은 상대를 살아 있다고 보게 된다.
 */
static void test_sending_does_not_keep_alive(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    session_config_t cfg;
    session_config_default(&cfg);

    uint8_t body[MSG_ORDER_REQ_LEN];
    size_t  n = make_order_body(body, sizeof(body), 1);

    /* 쉬지 않고 보내지만 상대는 한마디도 하지 않는다. */
    bool dropped = false;
    for (int i = 0; i < 100; i++) {
        now += 1000;
        if (session_send(&g_s, MSG_ORDER_REQ, body, n, now) != ERR_OK) {
            break;
        }
        (void)session_on_writable(&g_s, now);
        if (session_tick(&g_s, now) == ERR_IO) {
            dropped = true;
            break;
        }
    }
    assert(dropped);
    assert(session_state(&g_s) == SESSION_DOWN);
    assert(now - T0 >= cfg.idle_timeout_ms);

    close(ours);
    close(peer);
}

/* 받으면 살아난다 — 하트비트든 업무 전문이든. */
static void test_receiving_keeps_alive(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    session_config_t cfg;
    session_config_default(&cfg);

    /* 무응답 한계 직전마다 상대가 하트비트를 보낸다. 열 번 반복해도 살아 있다. */
    for (int i = 0; i < 10; i++) {
        now += cfg.idle_timeout_ms - 1;
        peer_send(peer, MSG_HEARTBEAT, NULL, 0, (uint64_t)(i + 2));
        assert(session_on_readable(&g_s, NULL, NULL, now) == 1);
        assert(session_tick(&g_s, now) >= 0);
        assert(session_state(&g_s) == SESSION_READY);

        /* 우리가 보낸 하트비트는 상대 쪽에 쌓이므로 비워 준다. */
        assert(session_on_writable(&g_s, now) >= 0);
        wire_header_t hdr;
        uint8_t       got[256];
        while (peer_recv(peer, &hdr, got, sizeof(got)) == 1) {
        }
    }

    close(ours);
    close(peer);
}

/* 로그인 응답이 안 오면 끊는다. */
static void test_login_timeout(void)
{
    int ours;
    int peer;
    make_pair(&ours, &peer);

    int64_t          now = T0;
    session_config_t cfg;
    session_config_default(&cfg);

    assert(session_init(&g_s, &cfg, "FEP-KRX-01", now) == ERR_OK);
    assert(session_on_connected(&g_s, ours, now) == ERR_OK);

    assert(session_tick(&g_s, now + cfg.login_timeout_ms - 1) == 0);
    assert(session_state(&g_s) == SESSION_LOGGING_IN);

    assert(session_tick(&g_s, now + cfg.login_timeout_ms) == ERR_NOT_LOGGED_IN);
    assert(session_state(&g_s) == SESSION_DOWN);

    close(ours);
    close(peer);
}

/* --- 5. 백오프 --- */

/*
 * **소켓이 하나도 나오지 않는다.** 백오프는 순수한 산술이라 접속 없이
 * 전부 확인할 수 있다 — 세션이 소켓을 만들지 않기로 한 덕이다.
 */
static void test_backoff_grows_and_caps(void)
{
    session_config_t cfg;
    session_config_default(&cfg);
    /*
     * **상한을 하한의 2의 거듭제곱 배수로 두면 안 된다.** 100에서 1600이면
     * 배증이 정확히 상한에 떨어져 **자르는 코드가 한 번도 실행되지 않는다** —
     * 자르기를 통째로 지워도 테스트가 통과한다(X4가 그렇게 살아남았다).
     * 1000으로 두면 800의 다음이 1600이 되어 반드시 잘라야 한다.
     */
    cfg.backoff_min_ms = 100;
    cfg.backoff_max_ms = 1000;

    int64_t now = T0;
    assert(session_init(&g_s, &cfg, "FEP-KRX-01", now) == ERR_OK);

    /* 100 -> 200 -> 400 -> 800 -> (1600이 아니라) 1000 -> 1000 -> ... */
    const int64_t want[] = {100, 200, 400, 800, 1000, 1000, 1000};

    for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); i++) {
        session_drop(&g_s, now);
        assert(session_state(&g_s) == SESSION_DOWN);

        /* 대기 중에는 붙지 않는다. */
        assert(!session_should_connect(&g_s, now));
        assert(session_retry_in(&g_s, now) == want[i]);
        assert(!session_should_connect(&g_s, now + want[i] - 1));

        /* 때가 되면 붙는다. */
        now += want[i];
        assert(session_should_connect(&g_s, now));
        assert(session_retry_in(&g_s, now) == 0);

        assert(g_s.attempts == (int32_t)i + 1);
    }
}

/*
 * 배증이 int32를 넘치는 경우. 넘치면 음수가 되어 **대기 시간이 과거가 된다** —
 * 그러면 끊기자마자 쉬지 않고 다시 붙어 상대를 두들긴다.
 */
static void test_backoff_overflow(void)
{
    session_config_t cfg;
    session_config_default(&cfg);
    cfg.backoff_min_ms = 2000000000; /* 두 배면 int32를 넘는다 */
    cfg.backoff_max_ms = 2100000000;

    int64_t now = T0;
    assert(session_init(&g_s, &cfg, "FEP-KRX-01", now) == ERR_OK);

    session_drop(&g_s, now);
    assert(session_retry_in(&g_s, now) == cfg.backoff_min_ms);

    session_drop(&g_s, now);
    /* 넘쳐서 음수가 되는 대신 상한에 붙어야 한다. */
    assert(g_s.backoff_ms == cfg.backoff_max_ms);
    assert(session_retry_in(&g_s, now) == cfg.backoff_max_ms);
    assert(!session_should_connect(&g_s, now));
}

/* 로그인에 성공하면 백오프가 처음으로 돌아간다. */
static void test_backoff_resets_on_login(void)
{
    session_config_t cfg;
    session_config_default(&cfg);
    cfg.backoff_min_ms = 100;
    cfg.backoff_max_ms = 1600;

    int64_t now = T0;
    assert(session_init(&g_s, &cfg, "FEP-KRX-01", now) == ERR_OK);

    /* 세 번 실패해 백오프를 키운다. */
    for (int i = 0; i < 3; i++) {
        session_drop(&g_s, now);
        now += 10000;
    }
    assert(g_s.attempts == 3);
    assert(g_s.backoff_ms == 800);

    /* 붙어서 로그인까지 성공한다. */
    int ours;
    int peer;
    make_pair(&ours, &peer);
    assert(session_on_connected(&g_s, ours, now) == ERR_OK);
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer, &hdr, got, sizeof(got)) == 1);

    /*
     * **붙은 시점이 아니라 로그인된 시점에 되돌린다.** TCP만 받아 주고 로그인을
     * 거절하는 상대에게 백오프 없이 달려들면 안 된다.
     */
    assert(g_s.attempts == 3);
    assert(g_s.backoff_ms == 800);

    peer_send_login_ack(peer, ERR_OK);
    assert(session_on_readable(&g_s, NULL, NULL, now) == 1);

    assert(g_s.attempts == 0);
    assert(g_s.backoff_ms == cfg.backoff_min_ms);

    close(ours);
    close(peer);
}

/* --- 6. 재접속 뒤 다시 주고받는다 --- */

static void test_reconnect_and_resume(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    uint64_t seq_before = g_s.out_seq;

    /* 상대가 사라진다. */
    close(peer);
    assert(session_on_readable(&g_s, NULL, NULL, now) == ERR_IO);
    assert(session_state(&g_s) == SESSION_DOWN);
    close(ours);

    /* 백오프만큼 기다렸다 다시 붙는다. */
    now += session_retry_in(&g_s, now);
    assert(session_should_connect(&g_s, now));

    int ours2;
    int peer2;
    make_pair(&ours2, &peer2);
    assert(session_on_connected(&g_s, ours2, now) == ERR_OK);
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer2, &hdr, got, sizeof(got)) == 1);
    assert(hdr.type == MSG_LOGIN_REQ);
    /*
     * **시퀀스는 접속이 바뀌어도 이어진다.** 다시 1부터 시작하면 상대가
     * 갭을 감지할 수 없다(T3-12의 재료다).
     */
    assert(hdr.seq == seq_before);

    peer_send_login_ack(peer2, ERR_OK);
    assert(session_on_readable(&g_s, NULL, NULL, now) == 1);
    assert(session_state(&g_s) == SESSION_READY);

    /* 다시 업무가 흐른다. */
    uint8_t body[MSG_ORDER_REQ_LEN];
    size_t  n = make_order_body(body, sizeof(body), 99);
    assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) == ERR_OK);
    assert(session_on_writable(&g_s, now) > 0);
    assert(peer_recv(peer2, &hdr, got, sizeof(got)) == 1);
    assert(hdr.type == MSG_ORDER_REQ);

    close(ours2);
    close(peer2);
}

/*
 * 앞 접속에 반쯤 와 있던 조각을 물려받지 않는다. 물려받으면 새 접속의 첫
 * 전문이 앞 조각에 이어 붙어 엉뚱하게 해석된다.
 */
static void test_reconnect_clears_partial(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    /* 헤더 절반만 보내 놓고 끊는다. */
    uint8_t half[12] = {'M', 'S', WIRE_VERSION, MSG_ORDER_ACK, 0, 0, 0, 0};
    assert(write(peer, half, sizeof(half)) == (ssize_t)sizeof(half));
    assert(session_on_readable(&g_s, NULL, NULL, now) == 0);
    assert(framer_pending(&g_s.rx) == sizeof(half));

    session_drop(&g_s, now);
    close(ours);
    close(peer);

    assert(framer_pending(&g_s.rx) == 0); /* 조각이 남지 않았다 */

    /* 새 접속에서 멀쩡한 전문이 그대로 해석된다. */
    now += session_retry_in(&g_s, now);
    int ours2;
    int peer2;
    make_pair(&ours2, &peer2);
    assert(session_on_connected(&g_s, ours2, now) == ERR_OK);
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer2, &hdr, got, sizeof(got)) == 1);
    peer_send_login_ack(peer2, ERR_OK);
    assert(session_on_readable(&g_s, NULL, NULL, now) == 1);
    assert(session_state(&g_s) == SESSION_READY);

    close(ours2);
    close(peer2);
}

/*
 * **길이가 규격과 다른 업무 전문은 받지 않는다.**
 *
 * 조립기(T3-09)는 헤더가 말한 만큼 모았을 뿐, 그 길이가 그 종별에 옳은지는
 * 모른다. 대조하지 않으면 한 바이트 짧은 ORDER_ACK를 그대로 디코딩하려 들고,
 * 필드가 밀린 값을 **그럴듯하게** 위로 올린다 — 주문 번호가 한 바이트 어긋난
 * 체결 응답보다 끊기는 쪽이 낫다.
 */
static void test_wrong_body_len_drops(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    /* 내용은 멀쩡하지만 길이가 한 바이트 짧다. */
    uint8_t abody[MSG_ORDER_ACK_LEN];
    memset(abody, 0, sizeof(abody));
    peer_send(peer, MSG_ORDER_ACK, abody, MSG_ORDER_ACK_LEN - 1, 2);

    sink_t k;
    memset(&k, 0, sizeof(k));
    assert(session_on_readable(&g_s, on_frame, &k, now) == ERR_INVALID_ARG);
    assert(k.calls == 0); /* 위로 올리지 않았다 */
    assert(session_state(&g_s) == SESSION_DOWN);

    close(ours);
    close(peer);
}

/* 긴 쪽도 마찬가지다. 규격이 다른 상대다. */
static void test_too_long_body_drops(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    uint8_t abody[MSG_ORDER_ACK_LEN + 1];
    memset(abody, 0, sizeof(abody));
    peer_send(peer, MSG_ORDER_ACK, abody, MSG_ORDER_ACK_LEN + 1, 2);

    sink_t k;
    memset(&k, 0, sizeof(k));
    assert(session_on_readable(&g_s, on_frame, &k, now) == ERR_INVALID_ARG);
    assert(k.calls == 0);
    assert(session_state(&g_s) == SESSION_DOWN);

    close(ours);
    close(peer);
}

/* 어긋난 스트림은 재동기하지 않고 끊는다(T3-09의 판단 그대로). */
static void test_garbage_drops(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    uint8_t junk[64];
    memset(junk, 0x5A, sizeof(junk));
    assert(write(peer, junk, sizeof(junk)) == (ssize_t)sizeof(junk));

    assert(session_on_readable(&g_s, NULL, NULL, now) < 0);
    assert(session_state(&g_s) == SESSION_DOWN);

    close(ours);
    close(peer);
}

/* ===================================================================== */
/* --- T3-12 전 구간: 갭 감지 · 재전송 요청 · 채우기 --- */
/* ===================================================================== */

/* 상대가 받은 전문을 종별이 맞을 때까지 훑는다. 없으면 0. */
static int peer_wait_type(int peer, uint8_t want, wire_header_t *hdr,
                          uint8_t *body, size_t cap)
{
    while (peer_recv(peer, hdr, body, cap) == 1) {
        if (hdr->type == want) {
            return 1;
        }
    }
    return 0;
}

static void peer_send_order_ack(int peer, uint64_t cl_ord_id, uint64_t seq)
{
    msg_order_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.cl_ord_id = cl_ord_id;
    ack.order_id = cl_ord_id + 1000;
    ack.status = 1;
    ack.reason = ERR_OK;

    uint8_t abody[MSG_ORDER_ACK_LEN];
    assert(msg_encode_order_ack(&ack, abody, sizeof(abody)) ==
           MSG_ORDER_ACK_LEN);
    peer_send(peer, MSG_ORDER_ACK, abody, MSG_ORDER_ACK_LEN, seq);
}

/*
 * **갭을 만나면 재전송을 요청하고, 그동안 오는 것은 위로 올리지 않는다.**
 * 그리고 채워지면 막혔던 흐름이 다시 흐른다.
 */
static void test_gap_requests_resend(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    /* 로그인 응답이 seq 1이었으므로 다음 기대는 2다. */
    assert(session_expected_seq(&g_s) == 2);
    assert(!session_recovering(&g_s));

    sink_t k;
    memset(&k, 0, sizeof(k));

    /* 2는 제대로 온다. */
    peer_send_order_ack(peer, 100, 2);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(k.calls == 1);
    assert(k.last_cl_ord_id == 100);

    /* 3, 4가 빠지고 5가 왔다. */
    peer_send_order_ack(peer, 500, 5);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(session_recovering(&g_s));
    assert(session_gaps(&g_s) == 1);
    /* **위로 올리지 않았다.** 순서가 뒤바뀐 채 올라가면 안 된다. */
    assert(k.calls == 1);
    assert(session_expected_seq(&g_s) == 3);

    /* 재전송 요청이 나갔는지 본다. */
    assert(session_on_writable(&g_s, now) > 0);
    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_wait_type(peer, MSG_RESEND_REQ, &hdr, got, sizeof(got)) == 1);

    msg_resend_req_t req;
    assert(msg_decode_resend_req(got, hdr.body_len, &req) ==
           MSG_RESEND_REQ_LEN);
    assert(req.from_seq == 3); /* 빠진 첫 번호부터 */

    /* 메우는 중에 온 것도 버린다. 그리고 요청을 또 보내지 않는다. */
    peer_send_order_ack(peer, 600, 6);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(k.calls == 1);
    assert(session_gaps(&g_s) == 1);
    assert(session_on_writable(&g_s, now) >= 0);
    assert(peer_wait_type(peer, MSG_RESEND_REQ, &hdr, got, sizeof(got)) == 0);

    /* 상대가 3부터 다시 보낸다. */
    peer_send_order_ack(peer, 300, 3);
    peer_send_order_ack(peer, 400, 4);
    peer_send_order_ack(peer, 500, 5);
    peer_send_order_ack(peer, 600, 6);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 4);

    /* **네 건 모두 순서대로 올라왔다.** */
    assert(k.calls == 5);
    assert(k.last_cl_ord_id == 600);
    assert(!session_recovering(&g_s));
    assert(session_expected_seq(&g_s) == 7);

    close(ours);
    close(peer);
}

/* 중복은 버린다 — 재전송은 겹쳐 온다 */
static void test_duplicate_dropped(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    sink_t k;
    memset(&k, 0, sizeof(k));

    peer_send_order_ack(peer, 100, 2);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(k.calls == 1);

    /* 같은 번호가 또 온다. */
    peer_send_order_ack(peer, 100, 2);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(k.calls == 1); /* **두 번 올라가지 않았다** */
    assert(session_dups(&g_s) == 1);
    assert(session_expected_seq(&g_s) == 3);

    close(ours);
    close(peer);
}

/* 상대가 "그 앞은 없다"고 하면 건너뛰고 다시 흐른다 */
static void test_gap_fill_unblocks(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    sink_t k;
    memset(&k, 0, sizeof(k));

    /* 2~9가 빠지고 10이 왔다. */
    peer_send_order_ack(peer, 999, 10);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(session_recovering(&g_s));
    assert(k.calls == 0);

    /* 상대가 그 구간을 더 갖고 있지 않다고 답한다. */
    msg_gap_fill_t gf;
    memset(&gf, 0, sizeof(gf));
    gf.next_seq = 10;
    uint8_t gbody[MSG_GAP_FILL_LEN];
    assert(msg_encode_gap_fill(&gf, gbody, sizeof(gbody)) == MSG_GAP_FILL_LEN);
    peer_send(peer, MSG_GAP_FILL, gbody, MSG_GAP_FILL_LEN, 99);

    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(!session_recovering(&g_s));
    assert(session_expected_seq(&g_s) == 10);

    /* 막혔던 흐름이 다시 흐른다. */
    peer_send_order_ack(peer, 1000, 10);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(k.calls == 1);
    assert(k.last_cl_ord_id == 1000);

    close(ours);
    close(peer);
}

/* 우리가 요청받는 쪽: 보관한 것을 원래 번호 그대로 다시 보낸다 */
static void test_serves_resend(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    uint8_t body[MSG_ORDER_REQ_LEN];
    for (uint64_t i = 0; i < 5; i++) {
        size_t n = make_order_body(body, sizeof(body), 10 + i);
        assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) == ERR_OK);
    }
    assert(session_on_writable(&g_s, now) > 0);

    /* 상대가 받은 것을 전부 비운다. */
    wire_header_t hdr;
    uint8_t       got[256];
    while (peer_recv(peer, &hdr, got, sizeof(got)) == 1) {
    }

    /* 상대가 "3번부터 다시"라고 한다(로그인 요청이 1, 주문이 2~6). */
    msg_resend_req_t req;
    memset(&req, 0, sizeof(req));
    req.from_seq = 3;
    uint8_t rbody[MSG_RESEND_REQ_LEN];
    assert(msg_encode_resend_req(&req, rbody, sizeof(rbody)) ==
           MSG_RESEND_REQ_LEN);
    peer_send(peer, MSG_RESEND_REQ, rbody, MSG_RESEND_REQ_LEN, 2);

    assert(session_on_readable(&g_s, NULL, NULL, now) == 1);
    assert(session_on_writable(&g_s, now) > 0);

    /* 3, 4, 5, 6이 **원래 번호 그대로** 다시 온다. */
    uint64_t want = 3;
    int      seen = 0;
    while (peer_recv(peer, &hdr, got, sizeof(got)) == 1) {
        if (hdr.type != MSG_ORDER_REQ) {
            continue;
        }
        assert(hdr.seq == want); /* 새 번호를 매기면 상대가 영영 못 메운다 */
        want++;
        seen++;
    }
    assert(seen == 4);

    close(ours);
    close(peer);
}

/*
 * **보관에 없는 구간을 요청받으면 `GAP_FILL`로 알린다.**
 * 알리지 않으면 상대는 오지 않을 번호를 영원히 기다린다 — 양쪽 다 살아 있는데
 * 아무것도 흐르지 않는 접속이 된다.
 */
static void test_resend_beyond_store(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    /* 보관 고리를 한 바퀴 넘게 채운다. 앞쪽은 밀려 나간다. */
    uint8_t body[MSG_ORDER_REQ_LEN];
    for (int i = 0; i < SEQSTORE_KEEP + 50; i++) {
        size_t n = make_order_body(body, sizeof(body), (uint64_t)i);
        assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) == ERR_OK);
        assert(session_on_writable(&g_s, now) >= 0);

        wire_header_t hdr2;
        uint8_t       got2[256];
        while (peer_recv(peer, &hdr2, got2, sizeof(got2)) == 1) {
        }
    }

    /* 1번은 이미 밀려 나갔다. */
    msg_resend_req_t req;
    memset(&req, 0, sizeof(req));
    req.from_seq = 1;
    uint8_t rbody[MSG_RESEND_REQ_LEN];
    assert(msg_encode_resend_req(&req, rbody, sizeof(rbody)) ==
           MSG_RESEND_REQ_LEN);
    peer_send(peer, MSG_RESEND_REQ, rbody, MSG_RESEND_REQ_LEN, 2);

    assert(session_on_readable(&g_s, NULL, NULL, now) == 1);
    assert(session_on_writable(&g_s, now) > 0);

    /* **GAP_FILL이 나와야 한다.** */
    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_wait_type(peer, MSG_GAP_FILL, &hdr, got, sizeof(got)) == 1);

    msg_gap_fill_t gf;
    assert(msg_decode_gap_fill(got, hdr.body_len, &gf) == MSG_GAP_FILL_LEN);
    assert(gf.next_seq > 1); /* 여기부터 있다 */

    close(ours);
    close(peer);
}

/*
 * 재접속 뒤 **끊겨 있는 동안 상대가 보낸 것**을 갭으로 잡는다.
 * 이것이 이 태스크가 존재하는 이유다 — 한 접속 안에서는 TCP가 갭을 막는다.
 */
static void test_gap_across_reconnect(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    sink_t k;
    memset(&k, 0, sizeof(k));

    peer_send_order_ack(peer, 100, 2);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(session_expected_seq(&g_s) == 3);

    /* 끊긴다. 그 사이 상대는 3~20을 보냈지만 어디에도 남지 않는다. */
    close(peer);
    assert(session_on_readable(&g_s, NULL, NULL, now) == ERR_IO);
    close(ours);

    /* **기대값은 살아남는다.** 지우면 놓친 것을 영영 모른다. */
    assert(session_expected_seq(&g_s) == 3);
    /* 앞 접속에서 요청한 재전송은 그 접속과 함께 사라졌다. */
    assert(!session_recovering(&g_s));

    now += session_retry_in(&g_s, now);
    int ours2;
    int peer2;
    make_pair(&ours2, &peer2);
    assert(session_on_connected(&g_s, ours2, now) == ERR_OK);
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer2, &hdr, got, sizeof(got)) == 1);
    assert(hdr.type == MSG_LOGIN_REQ);

    /*
     * 상대의 로그인 응답은 **이미 갭 너머의 번호**(21)를 달고 온다.
     * 이것을 시퀀스로 막으면 로그인이 끝나지 않고, 로그인이 안 끝나면
     * 재전송도 못 받는다 — 갭에서 빠져나오는 열쇠를 갭 안에 가두는 셈이다.
     */
    msg_login_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.result = ERR_OK;
    uint8_t abody[MSG_LOGIN_ACK_LEN];
    assert(msg_encode_login_ack(&ack, abody, sizeof(abody)) ==
           MSG_LOGIN_ACK_LEN);
    peer_send(peer2, MSG_LOGIN_ACK, abody, MSG_LOGIN_ACK_LEN, 21);

    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    /* **로그인은 됐고**, 동시에 갭이 잡혔다. */
    assert(session_state(&g_s) == SESSION_READY);
    assert(session_recovering(&g_s));
    assert(session_gaps(&g_s) == 1);
    assert(session_expected_seq(&g_s) == 3);

    /* 3부터 다시 달라는 요청이 나간다. */
    assert(session_on_writable(&g_s, now) > 0);
    assert(peer_wait_type(peer2, MSG_RESEND_REQ, &hdr, got, sizeof(got)) == 1);
    msg_resend_req_t req;
    assert(msg_decode_resend_req(got, hdr.body_len, &req) ==
           MSG_RESEND_REQ_LEN);
    assert(req.from_seq == 3);

    close(ours2);
    close(peer2);
}

/*
 * **메우는 중에 끊기면, 다시 붙을 때 메우기를 풀어야 한다.**
 *
 * 앞 접속에서 보낸 재전송 요청은 그 접속과 함께 사라졌다. "메우는 중"을
 * 그대로 들고 가면 새 접속에서 갭을 봐도 `SEQ_WAIT`으로 삼켜 **다시는
 * 요청하지 않으면서 영원히 기다린다.** 양쪽 다 살아 있는데 아무것도 흐르지
 * 않는 접속이 된다.
 */
static void test_recovering_cleared_on_reconnect(void)
{
    int     ours;
    int     peer;
    int64_t now = T0;
    bring_up(&ours, &peer, now);

    sink_t k;
    memset(&k, 0, sizeof(k));

    /* 2는 오고, 3·4가 빠진 채 5가 온다 -> 메우기 시작. */
    peer_send_order_ack(peer, 100, 2);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    peer_send_order_ack(peer, 500, 5);
    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(session_recovering(&g_s));
    assert(session_expected_seq(&g_s) == 3);

    /* 재전송이 오기 전에 끊긴다. */
    close(peer);
    assert(session_on_readable(&g_s, NULL, NULL, now) == ERR_IO);
    close(ours);

    /* **메우기가 풀려 있어야 한다.** 기대값은 그대로 3이다. */
    assert(!session_recovering(&g_s));
    assert(session_expected_seq(&g_s) == 3);

    now += session_retry_in(&g_s, now);
    int ours2;
    int peer2;
    make_pair(&ours2, &peer2);
    assert(session_on_connected(&g_s, ours2, now) == ERR_OK);
    assert(session_on_writable(&g_s, now) > 0);

    wire_header_t hdr;
    uint8_t       got[256];
    assert(peer_recv(peer2, &hdr, got, sizeof(got)) == 1);
    assert(hdr.type == MSG_LOGIN_REQ);

    /* 상대가 갭 너머 번호로 로그인 응답을 준다. */
    msg_login_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.result = ERR_OK;
    uint8_t abody[MSG_LOGIN_ACK_LEN];
    assert(msg_encode_login_ack(&ack, abody, sizeof(abody)) ==
           MSG_LOGIN_ACK_LEN);
    peer_send(peer2, MSG_LOGIN_ACK, abody, MSG_LOGIN_ACK_LEN, 30);

    assert(session_on_readable(&g_s, on_frame, &k, now) == 1);
    assert(session_state(&g_s) == SESSION_READY);

    /*
     * **새 요청이 나가야 한다.** 메우기가 안 풀렸다면 이 갭은 SEQ_WAIT으로
     * 삼켜지고 요청이 하나도 나가지 않는다 — 여기서 걸린다.
     */
    assert(session_recovering(&g_s));
    assert(session_on_writable(&g_s, now) > 0);
    assert(peer_wait_type(peer2, MSG_RESEND_REQ, &hdr, got, sizeof(got)) == 1);

    msg_resend_req_t req;
    assert(msg_decode_resend_req(got, hdr.body_len, &req) ==
           MSG_RESEND_REQ_LEN);
    assert(req.from_seq == 3);

    close(ours2);
    close(peer2);
}

/* --- 인자와 상태 --- */

static void test_args(void)
{
    int64_t now = T0;

    assert(session_init(NULL, NULL, "x", now) == ERR_NULL_PTR);
    assert(session_init(&g_s, NULL, NULL, now) == ERR_NULL_PTR);
    assert(session_init(&g_s, NULL, "", now) == ERR_NULL_PTR);

    session_config_t bad;
    session_config_default(&bad);
    bad.backoff_max_ms = bad.backoff_min_ms - 1; /* 상한이 하한보다 작다 */
    assert(session_init(&g_s, &bad, "x", now) == ERR_INVALID_ARG);

    session_config_default(&bad);
    bad.heartbeat_ms = 0;
    assert(session_init(&g_s, &bad, "x", now) == ERR_INVALID_ARG);

    assert(session_init(&g_s, NULL, "FEP-KRX-01", now) == ERR_OK);
    assert(session_on_connected(&g_s, -1, now) == ERR_INVALID_ARG);

    /* DOWN에서는 읽기·쓰기가 할 일이 없다. */
    assert(session_on_readable(&g_s, NULL, NULL, now) == ERR_NOT_LOGGED_IN);
    assert(session_on_writable(&g_s, now) == ERR_NOT_LOGGED_IN);
    assert(session_tick(&g_s, now) == 0);
    assert(session_retry_in(&g_s, now) == 0);

    int ours;
    int peer;
    bring_up(&ours, &peer, now);

    /* 이미 붙어 있으면 또 붙일 수 없다. */
    assert(session_on_connected(&g_s, ours, now) == ERR_DUPLICATE);
    assert(session_retry_in(&g_s, now) == -1); /* DOWN이 아니다 */

    /* 세션 전문은 호출부가 보낼 수 없다. */
    uint8_t one = 0;
    assert(session_send(&g_s, MSG_HEARTBEAT, NULL, 0, now) == ERR_INVALID_ARG);
    assert(session_send(&g_s, MSG_LOGIN_REQ, &one, 1, now) == ERR_INVALID_ARG);
    assert(session_send(&g_s, 200, &one, 1, now) == ERR_NOT_SUPPORTED);

    /* 길이가 규격과 다르면 거절한다. */
    uint8_t body[MSG_ORDER_REQ_LEN];
    size_t  n = make_order_body(body, sizeof(body), 1);
    assert(session_send(&g_s, MSG_ORDER_REQ, body, n - 1, now) ==
           ERR_INVALID_ARG);
    assert(session_send(&g_s, MSG_ORDER_REQ, NULL, n, now) == ERR_NULL_PTR);

    assert(session_state(NULL) == SESSION_DOWN);
    assert(!session_want_write(NULL));
    assert(!session_should_connect(NULL, now));
    assert(session_retry_in(NULL, now) == -1);
    assert(session_tick(NULL, now) == ERR_NULL_PTR);
    assert(session_send(NULL, MSG_ORDER_REQ, body, n, now) == ERR_NULL_PTR);
    assert(session_on_readable(NULL, NULL, NULL, now) == ERR_NULL_PTR);
    assert(session_on_writable(NULL, now) == ERR_NULL_PTR);
    session_drop(NULL, now);      /* 죽지 않는다 */
    session_config_default(NULL); /* 죽지 않는다 */

    close(ours);
    close(peer);
}

int main(void)
{
    STEP(test_rejects_before_login);
    STEP(test_login_rejected);
    STEP(test_login_and_exchange);
    STEP(test_heartbeat_not_delivered);
    STEP(test_heartbeat_on_idle);
    STEP(test_no_heartbeat_when_sending);
    STEP(test_idle_timeout);
    STEP(test_sending_does_not_keep_alive);
    STEP(test_receiving_keeps_alive);
    STEP(test_login_timeout);
    STEP(test_backoff_grows_and_caps);
    STEP(test_backoff_overflow);
    STEP(test_backoff_resets_on_login);
    STEP(test_reconnect_and_resume);
    STEP(test_reconnect_clears_partial);
    STEP(test_wrong_body_len_drops);
    STEP(test_too_long_body_drops);
    STEP(test_garbage_drops);
    STEP(test_gap_requests_resend);
    STEP(test_duplicate_dropped);
    STEP(test_gap_fill_unblocks);
    STEP(test_serves_resend);
    STEP(test_resend_beyond_store);
    STEP(test_gap_across_reconnect);
    STEP(test_recovering_cleared_on_reconnect);
    STEP(test_args);
    return 0;
}
