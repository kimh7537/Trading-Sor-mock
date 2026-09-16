/*
 * T3-15 전 구간 통합 — Phase 3의 마지막.
 *
 * ===========================================================================
 * 왜 이 테스트가 따로 필요한가
 * ===========================================================================
 *
 * T3-01부터 T3-14까지 조각마다 테스트가 있고 변이 검사도 통과했다. 그런데
 * **이어 붙였을 때 도는지는 아직 아무도 보지 않았다.** 조각이 각자 옳은 것과
 * 이어서 옳은 것은 다른 문제다 — 규격을 서로 다르게 읽고 있어도 각자의
 * 테스트는 통과한다.
 *
 * 그래서 여기서는 **진짜 프로세스 둘이 진짜 소켓으로** 주고받는다.
 *
 *   부모: FEP 쪽. `session_t`(T3-11) + `ordmap_t`(T3-13) + 판정(T3-14)
 *   자식: 거래소 쪽. **진짜 매칭 엔진**(`exchange/`)을 돌린다
 *
 * 자식이 응답을 지어내면 그것은 통합이 아니라 각본이다. 체결 수량과 가격은
 * 매칭 엔진이 호가창을 소진해서 낸 값이어야 한다.
 *
 * ===========================================================================
 * 기다림은 `poll`과 횟수 상한으로 한다
 * ===========================================================================
 *
 * `sleep`을 쓰지 않는다. 그리고 **매달리면 통과가 아니라 실패여야 한다** —
 * 모든 대기에 상한이 있고, 상한에 닿으면 단언이 깨진다.
 */
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "errors.h"
#include "evloop.h"
#include "market_rules.h"
#include "match.h"
#include "ordmap.h"
#include "session.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

#define T0 1000000
#define PUMP_MAX 400 /* 한 번 기다릴 때의 상한. 닿으면 실패다 */

/* 호가창에 미리 깔아 둘 매도 호가. 70000에 30주, 70100에 50주. */
/*
 * 논리 시각은 **정규장 안**이어야 한다. 시장 규칙이 장 마감 시간대의 주문을
 * 거절하기 때문이다(T1의 KRX 규칙). 시스템 시각은 읽지 않는다 — 이 값들은
 * 테스트가 정한 상수다.
 */
#define TS_REST_A TOD_NS(10, 0, 0)
#define TS_REST_B TOD_NS(10, 0, 1)
#define TS_TAKER TOD_NS(10, 0, 2)

#define REST_PRICE_A 70000
#define REST_QTY_A 30
#define REST_PRICE_B 70100
#define REST_QTY_B 50

static session_t g_s;
static ordmap_t  g_map;

/* ===================================================================== */
/* --- 거래소 쪽 (자식 프로세스) --- */
/* ===================================================================== */

/*
 * 자식은 블로킹 소켓으로 단순하게 짠다. **여기는 시험 상대역이지 운영 코드가
 * 아니다** — 논블로킹·이벤트 루프는 부모(FEP) 쪽이 증명할 일이다.
 */

typedef struct {
    int      fd;
    uint64_t out_seq;
    /* 우리가 접수한 주문: 우리 번호 -> 거래소 번호·상태 */
    uint64_t cl[64];
    uint64_t exch[64];
    bool     live[64];
    int32_t  n;
    /*
     * **거래소 주문번호를 여기서 매긴다.** 매칭 엔진은 `order_t.id`를
     * 호출부가 채워 주기를 기대한다 — 번호를 정하는 것은 거래소의 일이다.
     * T3-13이 "거래소 번호는 우리가 고를 수 없다"고 한 것이 바로 이 구조다.
     */
    order_id_t next_exch_id;
} peer_t;

