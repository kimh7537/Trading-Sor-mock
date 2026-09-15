/*
 * T1-04 주문 구조체와 주문 풀.
 *
 * 검증하는 것은 셋이다.
 *  1. 고갈까지 획득 → 전량 해제 → 재획득이 다시 된다 (슬롯이 새지 않는다)
 *  2. 고갈 시 NULL을 반환한다 — 크래시하지 않는다
 *  3. 재사용된 슬롯에 이전 주문의 흔적이 남지 않는다
 * 이중 해제 감지는 assert가 터지는 것을 봐야 하므로 test_order_pool_double_free.c로
 * 분리하고 ctest에 WILL_FAIL로 등록했다.
 */
#include <assert.h>
#include <stddef.h>

#include "order.h"

#define CAP 8

/* 잘못된 용량은 NULL. 풀이 0칸으로 만들어지면 이후 전부 고갈 경로가 된다. */
static void test_create_rejects_bad_capacity(void)
{
    assert(order_pool_create(0) == NULL);
    assert(order_pool_create(-1) == NULL);
    order_pool_destroy(NULL); /* NULL 파괴는 무해해야 한다 */
}

/* 고갈까지 획득 → 전량 해제 → 재획득. 두 바퀴 모두 CAP개가 나와야 한다. */
static void test_exhaust_release_reacquire(void)
{
    order_pool_t *pool = order_pool_create(CAP);
    assert(pool != NULL);

    order_t *first[CAP];
    for (int i = 0; i < CAP; i++) {
        first[i] = order_pool_acquire(pool);
        assert(first[i] != NULL);
    }
    /* 같은 슬롯을 두 번 내주면 호가창이 조용히 깨진다 */
    for (int i = 0; i < CAP; i++) {
        for (int j = i + 1; j < CAP; j++) {
            assert(first[i] != first[j]);
        }
    }

    /* 고갈. 반복 호출해도 NULL이지 크래시가 아니다 */
    assert(order_pool_acquire(pool) == NULL);
    assert(order_pool_acquire(pool) == NULL);

    for (int i = 0; i < CAP; i++) {
        order_pool_release(pool, first[i]);
    }

    order_t *second[CAP];
    for (int i = 0; i < CAP; i++) {
        second[i] = order_pool_acquire(pool);
        assert(second[i] != NULL);
    }
    assert(order_pool_acquire(pool) == NULL);

    order_pool_destroy(pool);
}

/* 부분 해제: 돌려준 수만큼만 다시 나온다 */
static void test_partial_release(void)
{
    order_pool_t *pool = order_pool_create(CAP);
    assert(pool != NULL);

    order_t *held[CAP];
    for (int i = 0; i < CAP; i++) {
        held[i] = order_pool_acquire(pool);
        assert(held[i] != NULL);
    }

    order_pool_release(pool, held[3]);
    order_pool_release(pool, held[5]);

    assert(order_pool_acquire(pool) != NULL);
    assert(order_pool_acquire(pool) != NULL);
    assert(order_pool_acquire(pool) == NULL);

    order_pool_destroy(pool);
}

/* 재사용 슬롯은 초기 상태여야 한다. 이전 주문의 잔량이 남으면 체결이 틀어진다. */
static void test_reuse_is_reinitialized(void)
{
    order_pool_t *pool = order_pool_create(1);
    assert(pool != NULL);

    order_t *order = order_pool_acquire(pool);
    assert(order != NULL);
    order->id = 4242;
    order->client_order_id = 99;
    order->ts = 1700000000000000000;
    order->price = 71500;
    order->qty = 300;
    order->filled_qty = 120;
    order->side = SIDE_SELL;
    order->type = ORDER_IOC;
    order->market = MARKET_NXT;
    order->prev = order;  /* 링크가 남아 있으면 다음 사용자가 남의 큐에 끼어든다 */
    order->next = order;

    order_pool_release(pool, order);

    order_t *reused = order_pool_acquire(pool);
    assert(reused != NULL);
    assert(reused == order); /* 용량 1이므로 같은 슬롯이 나온다 */
    assert(reused->id == ORDER_ID_INVALID);
    assert(reused->client_order_id == 0);
    assert(reused->ts == TS_INVALID);
    assert(reused->price == 0);
    assert(reused->qty == 0);
    assert(reused->filled_qty == 0);
    assert(reused->side == SIDE_BUY);
    assert(reused->type == ORDER_LIMIT);
    assert(reused->market == MARKET_KRX);
    assert(reused->prev == NULL);
    assert(reused->next == NULL);

    order_pool_destroy(pool);
}

/* NULL 인자로 크래시하지 않는다 */
static void test_null_args(void)
{
    assert(order_pool_acquire(NULL) == NULL);

    order_pool_t *pool = order_pool_create(1);
    assert(pool != NULL);
    order_pool_release(pool, NULL);
    order_t *order = order_pool_acquire(pool);
    assert(order != NULL);
    order_pool_release(NULL, order);
    order_pool_destroy(pool);
}

int main(void)
{
    test_create_rejects_bad_capacity();
    test_exhaust_release_reacquire();
    test_partial_release();
    test_reuse_is_reinitialized();
    test_null_args();
    return 0;
}
