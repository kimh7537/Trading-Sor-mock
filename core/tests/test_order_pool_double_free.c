/*
 * T1-04 이중 해제 감지.
 *
 * 이중 해제는 외부 입력이 아니라 엔진 내부의 버그다 — 풀에 order_t*를 넘기는 것은
 * 우리 코드뿐이다. 그래서 에러 코드가 아니라 assert로 잡는다.
 *
 * 다만 `order_pool_release()`는 void 반환이라 NDEBUG 빌드에서 assert가 사라지면
 * 알릴 방법이 없다. 그래서 계약이 빌드에 따라 둘이다.
 *
 *   assert가 살아 있으면 : 즉시 abort
 *   assert가 없으면      : 무시하고 반환. **프리리스트는 멀쩡해야 한다**
 *
 * 이 테스트는 둘 다 통과로 친다. 라이브러리가 어느 쪽으로 빌드됐는지 테스트가
 * 알 수 없기 때문이다(테스트 타깃만 -UNDEBUG로 빌드된다).
 *
 * 절대 통과하면 안 되는 것은 세 번째 경우다 — 검사를 통째로 빼서 프리리스트에
 * 순환이 생기는 것. 그러면 같은 슬롯이 두 번 나오고 아래 검사가 잡는다.
 */
#include <assert.h>
#include <signal.h>
#include <stddef.h>
#include <stdlib.h>

#include "order.h"

#define CAP 4

static void on_abort(int sig)
{
    (void)sig;
    _Exit(0); /* 기대한 대로 assert가 터졌다 */
}

int main(void)
{
    order_pool_t *pool = order_pool_create(CAP);
    assert(pool != NULL);

    order_t *order = order_pool_acquire(pool);
    assert(order != NULL);

    order_pool_release(pool, order);

    signal(SIGABRT, on_abort);
    order_pool_release(pool, order); /* 여기서 죽거나, 무시되거나 */
    signal(SIGABRT, SIG_DFL);

    /*
     * 핸들러를 즉시 되돌리는 것이 중요하다. 깔아 둔 채로 두면 이 아래의 assert
     * 실패까지 "기대한 abort"로 둔갑해 테스트가 통과한다 — 실제로 그렇게 짰다가
     * 이중 해제 검사를 통째로 빼도 안 잡히는 것을 보고 고쳤다.
     *
     * 안 죽었다면 NDEBUG 빌드다. 무시되기만 했는지 — 즉 프리리스트가 성한지 본다.
     * 검사를 통째로 뺀 구현이라면 같은 슬롯이 프리리스트에 두 번 들어가 있어서
     * 용량보다 많이 나오거나 같은 포인터가 두 번 나온다.
     */
    order_t *slots[CAP];
    int n = 0;
    for (int i = 0; i < CAP; i++) {
        slots[i] = order_pool_acquire(pool);
        if (slots[i] == NULL) {
            break;
        }
        n++;
    }
    assert(n == CAP); /* 이중 해제로 슬롯이 사라지지도 늘어나지도 않았다 */
    assert(order_pool_acquire(pool) == NULL);

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            assert(slots[i] != slots[j]); /* 같은 슬롯이 두 번 나오지 않는다 */
        }
    }

    order_pool_destroy(pool);
    return 0;
}