static bool read_exact(int fd, uint8_t *buf, size_t n)
{
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r > 0) {
            got += (size_t)r;
            continue;
        }
        if (r < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

static bool write_all(int fd, const uint8_t *buf, size_t n)
{
    size_t put = 0;
    while (put < n) {
        ssize_t w = write(fd, buf + put, n - put);
        if (w > 0) {
            put += (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

static bool peer_send(peer_t *p, uint8_t type, const uint8_t *body,
                      uint32_t body_len)
{
    uint8_t frame[WIRE_HEADER_LEN + 256];
    assert(body_len <= sizeof(frame) - WIRE_HEADER_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = type;
    h.body_len = body_len;
    h.seq = p->out_seq++;
    h.ts = 0;

    int n = wire_encode_header(&h, frame, sizeof(frame));
    assert(n == (int)WIRE_HEADER_LEN);
    if (body_len > 0) {
        memcpy(frame + WIRE_HEADER_LEN, body, body_len);
    }
    return write_all(p->fd, frame, WIRE_HEADER_LEN + body_len);
}

static void peer_order(peer_t *p, match_engine_t *eng, const uint8_t *body,
                       uint32_t body_len)
{
    msg_order_req_t req;
    if (msg_decode_order_req(body, body_len, &req) < 0) {
        return;
    }

    /* **진짜 매칭 엔진에 넣는다.** 응답을 지어내지 않는다. */
    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = p->next_exch_id++;
    o.client_order_id = req.cl_ord_id;
    o.ts = TS_TAKER; /* 논리 시각. 정규장 안이면 되고 결정적이다 */
    o.price = req.price;
    o.qty = req.qty;
    o.side = (side_t)req.side;
    o.type = ORDER_LIMIT;
    o.market = MARKET_KRX;

    exec_result_t res;
    memset(&res, 0, sizeof(res));
    int rc = match_limit(eng, &o, &res);

    bool accepted = (rc == ERR_OK);

    msg_order_ack_t ack;
    memset(&ack, 0, sizeof(ack));
    ack.cl_ord_id = req.cl_ord_id;
    ack.order_id = accepted ? o.id : 0;
    ack.status = (uint8_t)res.status;
    ack.reason = accepted ? ERR_OK : rc;
    ack.filled_qty = res.filled_qty;
    ack.price = req.price;

    uint8_t abody[MSG_ORDER_ACK_LEN];
    int     an = msg_encode_order_ack(&ack, abody, sizeof(abody));
    assert(an == MSG_ORDER_ACK_LEN);
    (void)peer_send(p, MSG_ORDER_ACK, abody, (uint32_t)an);

    if (!accepted) {
        return;
    }

    if (p->n < 64) {
        p->cl[p->n] = req.cl_ord_id;
        p->exch[p->n] = o.id;
        p->live[p->n] = res.resting;
        p->n++;
    }

    /* 체결이 있으면 건별로 통보한다. */
    qty_t left = res.filled_qty;
    for (int32_t i = 0; i < res.fill_count; i++) {
        msg_fill_noti_t fn;
        memset(&fn, 0, sizeof(fn));
        fn.order_id = o.id;
        fn.cl_ord_id = req.cl_ord_id;
        snprintf(fn.symbol, sizeof(fn.symbol), "%s", req.symbol);
        fn.market = req.market;
        fn.side = req.side;
        fn.price = res.fills[i].price;
        fn.qty = res.fills[i].qty;
        left -= res.fills[i].qty;
        fn.remaining_qty = res.remaining_qty;
        fn.exec_id = (uint64_t)(i + 1);

        uint8_t fbody[MSG_FILL_NOTI_LEN];
        int     fnn = msg_encode_fill_noti(&fn, fbody, sizeof(fbody));
        assert(fnn == MSG_FILL_NOTI_LEN);
        (void)peer_send(p, MSG_FILL_NOTI, fbody, (uint32_t)fnn);
    }
    assert(left == 0);
}

static void peer_query(peer_t *p)
{
    /*
     * **당일 전체를 답한다**(T3-14의 전제). 마지막 한 건에 끝 표시를 단다 —
     * 한 건도 없으면 빈 응답 하나에 끝 표시를 달아 보낸다. 그래야 받는 쪽이
     * "없다"를 결론 낼 수 있다.
     */
    for (int32_t i = 0; i < p->n; i++) {
        msg_query_ack_t qa;
        memset(&qa, 0, sizeof(qa));
        qa.order_id = p->exch[i];
        qa.cl_ord_id = p->cl[i];
        snprintf(qa.symbol, sizeof(qa.symbol), "%s", "005930");
        /*
         * **호가창에 남아 있는가**가 "살아 있다"의 뜻이다. 전량 체결됐거나
         * 취소된 주문은 거래소가 알고는 있지만 살아 있지 않다.
         */
        qa.status = p->live[i] ? (uint8_t)STATUS_NEW : (uint8_t)STATUS_FILLED;
        qa.last = (i == p->n - 1);

        uint8_t qbody[MSG_QUERY_ACK_LEN];
        int     qn = msg_encode_query_ack(&qa, qbody, sizeof(qbody));
        assert(qn == MSG_QUERY_ACK_LEN);
        (void)peer_send(p, MSG_QUERY_ACK, qbody, (uint32_t)qn);
    }
    if (p->n == 0) {
        msg_query_ack_t qa;
        memset(&qa, 0, sizeof(qa));
        qa.last = true;
        uint8_t qbody[MSG_QUERY_ACK_LEN];
        int     qn = msg_encode_query_ack(&qa, qbody, sizeof(qbody));
        assert(qn == MSG_QUERY_ACK_LEN);
        (void)peer_send(p, MSG_QUERY_ACK, qbody, (uint32_t)qn);
    }
}

/*
 * 거래소 역할을 돈다. `drop_after`번째 주문을 처리한 뒤 끊는다(-1이면 안 끊음).
 */
static void peer_serve(int fd, int32_t drop_after)
{
    match_engine_t *eng = match_engine_create(70000, 256);
    assert(eng != NULL);
    match_set_rules(eng, &KRX_RULES);

    peer_t p;
    memset(&p, 0, sizeof(p));
    p.fd = fd;
    p.out_seq = 1;
    p.next_exch_id = 90001; /* 우리 번호(5001 등)와 겹치지 않는 대역 */

    /* 호가창을 깐다. 두 가격대에 매도를 쌓아 체결이 두 건으로 나뉘게 한다. */
    const price_t rp[2] = {REST_PRICE_A, REST_PRICE_B};
    const qty_t   rq[2] = {REST_QTY_A, REST_QTY_B};
    for (int32_t i = 0; i < 2; i++) {
        order_t rest;
        memset(&rest, 0, sizeof(rest));
        rest.id = p.next_exch_id++;
        rest.ts = (i == 0) ? TS_REST_A : TS_REST_B;
        rest.price = rp[i];
        rest.qty = rq[i];
        rest.side = SIDE_SELL;
        rest.type = ORDER_LIMIT;
        rest.market = MARKET_KRX;

        exec_result_t r;
        memset(&r, 0, sizeof(r));
        assert(match_limit(eng, &rest, &r) == ERR_OK);
    }

    int32_t orders = 0;

    for (;;) {
        uint8_t hbuf[WIRE_HEADER_LEN];
        if (!read_exact(fd, hbuf, sizeof(hbuf))) {
            break;
        }
        wire_header_t h;
        if (wire_decode_header(hbuf, sizeof(hbuf), &h) < 0) {
            break;
        }
        uint8_t body[512];
        if (h.body_len > 0) {
            if (h.body_len > sizeof(body) || !read_exact(fd, body, h.body_len)) {
                break;
            }
        }

        if (h.type == MSG_LOGIN_REQ) {
            msg_login_ack_t la;
            memset(&la, 0, sizeof(la));
            la.result = ERR_OK;
            uint8_t lb[MSG_LOGIN_ACK_LEN];
            int     ln = msg_encode_login_ack(&la, lb, sizeof(lb));
            assert(ln == MSG_LOGIN_ACK_LEN);
            (void)peer_send(&p, MSG_LOGIN_ACK, lb, (uint32_t)ln);
        } else if (h.type == MSG_ORDER_REQ) {
            peer_order(&p, eng, body, h.body_len);
            orders++;
            if (drop_after >= 0 && orders >= drop_after) {
                break; /* 응답을 보낸 뒤 끊는다 */
            }
        } else if (h.type == MSG_QUERY_REQ) {
            peer_query(&p);
        }
        /* 하트비트·재전송 요청은 이 시험에서 받아 넘긴다. */
    }

    match_engine_destroy(eng);
    close(fd);
}

/* ===================================================================== */
/* --- FEP 쪽 (부모) --- */
/* ===================================================================== */

typedef struct {
    int32_t acks;
    int32_t fills;
    qty_t   filled_qty;
    int64_t notional;
    bool    query_done;
} tally_t;

static tally_t g_t;

static void on_frame(const wire_header_t *hdr, const uint8_t *body, void *ctx)
{
    tally_t *t = ctx;

    if (hdr->type == MSG_ORDER_ACK) {
        msg_order_ack_t a;
        assert(msg_decode_order_ack(body, hdr->body_len, &a) ==
               MSG_ORDER_ACK_LEN);
        t->acks++;
        /* **여기가 T3-13이 하는 일이다** — 거래소 번호를 우리 번호에 붙인다. */
        assert(ordmap_on_ack(&g_map, a.cl_ord_id, a.order_id,
                             a.reason == ERR_OK) == ERR_OK);
        return;
    }

    if (hdr->type == MSG_FILL_NOTI) {
        msg_fill_noti_t f;
        assert(msg_decode_fill_noti(body, hdr->body_len, &f) ==
               MSG_FILL_NOTI_LEN);
        t->fills++;
        t->filled_qty += f.qty;
        t->notional += (int64_t)f.price * (int64_t)f.qty;

        /*
         * **체결은 거래소 번호로 온다.** 우리 주문을 되짚을 수 있어야 한다 —
         * 못 찾으면 그 체결은 갈 곳이 없다.
         */
        const ordent_t *e = ordmap_find_by_exch(&g_map, f.order_id);
        assert(e != NULL);
        assert(e->cl_ord_id == f.cl_ord_id);
        return;
    }

    if (hdr->type == MSG_QUERY_ACK) {
        msg_query_ack_t q;
        assert(msg_decode_query_ack(body, hdr->body_len, &q) ==
               MSG_QUERY_ACK_LEN);
        if (q.cl_ord_id != 0) {
            bool live = (q.status == STATUS_NEW || q.status == STATUS_PARTIAL);
            (void)ordmap_on_query_result(&g_map, q.cl_ord_id, q.order_id, live);
        }
        if (q.last) {
            /* **끝 표시를 받았을 때만 판정한다**(T3-14). */
            (void)ordmap_finish_query(&g_map);
            t->query_done = true;
        }
        return;
    }
}

/* 한 바퀴 돌린다. `sleep`은 없고 `poll`로만 기다린다. */
static void pump_once(int64_t now)
{
    if (session_want_write(&g_s)) {
        if (session_on_writable(&g_s, now) < 0) {
            return;
        }
    }
    if (session_state(&g_s) == SESSION_DOWN) {
        return;
    }

    struct pollfd pfd = {.fd = g_s.fd, .events = POLLIN, .revents = 0};
    if (poll(&pfd, 1, 20) > 0) {
        (void)session_on_readable(&g_s, on_frame, &g_t, now);
    }
}

/* 조건이 참이 될 때까지 돌린다. 상한에 닿으면 **실패다.** */
#define PUMP_UNTIL(cond, now)                                               \
    do {                                                                    \
        int32_t _i = 0;                                                     \
        while (!(cond) && _i < PUMP_MAX) {                                  \
            pump_once(now);                                                 \
            _i++;                                                           \
        }                                                                   \
        assert(cond); /* 매달리면 통과가 아니라 실패여야 한다 */            \
    } while (0)

/* 거래소 역할을 하는 자식을 띄우고, 부모 쪽 소켓을 돌려준다. */
static pid_t spawn_peer(int *our_fd, int32_t drop_after)
{
    int sv[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(sv[0]);
        peer_serve(sv[1], drop_after);
        _exit(0);
    }

    close(sv[1]);
    assert(evloop_set_nonblocking(sv[0]) == ERR_OK);
    *our_fd = sv[0];
    return pid;
}

static size_t make_order(uint8_t *body, size_t cap, uint64_t cl_ord_id,
                         price_t price, qty_t qty)
{
    msg_order_req_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.account, sizeof(m.account), "%s", "123456789012");
    snprintf(m.symbol, sizeof(m.symbol), "%s", "005930");
    m.cl_ord_id = cl_ord_id;
    m.side = SIDE_BUY;
    m.type = ORDER_LIMIT;
    m.market = MARKET_KRX;
    m.price = price;
    m.qty = qty;

    int n = msg_encode_order_req(&m, body, cap);
    assert(n == MSG_ORDER_REQ_LEN);
    return (size_t)n;
}

/* --- 1. 주문 한 건이 끝까지 간다 --- */

/*
 * **이 테스트가 T3-15의 전부다.** 70100에 60주를 사면 매칭 엔진이 두 가격대를
 * 소진한다 — 70000에 30주, 70100에 30주. 그 결과가 전문으로 실려 돌아와
 * 우리 주문번호에 붙는지를 본다.
 */
static void test_order_to_fill_end_to_end(void)
{
    int64_t now = T0;
    int     fd;
    pid_t   peer = spawn_peer(&fd, -1);

    ordmap_init(&g_map);
    memset(&g_t, 0, sizeof(g_t));
    assert(session_init(&g_s, NULL, "FEP-KRX-01", now) == ERR_OK);
    assert(session_on_connected(&g_s, fd, now) == ERR_OK);
    PUMP_UNTIL(session_state(&g_s) == SESSION_READY, now);

    uint8_t body[MSG_ORDER_REQ_LEN];
    size_t  n = make_order(body, sizeof(body), 5001, REST_PRICE_B, 60);

    assert(ordmap_add(&g_map, 5001) == ERR_OK);
    assert(session_send(&g_s, MSG_ORDER_REQ, body, n, now) == ERR_OK);

    PUMP_UNTIL(g_t.acks == 1 && g_t.fills == 2, now);

    /* **매칭 엔진이 낸 값과 같아야 한다.** 30@70000 + 30@70100. */
    assert(g_t.filled_qty == 60);
    assert(g_t.notional == (int64_t)REST_PRICE_A * REST_QTY_A +
                               (int64_t)REST_PRICE_B * (60 - REST_QTY_A));

    /* 우리 주문에 거래소 번호가 붙었고 양방향으로 찾힌다. */
    const ordent_t *e = ordmap_find_by_cl(&g_map, 5001);
    assert(e != NULL);
    assert(e->exch_id != 0);
    assert(ordmap_find_by_exch(&g_map, e->exch_id) == e);

    close(fd);
    kill(peer, SIGKILL);
    waitpid(peer, NULL, 0);
}

/* --- 2. 끊기고, 다시 붙고, 판정한다 --- */

/*
 * T3-11(재접속) · T3-13(매핑) · T3-14(판정)가 **함께** 도는지를 본다.
 *
 * 주문 둘을 보내는데 거래소가 첫 응답만 주고 끊는다. 두 번째 주문은
 * **응답을 못 받은 채** 끊긴 주문이다 — 판정 보류가 되어야 하고, 다시 붙어
 * 조회하면 거래소가 아는지 모르는지로 갈려야 한다.
 */
static void test_disconnect_then_resolve(void)
{
    int64_t now = T0;
    int     fd;
    pid_t   peer = spawn_peer(&fd, 1); /* 주문 하나 처리하고 끊는다 */

    ordmap_init(&g_map);
    memset(&g_t, 0, sizeof(g_t));
    assert(session_init(&g_s, NULL, "FEP-KRX-01", now) == ERR_OK);
    assert(session_on_connected(&g_s, fd, now) == ERR_OK);
    PUMP_UNTIL(session_state(&g_s) == SESSION_READY, now);

    uint8_t body[MSG_ORDER_REQ_LEN];

    size_t n1 = make_order(body, sizeof(body), 6001, REST_PRICE_A, 10);
    assert(ordmap_add(&g_map, 6001) == ERR_OK);
    assert(session_send(&g_s, MSG_ORDER_REQ, body, n1, now) == ERR_OK);
    PUMP_UNTIL(g_t.acks == 1, now);

    /* 두 번째 주문은 나가지만 거래소는 이미 끊었다. */
    size_t n2 = make_order(body, sizeof(body), 6002, REST_PRICE_A, 10);
    assert(ordmap_add(&g_map, 6002) == ERR_OK);
    (void)session_send(&g_s, MSG_ORDER_REQ, body, n2, now);

    PUMP_UNTIL(session_state(&g_s) == SESSION_DOWN, now);
    close(fd);
    waitpid(peer, NULL, 0);

    /* **응답 못 받은 주문만 판정 보류가 된다.** 6001은 이미 접수됐다. */
    assert(ordmap_on_disconnect(&g_map) == 1);
    assert(ordmap_indoubt_count(&g_map) == 1);
    assert(ordmap_find_by_cl(&g_map, 6002)->state == ORD_INDOUBT);
    assert(ordmap_find_by_cl(&g_map, 6001)->state == ORD_LIVE);

    /*
     * 새 거래소에 다시 붙는다. 이 거래소는 우리 주문을 하나도 모른다 —
     * 곧 6002는 **닿지 않았다**는 뜻이고, 조회가 끝나면 그렇게 판정돼야 한다.
     */
    now += session_retry_in(&g_s, now);
    int   fd2;
    pid_t peer2 = spawn_peer(&fd2, -1);
    assert(session_on_connected(&g_s, fd2, now) == ERR_OK);
    PUMP_UNTIL(session_state(&g_s) == SESSION_READY, now);

    msg_query_req_t qr;
    memset(&qr, 0, sizeof(qr));
    snprintf(qr.account, sizeof(qr.account), "%s", "123456789012");
    qr.order_id = 0; /* 당일 전체 */
    uint8_t qb[MSG_QUERY_REQ_LEN];
    int     qn = msg_encode_query_req(&qr, qb, sizeof(qb));
    assert(qn == MSG_QUERY_REQ_LEN);
    assert(session_send(&g_s, MSG_QUERY_REQ, qb, (size_t)qn, now) == ERR_OK);

    PUMP_UNTIL(g_t.query_done, now);

    /* **판정됐다.** 거래소가 모르는 주문이었으므로 닿지 않은 것이다. */
    assert(ordmap_indoubt_count(&g_map) == 0);
    const ordent_t *e = ordmap_find_by_cl(&g_map, 6002);
    assert(e != NULL);
    assert(e->state == ORD_DONE);
    assert(e->exch_id == 0); /* 없는 주문에는 번호가 없다 */

    close(fd2);
    kill(peer2, SIGKILL);
    waitpid(peer2, NULL, 0);
}

int main(void)
{
    /* 상대가 먼저 끊었을 때 SIGPIPE로 죽지 않는다. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    assert(sigaction(SIGPIPE, &sa, NULL) == 0);

    STEP(test_order_to_fill_end_to_end);
    STEP(test_disconnect_then_resolve);
    return 0;
}
