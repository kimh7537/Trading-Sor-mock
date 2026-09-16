#include "session.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "errors.h"

void session_config_default(session_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
    cfg->heartbeat_ms = 5000;
    cfg->idle_timeout_ms = 15000;
    cfg->login_timeout_ms = 5000;
    cfg->backoff_min_ms = 200;
    cfg->backoff_max_ms = 30000;
}

static bool cfg_valid(const session_config_t *c)
{
    return c->heartbeat_ms > 0 && c->idle_timeout_ms > 0 &&
           c->login_timeout_ms > 0 && c->backoff_min_ms > 0 &&
           c->backoff_max_ms >= c->backoff_min_ms;
}

int session_init(session_t *s, const session_config_t *cfg,
                 const char *session_id, int64_t now_ms)
{
    if (s == NULL || session_id == NULL || session_id[0] == '\0') {
        return ERR_NULL_PTR;
    }

    session_config_t use;
    if (cfg == NULL) {
        session_config_default(&use);
    } else {
        use = *cfg;
    }
    if (!cfg_valid(&use)) {
        return ERR_INVALID_ARG;
    }

    memset(s, 0, sizeof(*s));
    s->cfg = use;
    s->state = SESSION_DOWN;
    s->fd = -1;
    framer_init(&s->rx);
    sendq_init(&s->tx);

    strncpy(s->session_id, session_id, SESSION_ID_LEN);
    s->session_id[SESSION_ID_LEN] = '\0';

    s->out_seq = 1; /* 0은 "아직 아무것도 안 보냈다"와 헷갈린다 */
    s->backoff_ms = s->cfg.backoff_min_ms;

    /*
     * **첫 접속은 기다리지 않는다.** 백오프는 실패한 뒤의 이야기이고,
     * 아직 한 번도 시도하지 않았다.
     */
    s->retry_at_ms = now_ms;
    s->last_tx_ms = now_ms;
    s->last_rx_ms = now_ms;

    return ERR_OK;
}

session_state_t session_state(const session_t *s)
{
    return (s != NULL) ? s->state : SESSION_DOWN;
}

bool session_want_write(const session_t *s)
{
    return (s != NULL) && sendq_want_write(&s->tx);
}

bool session_should_connect(const session_t *s, int64_t now_ms)
{
    return s != NULL && s->state == SESSION_DOWN && now_ms >= s->retry_at_ms;
}

int64_t session_retry_in(const session_t *s, int64_t now_ms)
{
    if (s == NULL || s->state != SESSION_DOWN) {
        return -1;
    }
    int64_t left = s->retry_at_ms - now_ms;
    return (left > 0) ? left : 0;
}

/* --- 보내기 --- */

/*
 * 헤더를 채워 큐에 넣는다. **전부 아니면 전무**(T3-10) — 헤더만 들어가고 바디가
 * 거절되면 받는 쪽은 길이가 맞지 않는 전문을 본다. 그래서 한 번에 조립해
 * 한 번에 넣는다.
 */
static int enqueue(session_t *s, uint8_t type, const uint8_t *body,
                   size_t body_len, int64_t now_ms)
{
    uint8_t frame[WIRE_HEADER_LEN + 256];
    if (body_len > sizeof(frame) - WIRE_HEADER_LEN) {
        return ERR_INVALID_ARG; /* 이 계층이 보내는 전문은 전부 작다 */
    }

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = type;
    h.body_len = (uint32_t)body_len;
    h.seq = s->out_seq;
    /*
     * 논리 시각을 그대로 싣는다. 시계를 여기서 읽지 않는다 — 헤더의 설명 참조.
     * 단위는 밀리초지만 전문 규격(T3-01)은 나노초라 맞춰 준다.
     */
    h.ts = now_ms * 1000000;

    int n = wire_encode_header(&h, frame, sizeof(frame));
    if (n < 0) {
        return n;
    }
    if (body_len > 0) {
        memcpy(frame + WIRE_HEADER_LEN, body, body_len);
    }

    int rc = sendq_push(&s->tx, frame, WIRE_HEADER_LEN + body_len);
    if (rc != ERR_OK) {
        return rc; /* 큐가 찼다. 아무것도 넣지 않았다 */
    }

    s->out_seq++;
    s->last_tx_ms = now_ms;

    return ERR_OK;
}

