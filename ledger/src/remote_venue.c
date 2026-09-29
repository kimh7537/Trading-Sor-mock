#include "remote_venue.h"

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"
#include "evloop.h"
#include "msg.h"
#include "wire.h"

/*
 * 실제로 흐른 시간(ms). 세션의 하트비트·무응답 판정에만 쓴다.
 * 헤더의 "여기는 시각을 읽는다" 참조 — 매칭도 SOR도 이 값을 보지 않는다.
 */
static int64_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* 지금 기다리고 있는 답. 오면 여기 담긴다. */
typedef struct {
    ordmap_t *om;
    uint8_t   want_type; /* 기다리는 응답 종별 */
    uint64_t  want_cl;   /* 다리·취소 응답을 가려낼 우리 번호 */
    bool      got;

    msg_leg_ack_t    leg;
    msg_cancel_ack_t can;
    msg_book_ack_t   book;
} wait_t;

/*
 * 세션이 넘겨준 업무 전문. 로그인·하트비트는 여기까지 오지 않는다.
 *
 * **기다리던 것이 아니어도 매핑에는 반영한다** — 늦게 온 답이라도 그 다리의
 * 거래소 번호는 알아야 취소를 보낼 수 있다.
 */
static void on_frame(const wire_header_t *hdr, const uint8_t *body, void *ctx)
{
    wait_t *w = ctx;
    if (w == NULL) {
        return;
    }

    switch (hdr->type) {
    case MSG_LEG_ACK: {
        msg_leg_ack_t a;
        if (msg_decode_leg_ack(body, hdr->body_len, &a) < 0) {
            return;
        }
        (void)ordmap_on_ack(w->om, a.cl_ord_id, a.order_id, a.reason == ERR_OK);
        if (w->want_type == MSG_LEG_ACK && a.cl_ord_id == w->want_cl) {
            w->leg = a;
            w->got = true;
        }
        return;
    }
    case MSG_CANCEL_ACK: {
        msg_cancel_ack_t a;
        if (msg_decode_cancel_ack(body, hdr->body_len, &a) < 0) {
            return;
        }
        if (w->want_type == MSG_CANCEL_ACK && a.cl_ord_id == w->want_cl) {
            w->can = a;
            w->got = true;
        }
        return;
    }
    case MSG_BOOK_ACK: {
        msg_book_ack_t a;
        if (msg_decode_book_ack(body, hdr->body_len, &a) < 0) {
            return;
        }
        if (w->want_type == MSG_BOOK_ACK) {
            w->book = a;
            w->got = true;
        }
        return;
    }
    default:
        return;
    }
}

/*
 * 한 바퀴. 보낼 것을 내보내고, 기다렸다 읽고, 시각을 먹인다.
 * `w`가 NULL이면 받은 다리 응답을 버린다(유휴 펌프).
 */
static int pump_once(remote_link_t *l, wait_t *w, int wait_ms)
{
    if (session_want_write(&l->s) && session_on_writable(&l->s, now_ms()) < 0) {
        return ERR_IO;
    }
    if (session_state(&l->s) == SESSION_DOWN) {
        return ERR_IO;
    }

    struct pollfd p = {.fd = l->s.fd, .events = POLLIN, .revents = 0};
    int           pr = poll(&p, 1, wait_ms);
    if (pr < 0) {
        /* 시그널에 깨는 것은 실패가 아니다. 다음 바퀴에서 다시 본다 */
        return (errno == EINTR) ? ERR_OK : ERR_IO;
    }
    if (pr > 0 && session_on_readable(&l->s, on_frame, w, now_ms()) < 0) {
        return ERR_IO;
    }
    if (session_tick(&l->s, now_ms()) < 0) {
        return ERR_IO;
    }
    return (session_state(&l->s) == SESSION_DOWN) ? ERR_IO : ERR_OK;
}

