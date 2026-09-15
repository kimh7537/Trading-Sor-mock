#include "order.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*
 * next[] 하나가 두 가지 일을 한다.
 *  - 슬롯이 비어 있으면: 다음 빈 슬롯의 인덱스 (프리리스트 링크)
 *  - 슬롯이 쓰이는 중이면: SLOT_IN_USE
 * 사용중 플래그를 따로 두지 않아도 이중 해제를 O(1)에 잡을 수 있다.
 */
#define SLOT_NIL    ((int32_t)-1)
#define SLOT_IN_USE ((int32_t)-2)

struct order_pool {
    order_t *slots;
    int32_t *next;
    int32_t  free_head;
    int32_t  capacity;
};

order_pool_t *order_pool_create(int32_t capacity)
{
    if (capacity <= 0) {
        return NULL;
    }

    order_pool_t *pool = calloc(1, sizeof(*pool));
    if (pool == NULL) {
        return NULL;
    }

    /* calloc이 개수 x 크기 오버플로를 대신 걸러 준다. */
    pool->slots = calloc((size_t)capacity, sizeof(*pool->slots));
    pool->next = calloc((size_t)capacity, sizeof(*pool->next));
    if (pool->slots == NULL || pool->next == NULL) {
        order_pool_destroy(pool);
        return NULL;
    }

    pool->capacity = capacity;
    for (int32_t i = 0; i < capacity - 1; i++) {
        pool->next[i] = i + 1;
    }
    pool->next[capacity - 1] = SLOT_NIL;
    pool->free_head = 0;

    return pool;
}

void order_pool_destroy(order_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }
    free(pool->slots);
    free(pool->next);
    free(pool);
}

order_t *order_pool_acquire(order_pool_t *pool)
{
    if (pool == NULL) {
        return NULL;
    }

    int32_t i = pool->free_head;
    if (i == SLOT_NIL) {
        return NULL;
    }
    assert(i >= 0 && i < pool->capacity);
    assert(pool->next[i] != SLOT_IN_USE);

    pool->free_head = pool->next[i];
    pool->next[i] = SLOT_IN_USE;

    order_t *order = &pool->slots[i];
    /* 패딩까지 밀어 두면 재사용 슬롯의 내용이 항상 같다 — 결정성에 필요하다. */
    memset(order, 0, sizeof(*order));
    order->id = ORDER_ID_INVALID;
    order->ts = TS_INVALID; /* 0은 유효한 논리 시각이므로 미설정과 구분한다 */

    return order;
}

void order_pool_release(order_pool_t *pool, order_t *order)
{
    if (pool == NULL || order == NULL) {
        return;
    }

    ptrdiff_t slot = order - pool->slots;
    assert(slot >= 0 && slot < pool->capacity); /* 이 풀의 슬롯이 아니다 */
    if (slot < 0 || slot >= pool->capacity) {
        return;
    }

    int32_t i = (int32_t)slot;
    assert(pool->next[i] == SLOT_IN_USE); /* 이중 해제 */
    if (pool->next[i] != SLOT_IN_USE) {
        return;
    }

    pool->next[i] = pool->free_head;
    pool->free_head = i;
}