int session_send(session_t *s, uint8_t type, const uint8_t *body,
                 size_t body_len, int64_t now_ms)
{
    if (s == NULL || (body == NULL && body_len > 0)) {
        return ERR_NULL_PTR;
    }
    /*
     * **로그인 전에는 보내지 않는다.** 헤더의 설명 참조 — 조용히 버려질 수
     * 있는 주문을 만들지 않는다.
     */
    if (s->state != SESSION_READY) {
        return ERR_NOT_LOGGED_IN;
    }
    /* 세션 전문은 세션이 보낸다. 호출부가 흉내 내면 상태가 어긋난다. */
    if (type == MSG_LOGIN_REQ || type == MSG_LOGIN_ACK ||
        type == MSG_HEARTBEAT) {
        return ERR_INVALID_ARG;
    }
    if (!msg_is_known(type)) {
        return ERR_NOT_SUPPORTED;
    }
    /* 길이도 규격과 맞아야 한다. 틀린 채로 내보내면 상대가 끊는다(T3-09). */
    if (msg_body_len(type) != (int32_t)body_len) {
        return ERR_INVALID_ARG;
    }

    return enqueue(s, type, body, body_len, now_ms);
}

/* --- 접속과 단절 --- */

int session_on_connected(session_t *s, int fd, int64_t now_ms)
{
    if (s == NULL) {
        return ERR_NULL_PTR;
    }
    if (fd < 0) {
        return ERR_INVALID_ARG;
    }
    if (s->state != SESSION_DOWN) {
        return ERR_DUPLICATE; /* 이미 붙어 있다 */
    }

    /*
     * 버퍼를 여기서 비우지 않는다. **DOWN이면 이미 비어 있다** —
     * `session_init`이 통째로 0으로 만들고 `session_drop`이 비운다. 그리고
     * DOWN이 아니면 위에서 `ERR_DUPLICATE`로 돌아간다.
     *
     * 여기서 한 번 더 비우면 안전해 보이지만 **동작이 똑같아 변이 검사가
     * 구분하지 못한다**(X6이 살아남았다). T3-08~10에서 되풀이한 것과 같은
     * 판단이다 — 대신 막아 주는 것이 우리 코드면 중복이다.
     */
    s->fd = fd;
    s->state = SESSION_LOGGING_IN;
    s->login_at_ms = now_ms;
    s->last_rx_ms = now_ms;
    s->last_tx_ms = now_ms;

    msg_login_req_t req;
    memset(&req, 0, sizeof(req));
    /*
     * `strncpy`가 아니라 `memcpy`다. 둘 다 크기가 `SESSION_ID_LEN + 1`이고
     * 원본은 이미 널로 끝나므로 널까지 통째로 옮기면 된다. `strncpy`로
     * `SESSION_ID_LEN`만 옮기면 **-O2에서 `-Wstringop-truncation`에 걸린다**
     * (16자가 꽉 차면 널이 안 따라간다는 것을 컴파일러가 증명한다).
     * Debug에서는 보이지 않고 Release에서만 나오는 종류다.
     */
    memcpy(req.session_id, s->session_id, sizeof(req.session_id));

    uint8_t body[MSG_LOGIN_REQ_LEN];
    int     n = msg_encode_login_req(&req, body, sizeof(body));
    if (n < 0) {
        return n;
    }

    return enqueue(s, MSG_LOGIN_REQ, body, (size_t)n, now_ms);
}