/*
 * 기다리던 답이 올 때까지 돌린다. **상한에 닿으면 실패다** — 매달리면 원장이
 * 그 접속을 붙잡은 채 다른 주문도 못 받는다.
 */
static int wait_for(remote_link_t *l, wait_t *w)
{
    int64_t deadline = now_ms() + REMOTE_VENUE_TIMEOUT_MS;
    while (!w->got) {
        if (now_ms() >= deadline) {
            return ERR_IO;
        }
        int rc = pump_once(l, w, REMOTE_VENUE_POLL_MS);
        if (rc != ERR_OK) {
            l->up = false;
            return rc;
        }
    }
    return ERR_OK;
}

/* 붙는다. 논블로킹으로 바꿔 돌려준다 — 세션이 그것을 요구한다(T3-08). */
static int dial(const char *host, uint16_t port, int *out_fd)
{
    char portbuf[8];
    snprintf(portbuf, sizeof(portbuf), "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* 상대는 `listener_open`이 연 IPv4 포트다 */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, portbuf, &hints, &res) != 0) {
        return ERR_NOT_FOUND;
    }

    int fd = -1;
    for (struct addrinfo *a = res; a != NULL; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        return ERR_IO;
    }
    if (evloop_set_nonblocking(fd) != ERR_OK) {
        close(fd);
        return ERR_IO;
    }
    *out_fd = fd;
    return ERR_OK;
}

void remote_venues_init(remote_venues_t *rv, const char *symbol)
{
    if (rv == NULL) {
        return;
    }
    memset(rv, 0, sizeof(*rv));
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        rv->link[m].fd = -1;
    }
    snprintf(rv->symbol, sizeof(rv->symbol), "%s", symbol != NULL ? symbol : "");
    /*
     * 거래소는 계좌를 보지 않는다 — 계좌 원장은 증권사의 것이다. 그래도 자리를
     * 비워 두지 않는 것은 전문이 고정 길이라 무엇이든 실리기 때문이다.
     */
    snprintf(rv->account, sizeof(rv->account), "%s", "000000000000");
}

int remote_venues_set(remote_venues_t *rv, market_t m, const char *host,
                      uint16_t port)
{
    if (rv == NULL || host == NULL) {
        return ERR_NULL_PTR;
    }
    if (m < 0 || m >= MARKET_COUNT || host[0] == 0 || port == 0) {
        return ERR_INVALID_ARG;
    }
    remote_link_t *l = &rv->link[m];
    snprintf(l->host, sizeof(l->host), "%s", host);
    l->port = port;
    l->configured = true;
    return ERR_OK;
}

bool remote_venues_any(const remote_venues_t *rv)
{
    if (rv == NULL) {
        return false;
    }
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (rv->link[m].configured) {
            return true;
        }
    }
    return false;
}

int remote_venues_connect(remote_venues_t *rv)
{
    if (rv == NULL) {
        return ERR_NULL_PTR;
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        remote_link_t *l = &rv->link[m];
        if (!l->configured) {
            continue;
        }

        int rc = dial(l->host, l->port, &l->fd);
        if (rc != ERR_OK) {
            remote_venues_close(rv);
            return rc;
        }

        ordmap_init(&l->om);

        char id[SESSION_ID_LEN + 1];
        snprintf(id, sizeof(id), "FEP-%s",
                 (m == MARKET_KRX) ? "KRX" : "NXT");
        rc = session_init(&l->s, NULL, id, now_ms());
        if (rc == ERR_OK) {
            rc = session_on_connected(&l->s, l->fd, now_ms());
        }
        if (rc != ERR_OK) {
            remote_venues_close(rv);
            return rc;
        }

        /* 로그인 응답을 기다린다. 상한에 닿으면 붙지 않은 것이다 */
        int64_t deadline = now_ms() + REMOTE_VENUE_TIMEOUT_MS;
        while (session_state(&l->s) != SESSION_READY) {
            if (now_ms() >= deadline ||
                pump_once(l, NULL, REMOTE_VENUE_POLL_MS) != ERR_OK) {
                remote_venues_close(rv);
                return ERR_IO;
            }
        }
        l->up = true;
    }
    return ERR_OK;
}

