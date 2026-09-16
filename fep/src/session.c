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

    seqstore_init(&s->store);
    /*
     * 상대도 1부터 센다고 본다 — 이 규격을 쓰는 양쪽이 같은 코드를 쓴다.
     * 틀려도 스스로 맞춰진다: 상대가 5부터 시작하면 첫 전문이 갭으로 잡히고,
     * 상대가 `GAP_FILL(5)`로 답해 기대값이 5로 옮겨진다.
     */
    seqtrack_init(&s->track, 1);
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

    /*
     * **큐에 넣은 뒤에 보관한다.** 순서가 반대면 큐가 차서 거절된 전문까지
     * 보관하게 되고, 그러면 보낸 적 없는 번호를 재전송해 주게 된다.
     *
     * 보관에 실패해도(너무 큰 전문) 전송은 그대로 둔다. 재전송해 줄 수 없을
     * 뿐이고, 그때는 `GAP_FILL`로 "없다"고 답한다 — 조용히 빠뜨리지 않는다.
     */
    (void)seqstore_put(&s->store, s->out_seq, frame,
                       WIRE_HEADER_LEN + body_len);

    s->out_seq++;
    s->last_tx_ms = now_ms;

    return ERR_OK;
}

/*
 * 이미 보낸 전문을 그대로 다시 큐에 넣는다.
 *
 * `enqueue`를 쓰지 않는다 — 그쪽은 새 번호를 매기지만, 재전송은 **원래 번호
 * 그대로** 나가야 한다. 번호를 새로 매기면 받는 쪽이 갭을 영영 못 메운다.
 */
static int requeue(session_t *s, uint64_t seq, int64_t now_ms)
{
    uint8_t frame[SEQSTORE_FRAME_MAX];

    int n = seqstore_get(&s->store, seq, frame, sizeof(frame));
    if (n < 0) {
        return n; /* 밀려 나갔다 */
    }

    int rc = sendq_push(&s->tx, frame, (size_t)n);
    if (rc != ERR_OK) {
        return rc;
    }
    s->last_tx_ms = now_ms;
    return ERR_OK;
}

/*
 * 상대가 요청한 구간을 다시 보낸다.
 *
 * 요청 시작점이 이미 밀려 나갔으면 **먼저 `GAP_FILL`로 "그 앞은 없다"고
 * 말한다.** 말해 주지 않으면 상대는 오지 않을 번호를 영원히 기다리며 그 뒤의
 * 전문을 전부 버린다 — 양쪽 다 살아 있는데 아무것도 흐르지 않는 접속이 된다.
 */
