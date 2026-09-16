#ifndef MINI_SOR_SESSION_H
#define MINI_SOR_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "framer.h"
#include "msg.h"
#include "sendq.h"
#include "seqtrack.h"
#include "wire.h"

/*
 * FEP — 세션.
 *
 * T3-08(이벤트 루프) · T3-09(전문 조립) · T3-10(송신 큐)이 배관이었다면
 * 여기는 그 위의 **상태 기계**다. 접속 하나의 일생을 맡는다 —
 * 붙고, 로그인하고, 살아 있음을 확인하고, 끊기면 다시 붙는다.
 *
 * ===========================================================================
 * 시각을 읽지 않는다. 받는다
 * ===========================================================================
 *
 * 하트비트와 타임아웃은 "몇 초 동안"이라는 말이고, 그것은 시계를 읽는 일이다.
 * CLAUDE.md는 매칭·SOR 엔진이 시스템 시각을 읽는 것을 금지한다. FEP는 그
 * 엔진이 아니지만 **같은 선을 여기서도 긋는다.**
 *
 * 모든 진입점이 `now_ms`를 인자로 받는다. 이 파일 어디에도 `time()`·`clock()`이
 * 없다. 시계를 읽는 것은 프로세스의 주 루프이고, 한 번 읽어 아래로 내려 준다
 * (T3-03에서 리스너에 대해 그은 선과 같다 — **경계가 읽고 논리는 받는다**).
 *
 * 얻는 것이 둘이다.
 *
 *  1. **테스트에 `sleep`이 없다.** 30초 뒤의 하트비트를 확인하려고 30초를
 *     기다리지 않는다. `now_ms`에 30000을 더해 넣으면 된다. 느린 테스트는
 *     결국 안 돌리게 되고, 안 도는 테스트는 없는 것과 같다
 *  2. **같은 입력에 같은 상태 변화가 나온다.** 시계가 끼면 "가끔 실패하는
 *     테스트"가 되는데, 그것은 버그를 숨기는 가장 좋은 방법이다
 *
 * ===========================================================================
 * 소켓은 세션이 만들지 않는다
 * ===========================================================================
 *
 * 세션은 **언제 다시 붙을지**를 정하고, **붙이는 일은 호출부**가 한다.
 * `session_should_connect()`가 참이면 호출부가 소켓을 만들어 붙이고
 * `session_on_connected()`로 넘긴다.
 *
 * 이렇게 나누는 이유:
 *
 *  - 주소 해석·`connect` 진행 중 처리·소켓 옵션은 환경마다 다르다. 상태 기계가
 *    그것까지 들면 **환경 없이는 시험할 수 없는 덩어리**가 된다
 *  - 백오프 계산은 순수하게 산술이라 소켓 없이 전부 확인할 수 있다.
 *    실제로 재접속 테스트에는 소켓이 하나도 나오지 않는다
 *
 * ===========================================================================
 * 로그인 전에는 업무 전문을 보내지 않는다
 * ===========================================================================
 *
 * 상대가 세션을 인정하기 전에 보낸 주문은 **조용히 버려질 수 있다.**
 * 그러면 "보냈는데 응답이 없는 주문"이 생기는데, 그것은 T3-10이 큐가 가득 찼을
 * 때 일부러 만들지 않기로 한 모호함과 **똑같은 것**이다. 같은 이유로 여기서도
 * 만들지 않는다 — `session_send()`가 `ERR_NOT_LOGGED_IN`을 돌려주고,
 * 무엇을 할지는 호출부가 정한다.
 *
 * 하트비트와 로그인 전문 자신은 이 제한을 받지 않는다.
 *
 * ===========================================================================
 * "보낸 지"와 "받은 지"를 따로 센다
 * ===========================================================================
 *
 * 둘은 다른 질문에 답한다.
 *
 *  - **하트비트는 "내가 보낸 지"가 기준이다.** 목적이 *상대에게* 내가 살아
 *    있음을 알리는 것이기 때문이다. 내가 주문을 활발히 보내는 중이라면 굳이
 *    하트비트를 더 얹을 이유가 없다 — 어떤 전문이든 살아 있다는 신호다
 *  - **타임아웃은 "내가 받은 지"가 기준이다.** 목적이 *상대가* 살아 있는지
 *    판단하는 것이기 때문이다. 내가 아무리 보내도 상대가 죽었으면 소용없다
 *
 * 이 둘을 하나로 합치면 어느 쪽이든 틀린다. 바쁜 접속에 하트비트를 덧붙이거나,
 * 죽은 상대를 살아 있다고 보거나.
 */

/* 세션 식별자는 전문 규격을 따른다(T3-02). */
#define SESSION_ID_LEN MSG_SESSION_LEN

typedef enum {
    /* 접속이 없다. `retry_at_ms`가 되면 다시 붙을 때다. */
    SESSION_DOWN = 0,
    /* 붙었고 LOGIN_REQ를 보냈다. LOGIN_ACK를 기다린다. */
    SESSION_LOGGING_IN,
    /* 로그인됐다. 업무 전문을 주고받는다. */
    SESSION_READY
} session_state_t;

typedef struct {
    /* 이만큼 아무것도 **보내지** 않았으면 하트비트를 보낸다. */
    int32_t heartbeat_ms;
    /* 이만큼 아무것도 **받지** 못했으면 끊는다. */
    int32_t idle_timeout_ms;
    /* 로그인 응답을 이만큼 못 받으면 끊는다. */
    int32_t login_timeout_ms;
    /* 재접속 백오프. 첫 대기와 상한. */
    int32_t backoff_min_ms;
    int32_t backoff_max_ms;
} session_config_t;