void session_drop(session_t *s, int64_t now_ms)
{
    if (s == NULL) {
        return;
    }

    s->state = SESSION_DOWN;
    s->fd = -1;
    framer_init(&s->rx);
    sendq_init(&s->tx);

    /*
     * **지수 백오프에 상한을 둔다.** 상한이 없으면 오래 끊겨 있던 세션이
     * 몇 시간 뒤에나 다시 붙는다 — 상대가 이미 돌아와 있어도.
     *
     * 지터(무작위 흔들기)는 넣지 않는다. 지터는 **많은 클라이언트가 동시에
     * 재접속해 상대를 다시 쓰러뜨리는 것**을 막는 장치인데, 이 FEP는 거래소마다
     * 접속이 하나다. 무리를 이루지 않으므로 막을 무리가 없다. 넣으려면 난수가
     * 필요하고, 그러면 시드를 주입받아야 하며(CLAUDE.md), 재접속 시각이 더는
     * 시험하기 쉬운 값이 아니게 된다. **값을 치를 이유가 없다.**
     */
    s->retry_at_ms = now_ms + s->backoff_ms;
    s->attempts++;

    if (s->backoff_ms < s->cfg.backoff_max_ms) {
        int32_t next = s->backoff_ms * 2;
        /* 곱하기가 넘치거나 상한을 지나면 상한에 붙인다. */
        if (next <= s->backoff_ms || next > s->cfg.backoff_max_ms) {
            next = s->cfg.backoff_max_ms;
        }
        s->backoff_ms = next;
    }
}

/* --- 받기 --- */

static int handle_frame(session_t *s, const wire_header_t *hdr,
                        const uint8_t *body, session_frame_fn fn, void *ctx)
{
    switch (hdr->type) {
    case MSG_HEARTBEAT:
        /*
         * 받은 것만으로 할 일이 끝난다. 되받아치지 않는다 — 양쪽이 서로의
         * 하트비트에 하트비트로 답하면 **조용한 접속이란 것이 없어진다.**
         * 각자 자기 시계로 보낸다.
         */
        return ERR_OK;

    case MSG_LOGIN_ACK: {
        if (s->state != SESSION_LOGGING_IN) {
            return ERR_OK; /* 늦게 온 응답. 무시한다 */
        }
        msg_login_ack_t ack;
        int             rc = msg_decode_login_ack(body, hdr->body_len, &ack);
        if (rc < 0) {
            return rc;
        }
        if (ack.result != ERR_OK) {
            return ERR_NOT_LOGGED_IN; /* 상대가 거절했다 */
        }
        s->state = SESSION_READY;
        /*
         * **붙은 것이 아니라 로그인된 것이 성공이다.** TCP만 붙고 로그인이
         * 거절되는 상대에게 백오프 없이 달려들면 안 된다.
         */
        s->attempts = 0;
        s->backoff_ms = s->cfg.backoff_min_ms;
        return ERR_OK;
    }

    case MSG_LOGIN_REQ:
        /*
         * 이 세션은 거는 쪽이다. 로그인 요청을 받을 자리가 아니다 —
         * 규격이 다른 상대이거나 스트림이 어긋난 것이다.
         */
        return ERR_NOT_SUPPORTED;

    default:
        break;
    }

    /* 업무 전문은 로그인 뒤에만 뜻이 있다. */
    if (s->state != SESSION_READY) {
        return ERR_NOT_LOGGED_IN;
    }
    if (!msg_is_known(hdr->type)) {
        return ERR_NOT_SUPPORTED;
    }
    /*
     * **길이를 규격과 대조한다**(T3-02). 조립기는 헤더가 말한 만큼 모았을 뿐
     * 그 길이가 옳은지는 모른다.
     */
    if (msg_body_len(hdr->type) != (int32_t)hdr->body_len) {
        return ERR_INVALID_ARG;
    }

    if (fn != NULL) {
        fn(hdr, body, ctx);
    }
    return ERR_OK;
}