static int resend_from(session_t *s, uint64_t from_seq, int64_t now_ms)
{
    if (from_seq == 0) {
        return ERR_INVALID_ARG;
    }

    uint64_t oldest = seqstore_oldest(&s->store);

    if (oldest == 0 || from_seq < oldest) {
        /*
         * 들고 있는 것이 없거나, 요청이 우리가 가진 것보다 앞이다.
         * 어디서부터 이어야 하는지 알려 준다. 보관이 비었으면 **다음에 보낼
         * 번호**가 이을 자리다.
         */
        uint64_t next = (oldest == 0) ? s->out_seq : oldest;

        msg_gap_fill_t gf;
        memset(&gf, 0, sizeof(gf));
        gf.next_seq = next;

        uint8_t body[MSG_GAP_FILL_LEN];
        int     n = msg_encode_gap_fill(&gf, body, sizeof(body));
        if (n < 0) {
            return n;
        }
        int rc = enqueue(s, MSG_GAP_FILL, body, (size_t)n, now_ms);
        if (rc != ERR_OK) {
            return rc;
        }

        from_seq = next;
    }

    /* 있는 것을 순서대로 다시 보낸다. */
    for (uint64_t seq = from_seq; seq < s->out_seq; seq++) {
        int rc = requeue(s, seq, now_ms);
        if (rc == ERR_NOT_FOUND) {
            continue; /* 그 번호는 보관하지 않은 전문이다 */
        }
        if (rc != ERR_OK) {
            /*
             * 큐가 찼다. 여기서 멈추면 상대는 반쯤 채워진 채 기다리게 되므로
             * **끊는다** — 다시 붙으면 상대가 갭을 새로 감지해 다시 요청한다.
             */
            return rc;
        }
    }

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
     * **"메우는 중"은 접속에 딸린 상태다.** 앞 접속에서 보낸 재전송 요청은
     * 그 접속과 함께 사라졌으므로, 여기서 풀지 않으면 다시 붙어 갭을 봐도
     * `SEQ_WAIT`으로 삼켜 **다시는 요청하지 않으면서 영원히 기다린다.**
     * 양쪽 다 살아 있는데 아무것도 흐르지 않는 접속이 된다.
     *
     * 반대로 **기대값과 보관은 지우지 않는다.** 기대값을 지우면 접속 사이에
     * 놓친 전문을 영영 모르게 되는데, 갭이 생기는 자리가 바로 거기다
     * (`seqtrack.h` 참조). 보관도 남겨야 상대가 재접속 뒤 요청했을 때 답한다.
     *
     * 프레이머·송신 큐를 여기서 비우는 것과 같은 이유다 — 접속과 함께 죽는
     * 것은 접속과 함께 버린다.
     */
    s->track.recovering = false;

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

static bool is_session_frame(uint8_t type)
{
    return type == MSG_HEARTBEAT || type == MSG_LOGIN_REQ ||
           type == MSG_LOGIN_ACK || type == MSG_RESEND_REQ ||
           type == MSG_GAP_FILL;
}

static int handle_frame(session_t *s, const wire_header_t *hdr,
                        const uint8_t *body, session_frame_fn fn, void *ctx,
                        int64_t now_ms)
{
    /*
     * ======================================================================
     * 번호는 **모든 전문**에 대해 본다. 그러나 막는 것은 업무 전문뿐이다
     * ======================================================================
     *
     * 상대는 하트비트에도 번호를 매긴다(우리도 그렇다). 업무 전문만 세면
     * 기대값이 금세 어긋나 멀쩡한 흐름이 갭으로 보인다. 그래서 세는 것은 전부다.
     *
     * 하지만 **세션 전문까지 갭에 막으면 빠져나올 수 없다.** 재접속한 뒤
     * 상대의 `LOGIN_ACK`은 이미 갭 너머의 번호를 달고 온다 — 그것을 막으면
     * 로그인이 끝나지 않고, 로그인이 안 끝나면 재전송도 못 받는다.
     * `GAP_FILL`도 마찬가지다. 갭에서 빠져나오는 열쇠를 갭 안에 가두는 셈이다.
     *
     * 그래서 판정은 모두에게 하되, **버리는 것은 업무 전문뿐**이다.
     * 세션 전문의 중복은 해가 없다 — 늦은 `LOGIN_ACK`은 상태 검사가 무시하고,
     * 되돌리는 `GAP_FILL`은 `seqtrack_skip_to`가 거절한다.
     */
    seq_verdict_t verdict = seqtrack_on(&s->track, hdr->seq);

    if (verdict == SEQ_GAP) {
        msg_resend_req_t req;
        memset(&req, 0, sizeof(req));
        req.from_seq = seqtrack_expected(&s->track);

        uint8_t rbody[MSG_RESEND_REQ_LEN];
        int     n = msg_encode_resend_req(&req, rbody, sizeof(rbody));
        if (n < 0) {
            return n;
        }
        int rc = enqueue(s, MSG_RESEND_REQ, rbody, (size_t)n, now_ms);
        if (rc != ERR_OK) {
            return rc;
        }
    }

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
        /*
         * **로그인 응답의 번호가 기대값보다 작으면 상대가 재기동한 것이다.**
         *
         * 한 상대의 번호는 커지기만 한다. 더 작은 번호가 왔다는 것은 그
         * 번호를 매기던 상대가 더는 없다는 뜻이고, 그때는 메울 갭도 없다 —
         * 예전 전문을 가진 쪽이 사라졌기 때문이다.
         *
         * 이것을 처리하지 않으면 재기동한 상대의 **모든 전문이 중복으로
         * 버려진다.** T3-15의 통합 테스트가 그 증상으로 이 구멍을 찾았다.
         * 기대값보다 크거나 같으면 그것은 재시작이 아니라 갭이므로
         * `seqtrack_restart_at`이 아무것도 하지 않는다(T3-12가 그대로 산다).
         */
        (void)seqtrack_restart_at(&s->track, hdr->seq + 1);

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

    case MSG_RESEND_REQ: {
        /* 상대가 갭을 만났다. 보관하고 있는 것을 다시 보낸다. */
        msg_resend_req_t req;
        int              rc = msg_decode_resend_req(body, hdr->body_len, &req);
        if (rc < 0) {
            return rc;
        }
        return resend_from(s, req.from_seq, now_ms);
    }

    case MSG_GAP_FILL: {
        /*
         * 상대가 "그 앞은 더 없다"고 알려 왔다. 기다리기를 그만두고 그 번호로
         * 건너뛴다. **이것이 없으면 우리는 오지 않을 번호를 영원히 기다린다.**
         */
        msg_gap_fill_t gf;
        int            rc = msg_decode_gap_fill(body, hdr->body_len, &gf);
        if (rc < 0) {
            return rc;
        }
        /*
         * 되돌리는 요청은 `seqtrack_skip_to`가 거절한다. 거절돼도 접속을
         * 끊지는 않는다 — 늦게 도착한 `GAP_FILL`일 수 있고, 그때 이미
         * 우리는 앞서 나가 있다.
         */
        (void)seqtrack_skip_to(&s->track, gf.next_seq);
        return ERR_OK;
    }

    default:
        break;
    }

    /* 여기까지 왔다면 업무 전문이다. */
    if (is_session_frame(hdr->type)) {
        return ERR_NOT_SUPPORTED; /* 위 switch가 다 잡았어야 한다 */
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

    /*
     * **순서가 맞는 것만 위로 올린다.** 중복(SEQ_DUP)은 두 번 처리되면
     * 체결이 두 번 잡히고, 갭 뒤의 것(SEQ_WAIT/SEQ_GAP)은 순서가 뒤바뀐 채
     * 올라간다. 둘 다 버린다 — 상대가 다시 보내 준다.
     */
    if (verdict != SEQ_OK) {
        return ERR_OK;
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

        int rc = handle_frame(s, &hdr, body, fn, ctx, now_ms);
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

/* --- 시퀀스 지표 --- */

uint64_t session_expected_seq(const session_t *s)
{
    return (s != NULL) ? seqtrack_expected(&s->track) : 0;
}

bool session_recovering(const session_t *s)
{
    return (s != NULL) && seqtrack_recovering(&s->track);
}

int64_t session_gaps(const session_t *s)
{
    return (s != NULL) ? s->track.gaps : 0;
}

int64_t session_dups(const session_t *s)
{
    return (s != NULL) ? s->track.dups : 0;
}
