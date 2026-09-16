#ifndef MINI_SOR_EVLOOP_H
#define MINI_SOR_EVLOOP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * FEP 공통 — epoll 이벤트 루프.
 *
 * FEP는 거래소마다 하나씩 붙어 주문을 보내고 체결을 받는다. 한 프로세스가
 * 여러 접속을 동시에 다뤄야 하므로, 원장의 "한 번에 한 접속"(T3-03) 방식으로는
 * 안 된다.
 *
 * ===========================================================================
 * 레벨 트리거를 쓴다 (완료 조건이 요구한 문서화)
 * ===========================================================================
 *
 * epoll은 두 가지로 알려 줄 수 있다.
 *
 *  - **에지 트리거(ET)**: 상태가 *바뀔 때만* 알려 준다. 빠르지만 **읽을 것이
 *    남았는데 덜 읽으면 다음 알림이 오지 않는다.** `EAGAIN`이 날 때까지
 *    반드시 다 읽어야 하고, 한 번 빠뜨리면 그 접속은 조용히 멈춘다
 *  - **레벨 트리거(LT)**: 읽을 것이 남아 있는 한 계속 알려 준다
 *
 * **LT를 고른다.** ET의 이점은 fd가 수만 개일 때 깨어나는 횟수를 줄이는 것인데,
 * 이 프로젝트의 FEP는 거래소 접속 몇 개를 다룬다. 그 규모에서 ET가 버는 것은
 * 재지 않아도 될 만큼 작고, **잃는 것은 "덜 읽으면 조용히 멈춘다"는 버그
 * 종류**다. 그런 버그는 부하가 있을 때만 나타나서 재현이 어렵다.
 *
 * 이 프로젝트가 되풀이해 고른 쪽과 같은 선택이다 — **빠른 것보다 틀리기 어려운
 * 것.** 나중에 fd 수가 문제가 되면 그때 ET로 바꾸고, 그 변경은 이 파일 안에서
 * 끝난다.
 *
 * ===========================================================================
 * 논블로킹은 LT에서도 필요하다
 * ===========================================================================
 *
 * LT는 "읽을 것이 있다"까지만 보장한다. 얼마나 있는지는 말해 주지 않으므로,
 * 블로킹 소켓에 대고 원하는 만큼 읽으려 하면 **거기서 멈춘다.** 상대 하나가
 * 느리면 루프 전체가 선다. 그래서 등록하는 fd는 논블로킹이어야 한다.
 *
 * ===========================================================================
 * 콜백 안에서 fd를 닫는 경우
 * ===========================================================================
 *
 * `epoll_wait`은 이벤트를 **한 묶음으로** 돌려준다. 그 묶음 안에 같은 접속의
 * 이벤트가 여러 개 들어 있을 수 있고, 첫 번째를 처리하다 접속을 끊으면
 * **이미 닫힌 fd로 두 번째를 처리하게 된다.**
 *
 * fd 번호는 곧바로 재사용되므로, 운이 나쁘면 **엉뚱한 접속**의 콜백이 불린다.
 * 그래서 이벤트를 하나씩 꺼낼 때마다 **아직 등록되어 있는지 다시 본다.**
 * 콜백이 `evloop_del()`을 부르면 그 뒤 이벤트는 건너뛴다.
 */

/* 한 번에 꺼낼 이벤트 수의 상한. */
#define EVLOOP_EVENTS_MAX 256

/* 감시할 fd 번호의 상한. 등록 표를 fd로 색인한다. */
#define EVLOOP_FD_MAX 4096

/* 관심 있는 사건. epoll의 값을 그대로 쓰지 않고 감싼다. */
#define EV_READ 0x01u
#define EV_WRITE 0x02u
/* 아래 둘은 등록할 수 없다 — 커널이 알아서 얹어 준다. */
#define EV_ERROR 0x04u
#define EV_HANGUP 0x08u

typedef struct evloop evloop_t;

/*
 * fd 하나에 일어난 일을 처리한다.
 *
 * `events`는 EV_* 비트합이다. 콜백 안에서 `evloop_del()`을 부르고 fd를 닫아도
 * 된다 — 루프가 같은 묶음의 남은 이벤트를 건너뛴다.
 */
typedef void (*ev_fn)(int fd, uint32_t events, void *ctx);

/* 실패하면 NULL. */
evloop_t *evloop_create(void);
void evloop_destroy(evloop_t *lp);

/*
 * fd를 논블로킹으로 바꾼다. 성공하면 ERR_OK.
 *
 * 등록 전에 불러야 한다 — `evloop_add()`는 논블로킹인지 확인하고 아니면
 * 거절한다. 블로킹 fd가 섞이면 루프가 어디선가 멈추는데, 그 원인을 찾는 것이
 * 이 계층에서 가장 어려운 일이다.
 */
int evloop_set_nonblocking(int fd);

/*
 * fd를 등록한다. `events`는 EV_READ / EV_WRITE의 조합이다.
 *
 * fd가 논블로킹이 아니면 ERR_INVALID_ARG.
 * 이미 등록돼 있으면 ERR_DUPLICATE, fd가 상한을 넘으면 ERR_INVALID_ARG.
 */
int evloop_add(evloop_t *lp, int fd, uint32_t events, ev_fn fn, void *ctx);

/* 관심 사건을 바꾼다. 등록돼 있지 않으면 ERR_NOT_FOUND. */
int evloop_mod(evloop_t *lp, int fd, uint32_t events);

/*
 * 등록을 푼다. **fd를 닫지는 않는다** — 닫는 것은 호출부의 일이다.
 * 등록돼 있지 않으면 ERR_NOT_FOUND.
 */
int evloop_del(evloop_t *lp, int fd);

/* 등록된 fd 수. */
int32_t evloop_count(const evloop_t *lp);

/*
 * 한 번 기다렸다 처리한다. `timeout_ms`가 음수면 무한정 기다린다.
 *
 * 처리한 이벤트 수를 반환한다. 시그널에 깨어나면 0.
 * 인자가 잘못되면 음수 에러 코드.
 */
int evloop_once(evloop_t *lp, int timeout_ms);

/*
 * 멈출 때까지 돈다. `tick_ms`마다 깨어나 멈춤 여부를 본다.
 * 처리한 이벤트 총수를 반환한다.
 */
int evloop_run(evloop_t *lp, int tick_ms);

/*
 * 멈춤 플래그. **시그널 핸들러에서 불러도 안전하다**(T3-03과 같은 이유로
 * `volatile sig_atomic_t`만 건드린다).
 *
 * ponytail: 원장의 리스너에도 같은 모양의 플래그가 있다. 세 번째 쓰임이
 * 생기면 `core/`로 뽑는다 — 지금 합치면 두 계층이 서로를 끌고 온다.
 */
void evloop_request_stop(void);
bool evloop_stopping(void);
void evloop_reset_stop(void);

/* SIGTERM·SIGINT 핸들러를 건다. SA_RESTART를 켜지 않는다(T3-03과 같다). */
int evloop_install_signals(void);

#endif /* MINI_SOR_EVLOOP_H */