int session_on_readable(session_t *s, session_frame_fn fn, void *ctx,
                        int64_t now_ms)
{
    if (s == NULL) {
        return ERR_NULL_PTR;
    }
    if (s->state == SESSION_DOWN || s->fd < 0) {
        return ERR_NOT_LOGGED_IN;
    }

    uint8_t buf[8192];
    int     handled = 0;

    for (;;) {
        ssize_t r = read(s->fd, buf, sizeof(buf));
        if (r > 0) {
            s->last_rx_ms = now_ms; /* 무엇을 받았든 살아 있다는 신호다 */
            if (framer_push(&s->rx, buf, (size_t)r) != ERR_OK) {
                session_drop(s, now_ms);
                return ERR_POOL_EXHAUSTED;
            }
        } else if (r == 0) {
            session_drop(s, now_ms); /* 상대가 끊었다 */
            return ERR_IO;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                break; /* 지금 받을 것은 다 받았다 */
            }
            session_drop(s, now_ms);
            return ERR_IO;
        }

        if ((size_t)r < sizeof(buf)) {
            break; /* 버퍼를 다 못 채웠으면 더 없다 */
        }
    }

    for (;;) {
        wire_header_t  hdr;
        const uint8_t *body = NULL;

        int got = framer_next(&s->rx, &hdr, &body);
        if (got == 0) {
            break;
        }
        if (got < 0) {
            session_drop(s, now_ms); /* 스트림이 어긋났다(T3-09) */
            return got;
        }

        int rc = handle_frame(s, &hdr, body, fn, ctx);
        if (rc != ERR_OK) {
            session_drop(s, now_ms);
            return rc;
        }
        handled++;
    }

    return handled;
}

int session_on_writable(session_t *s, int64_t now_ms)
{
    if (s == NULL) {
        return ERR_NULL_PTR;
    }
    if (s->state == SESSION_DOWN || s->fd < 0) {
        return ERR_NOT_LOGGED_IN;
    }

    int w = sendq_flush(&s->tx, s->fd);
    if (w < 0) {
        session_drop(s, now_ms);
        return ERR_IO;
    }
    return w;
}

/* --- 시간이 하는 일 --- */

int session_tick(session_t *s, int64_t now_ms)
{
    if (s == NULL) {
        return ERR_NULL_PTR;
    }
    if (s->state == SESSION_DOWN) {
        return 0; /* 다시 붙는 것은 호출부가 한다 */
    }

    /*
     * **상대가 조용한 지 오래됐으면 끊는다.** 기준은 "받은 지"다 —
     * 내가 아무리 보내도 상대가 죽었으면 소용없다.
     */
    if (now_ms - s->last_rx_ms >= s->cfg.idle_timeout_ms) {
        session_drop(s, now_ms);
        return ERR_IO;
    }

    if (s->state == SESSION_LOGGING_IN) {
        if (now_ms - s->login_at_ms >= s->cfg.login_timeout_ms) {
            session_drop(s, now_ms);
            return ERR_NOT_LOGGED_IN;
        }
        /*
         * 로그인 중에는 하트비트를 보내지 않는다. 보낼 것은 이미 보냈고,
         * 기다리는 것은 응답이다.
         */
        return 0;
    }

    /*
     * **내가 보낸 지 오래됐으면 하트비트를 보낸다.** 기준은 "보낸 지"다 —
     * 주문을 활발히 보내는 중이면 하트비트를 덧붙일 이유가 없다.
     */
    if (now_ms - s->last_tx_ms >= s->cfg.heartbeat_ms) {
        int rc = enqueue(s, MSG_HEARTBEAT, NULL, 0, now_ms);
        if (rc != ERR_OK) {
            /*
             * 큐가 찼는데 하트비트조차 못 넣는다면 이 접속은 이미 막혀 있다.
             * 무응답 한계를 기다릴 것 없이 끊는다.
             */
            session_drop(s, now_ms);
            return rc;
        }
        return 1;
    }

    return 0;
}
