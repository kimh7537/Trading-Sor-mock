#include "price_level.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "errors.h"

/*
 * 주문이 정말 이 레벨에 매달려 있는지 O(1)에 본다.
 * 앞뒤 링크가 서로를 가리키는지, 끝이면 레벨의 head/tail인지만 보면 된다.
 * 리스트를 훑지 않으므로 핫 패스에 남겨도 된다.
 */
static void assert_linked(const price_level_t *level, const order_t *order)
{
    assert(order->prev != NULL ? order->prev->next == order
                               : level->head == order);
    assert(order->next != NULL ? order->next->prev == order
                               : level->tail == order);
    (void)level;
    (void)order;
}

int level_push_back(price_level_t *level, order_t *order)
{
    if (level == NULL || order == NULL) {
        return ERR_NULL_PTR;
    }

    qty_t rem = order_remaining_qty(order);
    if (rem <= 0) {
        return ERR_INVALID_QTY;
    }
    /* 총 잔량이 int32를 넘으면 불변조건 자체가 깨진다. 넘기 전에 거부한다. */
    if (rem > INT32_MAX - level->total_qty) {
        return ERR_BOOK_FULL;
    }
    /* 이미 다른 리스트에 매달린 주문이면 그쪽 링크가 끊긴다 — 내부 버그다. */
    assert(order->prev == NULL && order->next == NULL);

    order->prev = level->tail;
    order->next = NULL;
    if (level->tail != NULL) {
        level->tail->next = order;
    } else {
        level->head = order;
    }
    level->tail = order;

    level->total_qty += rem;
    level->order_count++;

    return ERR_OK;
}

order_t *level_pop_front(price_level_t *level)
{
    if (level == NULL || level->head == NULL) {
        return NULL;
    }

    order_t *order = level->head;
    (void)level_remove(level, order);
    return order;
}

int level_remove(price_level_t *level, order_t *order)
{
    if (level == NULL || order == NULL) {
        return ERR_NULL_PTR;
    }
    assert_linked(level, order);

    if (order->prev != NULL) {
        order->prev->next = order->next;
    } else {
        level->head = order->next;
    }
    if (order->next != NULL) {
        order->next->prev = order->prev;
    } else {
        level->tail = order->prev;
    }
    order->prev = NULL;
    order->next = NULL;

    level->total_qty -= order_remaining_qty(order);
    level->order_count--;

    assert(level->total_qty >= 0);
    assert(level->order_count >= 0);
    assert((level->order_count == 0) == (level->head == NULL));
    assert((level->head == NULL) == (level->tail == NULL));

    return ERR_OK;
}

int level_reduce_qty(price_level_t *level, order_t *order, qty_t qty)
{
    if (level == NULL || order == NULL) {
        return ERR_NULL_PTR;
    }
    assert_linked(level, order);

    qty_t rem = order_remaining_qty(order);
    /* 엄격히 부분 체결만 받는다. 거절해도 아무 상태가 바뀌지 않으므로
     * assert로 죽이지 않고 에러로 알린다 — 호출부가 반환값을 확인한다. */
    if (qty <= 0 || qty >= rem) {
        return ERR_INVALID_QTY;
    }

    order->filled_qty += qty;
    level->total_qty -= qty;

    assert(level->total_qty >= 0);

    return ERR_OK;
}
