#ifndef MINI_SOR_LISTENER_H
#define MINI_SOR_LISTENER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "msg.h"
#include "wire.h"

/*
 * 원장 리스너 — TCP로 전문을 받는다.
 *
 * ===========================================================================
 * 결정성은 여기서 끝난다. 그 경계를 명시한다
 * ===========================================================================
 *
 * Phase 1~2의 엔진은 시스템 시각을 읽지 않는다. 같은 입력이 같은 출력을 만드는 것이
 * 전략 비교의 전제였기 때문이다. **네트워크 계층은 그럴 수 없다** — 언제 몇 바이트가
 * 도착하는지는 커널과 상대가 정한다.
 *
 * 그래서 경계를 이렇게 긋는다.
 *
 *  - **전문이 들고 오는 논리 시각(`hdr.ts`)만 쓴다.** 리스너는 자기 시각을 만들지
 *    않는다. 도착 순서가 흔들려도 처리 결과는 전문의 내용만으로 정해진다
 *  - 소켓·시그널처럼 결정적일 수 없는 것은 **이 파일 안에 가둔다.** 처리 훅은
 *    바이트 배열과 헤더만 받으므로 그 자체는 결정적으로 시험할 수 있다
 *
 * ===========================================================================
 * 포트 0을 허용한다
 * ===========================================================================
 *
 * 테스트가 고정 포트를 쓰면 CI에서 다른 프로세스와 부딪히고, 앞선 실행이 남긴
 * TIME_WAIT과도 부딪힌다. 포트 0을 주면 커널이 빈 포트를 고르고,
 * `listener_port()`로 그 번호를 되물을 수 있다.
 *
 * `SO_REUSEADDR`도 같은 이유다 — 재시작할 때 TIME_WAIT 때문에 bind가 실패하면
 * 운영에서 "조금 기다렸다 다시 켜세요"가 된다. `SO_REUSEPORT`는 **쓰지 않는다**:
 * 여러 프로세스가 같은 포트에 붙는 것은 T3-04(워커 풀)의 선택지이고, 여기서 미리
 * 정해 버리면 그 판단을 뺏는다.
 *
 * ===========================================================================
 * 종료
 * ===========================================================================
 *
 * SIGTERM·SIGINT를 받으면 플래그만 세우고 돌아온다. `accept()`는 EINTR로 깨어나고,
 * 루프가 플래그를 보고 빠져나온다. **핸들러에서 하는 일은 `volatile sig_atomic_t`에
 * 값을 쓰는 것뿐이다** — 그 밖의 거의 모든 것이 비동기 시그널 안전하지 않다.
 *
 * 그래서 `SA_RESTART`를 **켜지 않는다.** 켜면 `accept()`가 자동 재시작되어 플래그를
 * 볼 기회가 없다.
 */

/*
 * 전문 하나를 처리한다.
 *
 * body는 `hdr->body_len` 바이트이고, 종별과 길이는 이미 검증됐다.
 * 응답을 보내려면 out에 채우고 길이를 반환한다. 0을 반환하면 응답하지 않는다.
 * 음수를 반환하면 접속을 끊는다.
 *
 * ctx는 호출부의 것이고 리스너는 손대지 않는다.
 */
typedef int (*frame_handler_fn)(const wire_header_t *hdr, const uint8_t *body,
                                uint8_t *out, size_t out_cap, void *ctx);

typedef struct listener listener_t;

/*
 * 리스닝 소켓을 연다. port가 0이면 커널이 고른다.
 * 실패하면 NULL.
 */
listener_t *listener_open(uint16_t port, int backlog);

/* 실제로 묶인 포트. 열려 있지 않으면 0. */
uint16_t listener_port(const listener_t *ln);

/* 리스닝 소켓의 fd. 열려 있지 않으면 -1. T3-04가 이 fd를 물려받는다. */
int listener_fd(const listener_t *ln);

void listener_close(listener_t *ln);

/*
 * 접속을 하나 받아 끝까지 처리하고 닫는다.
 *
 * **한 번에 한 접속만 다룬다.** 동시 접속은 T3-04(워커 풀)의 일이다 — 여기서
 * 미리 다루면 그 태스크가 이미 굳은 구조를 물려받는다.
 *
 * 멈춤 요청이 있었으면 아무것도 하지 않고 0을 반환한다.
 * 처리한 전문 수를 반환하고, 오류면 음수 에러 코드.
 */
int listener_serve_one(listener_t *ln, frame_handler_fn fn, void *ctx);

/*
 * 멈출 때까지 접속을 받는다. `listener_request_stop()`이 불리면 돌아온다.
 * 처리한 접속 수를 반환한다.
 */
int listener_run(listener_t *ln, frame_handler_fn fn, void *ctx);

/*
 * 멈춤을 요청한다. **시그널 핸들러에서 불러도 안전하다** — 플래그만 쓴다.
 * 테스트에서도 같은 함수를 쓴다.
 */
void listener_request_stop(void);

/* 멈춤 요청이 있었는가. */
bool listener_stopping(void);

/* 멈춤 플래그를 되돌린다. 테스트가 여러 번 돌 때 쓴다. */
void listener_reset_stop(void);

/*
 * SIGTERM·SIGINT 핸들러를 건다. SA_RESTART를 켜지 않는다 —
 * 켜면 accept()가 자동 재시작되어 멈춤 플래그를 볼 기회가 없다.
 * 성공하면 ERR_OK.
 */
int listener_install_signals(void);

#endif /* MINI_SOR_LISTENER_H */