/*
 * 기본값을 채운다. 하트비트 5초, 무응답 15초, 로그인 5초,
 * 백오프 200ms에서 시작해 30초까지.
 *
 * 무응답 한계를 하트비트의 정확히 2배로 두면 한 번만 늦어도 끊긴다.
 * 3배로 두어 **한 번의 지연과 진짜 단절을 구분**한다.
 */
void session_config_default(session_config_t *cfg);

typedef struct {
    session_state_t state;
    int             fd; /* -1이면 없음. 세션은 이 fd를 닫지 않는다 */

    framer_t rx;
    sendq_t  tx;

    char     session_id[SESSION_ID_LEN + 1];
    uint64_t out_seq; /* 다음에 보낼 시퀀스. 접속이 바뀌어도 이어진다 */

    /*
     * T3-12. 보낸 것은 다시 보낼 수 있게 보관하고, 받은 것은 번호를 대조한다.
     * **둘 다 접속을 넘어 살아남는다** — 갭은 접속과 접속 사이에 생기므로
     * 재접속 때 비우면 감지할 방법이 사라진다.
     */
    seqstore_t store;
    seqtrack_t track;

    int64_t last_tx_ms; /* 마지막으로 무언가 **보낸** 시각 */
    int64_t last_rx_ms; /* 마지막으로 무언가 **받은** 시각 */
    int64_t login_at_ms;
    int64_t retry_at_ms; /* DOWN일 때 다시 붙을 시각 */

    int32_t backoff_ms;
    int32_t attempts; /* 연달아 끊긴 횟수. 로그인에 성공하면 0 */

    session_config_t cfg;
} session_t;

/*
 * 받은 업무 전문을 호출부에 넘긴다.
 *
 * 로그인·하트비트는 세션이 직접 처리하므로 여기까지 오지 않는다.
 * `body`는 **다음 호출까지만 유효하다**(T3-09의 수명 규칙 그대로).
 */
typedef void (*session_frame_fn)(const wire_header_t *hdr, const uint8_t *body,
                                 void *ctx);

/*
 * 세션을 만든다. 아직 붙지 않은 상태(SESSION_DOWN)이고, `now_ms`부터 곧바로
 * 붙을 수 있다 — 첫 접속까지 기다릴 이유가 없다.
 *
 * `cfg`가 NULL이면 기본값을 쓴다.
 */
int session_init(session_t *s, const session_config_t *cfg,
                 const char *session_id, int64_t now_ms);

session_state_t session_state(const session_t *s);

/* 지금 붙어야 하는가. DOWN이고 백오프가 끝났으면 참. */
bool session_should_connect(const session_t *s, int64_t now_ms);

/*
 * 붙은 소켓을 넘긴다. 세션이 LOGIN_REQ를 큐에 넣고 SESSION_LOGGING_IN이 된다.
 *
 * fd는 **논블로킹이어야 한다**(T3-08의 규칙). 세션은 이 fd를 닫지 않는다 —
 * 연 쪽이 닫는다.
 */
int session_on_connected(session_t *s, int fd, int64_t now_ms);

/*
 * 읽을 수 있을 때 부른다. 받은 만큼 조립해 완성된 전문을 처리한다.
 *
 * 처리한 전문 수(>= 0). 상대가 끊겼거나 스트림이 어긋나면 음수 —
 * 그때 세션은 이미 DOWN이고 백오프가 잡혀 있다.
 */
int session_on_readable(session_t *s, session_frame_fn fn, void *ctx,
                        int64_t now_ms);

/* 쓸 수 있을 때 부른다. 보낸 바이트 수, 또는 음수 에러. */
int session_on_writable(session_t *s, int64_t now_ms);

/*
 * 주기적으로 부른다. 하트비트를 보낼 때가 됐는지, 상대가 조용한 지 오래됐는지,
 * 로그인 응답이 안 오는지를 본다.
 *
 * 무언가 했으면 1, 아무것도 안 했으면 0, 끊었으면 음수.
 */
int session_tick(session_t *s, int64_t now_ms);

/*
 * 업무 전문을 보낸다(바디만 준다. 헤더는 세션이 채운다).
 *
 * SESSION_READY가 아니면 ERR_NOT_LOGGED_IN. 큐가 차면 ERR_POOL_EXHAUSTED —
 * 둘 다 **아무것도 보내지 않은 상태**다.
 */
int session_send(session_t *s, uint8_t type, const uint8_t *body,
                 size_t body_len, int64_t now_ms);

/*
 * 끊는다. 다음 재접속 시각을 잡고 DOWN이 된다.
 * **fd를 닫지 않는다** — 닫는 것은 호출부의 일이다(T3-08의 `evloop_del`과 같다).
 */
void session_drop(session_t *s, int64_t now_ms);

/* EV_WRITE를 켜 두어야 하는가(T3-10의 규칙 그대로). */
bool session_want_write(const session_t *s);

/* 다음 재접속까지 남은 시간. DOWN이 아니면 -1. */
int64_t session_retry_in(const session_t *s, int64_t now_ms);

/* --- 시퀀스 (T3-12) --- */

/*
 * 다음에 받을 것으로 기대하는 번호. 갭을 메우는 중이면 빠진 첫 번호다.
 */
uint64_t session_expected_seq(const session_t *s);

/* 갭을 메우는 중인가. 이때 도착하는 전문은 위로 올라가지 않는다. */
bool session_recovering(const session_t *s);

/* 만난 갭의 수와 버린 중복의 수. 운영 지표다. */
int64_t session_gaps(const session_t *s);
int64_t session_dups(const session_t *s);

#endif /* MINI_SOR_SESSION_H */