void remote_venues_pump(remote_venues_t *rv)
{
    if (rv == NULL) {
        return;
    }
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        remote_link_t *l = &rv->link[m];
        if (!l->up) {
            continue;
        }
        /*
         * 기다리지 않는다(0ms). 원장의 유휴 콜백은 가상 참가자도 돌리므로
         * 여기서 붙잡고 있으면 호가창이 느려진다.
         */
        if (pump_once(l, NULL, 0) != ERR_OK) {
            l->up = false;
        }
    }
}

void remote_venues_close(remote_venues_t *rv)
{
    if (rv == NULL) {
        return;
    }
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        remote_link_t *l = &rv->link[m];
        if (l->fd >= 0) {
            close(l->fd);
            l->fd = -1;
        }
        l->up = false;
    }
}

/*
 * 다리 하나를 그 시장의 거래소로 보내고 답을 기다린다.
 *
 * 매칭 엔진을 부르는 자리(`send_local`)와 **같은 뜻의 결과**를 채운다.
 */
static int send_remote(void *ctx, const order_t *req, const plan_leg_t *leg,
                       order_id_t phys_id, exec_result_t *out)
{
    remote_venues_t *rv = ctx;
    if (leg->market < 0 || leg->market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }
    remote_link_t *l = &rv->link[leg->market];
    if (!l->up) {
        return ERR_IO; /* 그 거래소에 붙어 있지 않다 */
    }

    msg_order_req_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.account, sizeof(m.account), "%s", rv->account);
    snprintf(m.symbol, sizeof(m.symbol), "%s", rv->symbol);
    m.cl_ord_id = phys_id; /* **우리 번호.** 거래소 번호는 거래소가 붙여 돌려준다 */
    m.side = (uint8_t)req->side;
    m.type = (uint8_t)leg->type;
    m.market = (uint8_t)leg->market;
    m.price = leg->limit_price;
    m.qty = leg->qty;

    uint8_t body[MSG_LEG_REQ_LEN];
    int     n = msg_encode_order_req(&m, body, sizeof(body));
    if (n < 0) {
        return ERR_INVALID_ARG;
    }

    int rc = ordmap_add(&l->om, phys_id);
    if (rc != ERR_OK) {
        return rc;
    }

    rc = session_send(&l->s, MSG_LEG_REQ, body, (size_t)n, now_ms());
    if (rc != ERR_OK) {
        return rc; /* 아무것도 보내지 않았다(session.h) */
    }

    wait_t w;
    memset(&w, 0, sizeof(w));
    w.om = &l->om;
    w.want_type = MSG_LEG_ACK;
    w.want_cl = phys_id;

    rc = wait_for(l, &w);
    if (rc != ERR_OK) {
        /*
         * **닿았는지 모른다.** 거부로 단정하면 우리가 모르는 포지션이 생기고,
         * 접수로 단정하면 없는 주문을 취소하려 든다(T3-14). 판정 보류로 남긴다.
         */
        (void)ordmap_on_disconnect(&l->om);
        return rc;
    }

    if (w.leg.reason != ERR_OK) {
        return w.leg.reason;
    }

    memset(out, 0, sizeof(*out));
    /*
     * 체결 **목록**은 오지 않는다. 집행기가 수량과 금액에서 되짚는다 —
     * 평균가로 줄이지 않으므로 나눗셈 나머지가 새지 않는다.
     */
    out->fill_count = 0;
    out->truncated = (w.leg.filled_qty > 0);
    out->filled_qty = w.leg.filled_qty;
    out->notional = w.leg.notional;
    out->remaining_qty = leg->qty - w.leg.filled_qty;
    out->resting = (w.leg.resting != 0);

    if (!out->resting) {
        (void)ordmap_close(&l->om, phys_id);
    }
    return ERR_OK;
}

