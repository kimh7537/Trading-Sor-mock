/*
 * T1-04 이중 해제 감지.
 *
 * 이중 해제는 외부 입력이 아니라 엔진 내부의 버그다 — 풀에 order_t*를 넘기는 것은
 * 우리 코드뿐이다. 그래서 에러 코드가 아니라 assert로 잡는다.
 * assert가 실제로 터지는지는 프로세스가 죽는 것으로만 확인할 수 있으므로 이 파일만
 * 따로 둔다. ctest의 WILL_FAIL은 시그널로 죽은 테스트를 뒤집어 주지 않으므로
 * SIGABRT를 직접 받아 정상 종료로 바꾼다 — assert가 터지면 통과, 안 터지면 실패.
 */
#include <assert.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>

#include "order.h"

static void on_abort(int sig)
{
    (void)sig;
    _Exit(0); /* 기대한 대로 assert가 터졌다 */
}

int main(void)
{
    order_pool_t *pool = order_pool_create(4);
    assert(pool != NULL);

    order_t *order = order_pool_acquire(pool);
    assert(order != NULL);

    order_pool_release(pool, order);

    signal(SIGABRT, on_abort);
    order_pool_release(pool, order); /* 여기서 죽어야 한다 */

    /* 여기까지 왔다는 것은 이중 해제를 놓쳤다는 뜻이다. */
    return 1;
}
