#ifndef MINI_SOR_REMOTE_VENUE_H
#define MINI_SOR_REMOTE_VENUE_H

#include <stdbool.h>
#include <stdint.h>

#include "executor.h"
#include "msg.h"
#include "ordmap.h"
#include "session.h"
#include "types.h"

/*
 * 원격 거래소 — **원장이 FEP 세션으로 다리를 보낸다** (T12-02).
 *
 * ===========================================================================
 * 왜 있는가
 * ===========================================================================
 *
 * T6-03 "최소 연결"은 SOR과 매칭 엔진을 원장 프로세스 안에 넣었다. 그 구성에서는
 * `fep/`가 한 번도 돌지 않는다 — 조립해 두고 테스트로만 확인한 계층이 된다.
 * 증권사 계층을 재현하는 것이 이 프로젝트의 전제이므로, 거래소를 진짜 프로세스로
 * 떼어 내고(`exchanged`, T12-01) 그 사이를 FEP로 잇는 구성을 선택지로 둔다.
 *
 * **기본은 바뀌지 않는다.** `ledgerd`에 `--exchange`를 주지 않으면 지금까지와
 * 똑같이 프로세스 안에서 돈다. 측정 결과(`bench/results/`)의 전제가 그 구성이다.
 *
 * ===========================================================================
 * 보내는 방법만 다르다
 * ===========================================================================
 *
 * 돌아온 결과를 매핑에 적고 논리 주문의 상태를 합산하는 일은 in-process와 한 글자도
 * 다르지 않아야 한다. 다르면 `--exchange` 하나로 체결이 갈린다. 그래서 집행기의
 * 뒷일을 복사하지 않고 `exec_submit_via()`에 **다리를 보내는 함수만** 갈아끼운다.
 *
 * ===========================================================================
 * 동기로 기다린다
 * ===========================================================================
 *
 * 원장은 전문 하나를 받아 답할 때까지 그 자리에서 끝낸다(`listener_run`). 그래서
 * 다리를 보내고 **그 자리에서 응답을 기다린다** — 세션 상태 기계는 논블로킹이지만
 * 그것을 돌리는 것은 이 파일의 `poll` 루프다.
 *
 * 상한이 있다(`REMOTE_VENUE_TIMEOUT_MS`). 닿으면 `ERR_IO`로 답하고 그 다리를
 * **판정 보류**로 남긴다(`ordmap_on_disconnect`, T3-14) — 닿았는지 모르는 주문을
 * 거부로 단정하면 우리가 모르는 포지션이 생긴다.
 *
 * ===========================================================================
 * 여기는 시각을 읽는다
 * ===========================================================================
 *
 * `CLAUDE.md`의 결정성 규칙은 **매칭 엔진과 SOR 엔진**에 건 것이다. 이 파일은
 * 그 둘이 아니라 소켓 계층이고, 하트비트와 무응답 판정은 실제로 흐른 시간으로만
 * 잴 수 있다. 논리 시각으로는 "상대가 3초째 조용하다"를 말할 방법이 없다.
 *
 * 그래서 이 구성으로 낸 체결은 **재현되지 않을 수 있다.** 전략 비교(가격)를
 * in-process로 계속 재는 이유가 그것이다(T12-04).
 */

/* 다리 하나를 기다릴 상한. T3-14의 미응답 판정이 이 시간 뒤에 걸린다. */
#define REMOTE_VENUE_TIMEOUT_MS 3000

/* 한 번에 `poll`로 기다릴 시간. 이 간격으로 하트비트도 함께 돈다. */
#define REMOTE_VENUE_POLL_MS 50

/* 호스트 이름 길이. "127.0.0.1"이 보통이다. */
#define REMOTE_HOST_MAX 64

/* 거래소 한 곳과의 접속. */
typedef struct {
    session_t s;
    /* 우리 번호 <-> 거래소 번호. **거래소마다 따로다** — 번호 체계가 다르다 */
    ordmap_t om;
    int      fd; /* -1이면 없음 */
    char     host[REMOTE_HOST_MAX];
    uint16_t port;
    bool     configured; /* `--exchange`가 이 시장을 지정했나 */
    bool     up;         /* 붙어서 로그인까지 끝났나 */
} remote_link_t;

typedef struct remote_venues {
    remote_link_t link[MARKET_COUNT];
    char          symbol[MSG_SYMBOL_LEN + 1];
    char          account[MSG_ACCOUNT_LEN + 1];
} remote_venues_t;

/* 빈 상태로 만든다. 아직 어느 시장도 지정되지 않았다. */
void remote_venues_init(remote_venues_t *rv, const char *symbol);

/*
 * 시장 하나가 어디에 있는지 적는다. 아직 붙지 않는다.
 * 같은 시장을 두 번 적으면 뒤엣것이 이긴다.
 */
int remote_venues_set(remote_venues_t *rv, market_t m, const char *host,
                      uint16_t port);

/* 하나라도 지정됐나. 거짓이면 이 구성이 아니다(in-process로 간다). */
bool remote_venues_any(const remote_venues_t *rv);

/*
 * 지정된 시장에 붙고 로그인까지 끝낸다.
 *
 * **하나라도 못 붙으면 에러다.** 반쪽만 붙은 채로 돌면 SOR이 고른 시장에 따라
 * 주문이 되기도 하고 안 되기도 한다 — 그것은 측정도 시연도 못 한다.
 */
int remote_venues_connect(remote_venues_t *rv);

/*
 * 한가할 때 불러 준다. 하트비트를 내보내고 상대의 것을 받는다.
 *
 * **부르지 않으면 세션이 말라 죽는다** — 주문 사이가 무응답 한계(15초)보다 길면
 * 다음 주문에서 세션이 끊긴 것을 발견한다.
 */
void remote_venues_pump(remote_venues_t *rv);

void remote_venues_close(remote_venues_t *rv);

/*
 * `exec_submit`과 같은 답을 낸다. 다리가 소켓을 건너갈 뿐이다.
 *
 * 거래소는 체결 **목록**을 싣지 않고 수량과 금액만 싣는다(`MSG_LEG_ACK`).
 * 평균가로 줄이면 나눗셈 나머지가 새므로(T7-07) 금액을 i64로 그대로 받아
 * 집행기가 되짚는다.
 */
int remote_submit(remote_venues_t *rv, order_map_t *map, const order_t *req,
                  const exec_plan_t *plan, exec_report_t *out);

/*
 * `exec_cancel`과 같은 답을 낸다.
 *
 * **거래소 번호로 취소한다.** 아직 응답을 못 받은 다리는 우리 번호로 보내고
 * (`ordmap_cancel_key`가 0을 준다), 거래소가 못 받았다면 "없는 주문"으로 깔끔히
 * 거부된다 — 어느 쪽이든 모호함이 남지 않는다(T3-13).
 */
int remote_cancel(remote_venues_t *rv, order_map_t *map, order_id_t logical_id,
                  ts_t ts, cancel_report_t *out);

/*
 * 그 시장 거래소의 호가창을 받아 온다.
 *
 * **이 구성에서는 호가창이 저쪽에 있다.** 원장 프로세스 안의 호가창을 보여 주면
 * 주문이 간 곳과 화면이 보는 곳이 달라진다.
 */
int remote_book(remote_venues_t *rv, market_t m, const char *symbol,
                msg_book_ack_t *out);

#endif /* MINI_SOR_REMOTE_VENUE_H */