int remote_submit(remote_venues_t *rv, order_map_t *map, const order_t *req,
                  const exec_plan_t *plan, exec_report_t *out)
{
    if (rv == NULL) {
        return ERR_NULL_PTR;
    }
    return exec_submit_via(map, send_remote, rv, req, plan, out);
}

/*
 * 다리 하나를 그 거래소에서 취소한다.
 *
 * **거래소 번호로 보낸다.** 아직 응답을 못 받은 다리는 `ordmap_cancel_key`가 0을
 * 주고, 그러면 거래소가 우리 번호로 찾는다 — 못 받았다면 "없는 주문"으로 깔끔히
 * 거부된다(T3-13의 (c)).
 */
static int cancel_remote(void *ctx, order_id_t phys_id, market_t market, ts_t ts,
                         qty_t *out_qty)
{
    (void)ts; /* 취소 전문에는 논리 시각이 실리지 않는다. 헤더의 것을 세션이 채운다 */

    remote_venues_t *rv = ctx;
    if (market < 0 || market >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }
    remote_link_t *l = &rv->link[market];
    if (!l->up) {
        return ERR_IO;
    }

    order_id_t exch_id = 0;
    int        rc = ordmap_cancel_key(&l->om, phys_id, &exch_id);
    if (rc != ERR_OK) {
        return rc;
    }

    msg_cancel_req_t m;
    memset(&m, 0, sizeof(m));
    snprintf(m.account, sizeof(m.account), "%s", rv->account);
    m.order_id = exch_id;
    m.cl_ord_id = phys_id;

    uint8_t body[MSG_CANCEL_REQ_LEN];
    int     n = msg_encode_cancel_req(&m, body, sizeof(body));
    if (n < 0) {
        return ERR_INVALID_ARG;
    }

    rc = session_send(&l->s, MSG_CANCEL_REQ, body, (size_t)n, now_ms());
    if (rc != ERR_OK) {
        return rc;
    }

    wait_t w;
    memset(&w, 0, sizeof(w));
    w.om = &l->om;
    w.want_type = MSG_CANCEL_ACK;
    w.want_cl = phys_id;

    rc = wait_for(l, &w);
    if (rc != ERR_OK) {
        return rc;
    }
    if (w.can.reason != ERR_OK) {
        return w.can.reason;
    }

    *out_qty = w.can.canceled_qty;
    (void)ordmap_close(&l->om, phys_id);
    return ERR_OK;
}

int remote_cancel(remote_venues_t *rv, order_map_t *map, order_id_t logical_id,
                  ts_t ts, cancel_report_t *out)
{
    if (rv == NULL) {
        return ERR_NULL_PTR;
    }
    return exec_cancel_via(map, cancel_remote, rv, logical_id, ts, out);
}

int remote_book(remote_venues_t *rv, market_t m, const char *symbol,
                msg_book_ack_t *out)
{
    if (rv == NULL || symbol == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (m < 0 || m >= MARKET_COUNT) {
        return ERR_INVALID_ARG;
    }
    remote_link_t *l = &rv->link[m];
    if (!l->up) {
        return ERR_IO;
    }

    msg_book_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.symbol, sizeof(req.symbol), "%s", symbol);
    req.market = (uint8_t)m;

    uint8_t body[MSG_BOOK_REQ_LEN];
    int     n = msg_encode_book_req(&req, body, sizeof(body));
    if (n < 0) {
        return ERR_INVALID_ARG;
    }

    int rc = session_send(&l->s, MSG_BOOK_REQ, body, (size_t)n, now_ms());
    if (rc != ERR_OK) {
        return rc;
    }

    wait_t w;
    memset(&w, 0, sizeof(w));
    w.om = &l->om;
    w.want_type = MSG_BOOK_ACK;

    rc = wait_for(l, &w);
    if (rc != ERR_OK) {
        return rc;
    }
    *out = w.book;
    return ERR_OK;
}
