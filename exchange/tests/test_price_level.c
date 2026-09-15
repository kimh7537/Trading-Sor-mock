/*
 * T1-05 가격 레벨 FIFO.
 *
 * 이 모듈의 유일한 불변조건은 "total_qty == 소속 주문 잔량의 합"이다.
 * 링크가 어긋나면 합계는 맞는데 순서가 틀리거나 그 반대가 되므로,
 * 매 연산 뒤에 리스트를 실제로 훑어서 합계·개수·앞뒤 링크·head/tail을 전부 대조한다.
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "price_level.h"

#define POOL_CAP 16

/* 리스트를 직접 훑어 캐시된 값과 대조한다. 테스트에서만 하는 O(n) 검사다. */
static void check_invariant(const price_level_t *level)
{
    qty_t sum = 0;
    int32_t count = 0;
    const order_t *prev = NULL;

    for (const order_t *o = level->head; o != NULL; o = o->next) {
        assert(o->prev == prev); /* 뒤 링크가 앞 링크와 맞물려야 한다 */
        sum += order_remaining_qty(o);
        count++;
        prev = o;
        assert(count <= POOL_CAP); /* 순환하면 여기서 멈춘다 */
    }

    assert(level->tail == prev);
    assert(level->total_qty == sum);
    assert(level->order_count == count);
    assert((level->head == NULL) == (level->tail == NULL));
}

static order_t *make_order(order_pool_t *pool, order_id_t id, qty_t qty)
{
    order_t *o = order_pool_acquire(pool);
    assert(o != NULL);
    o->id = id;
    o->qty = qty;
    o->price = 10000;
    return o;
}

/* 삽입 순서 보존 — 시간 우선 원칙 그 자체다 */
static void test_fifo_order(order_pool_t *pool)
{
    price_level_t level = {0};
    check_invariant(&level);
    assert(level_pop_front(&level) == NULL); /* 빈 레벨 */

    order_t *o[4];
    for (int i = 0; i < 4; i++) {
        o[i] = make_order(pool, (order_id_t)(i + 1), (qty_t)((i + 1) * 100));
        assert(level_push_back(&level, o[i]) == ERR_OK);
        check_invariant(&level);
    }
    assert(level.total_qty == 100 + 200 + 300 + 400);
    assert(level.order_count == 4);
    assert(level.head == o[0] && level.tail == o[3]);

    for (int i = 0; i < 4; i++) {
        order_t *popped = level_pop_front(&level);
        assert(popped == o[i]); /* 넣은 순서 그대로 */
        assert(popped->prev == NULL && popped->next == NULL);
        check_invariant(&level);
    }
    assert(level.total_qty == 0);
    assert(level_pop_front(&level) == NULL);

    for (int i = 0; i < 4; i++) {
        order_pool_release(pool, o[i]);
    }
}

/* 중간 제거 — 앞뒤가 서로를 잇고 나머지 순서는 그대로여야 한다 */
static void test_remove_middle(order_pool_t *pool)
{
    price_level_t level = {0};
    order_t *o[5];
    for (int i = 0; i < 5; i++) {
        o[i] = make_order(pool, (order_id_t)(i + 1), 100);
        assert(level_push_back(&level, o[i]) == ERR_OK);
    }
    check_invariant(&level);

    assert(level_remove(&level, o[2]) == ERR_OK); /* 한가운데 */
    check_invariant(&level);
    assert(level.total_qty == 400 && level.order_count == 4);
    assert(o[1]->next == o[3] && o[3]->prev == o[1]);

    assert(level_remove(&level, o[0]) == ERR_OK); /* head */
    check_invariant(&level);
    assert(level.head == o[1] && o[1]->prev == NULL);

    assert(level_remove(&level, o[4]) == ERR_OK); /* tail */
    check_invariant(&level);
    assert(level.tail == o[3] && o[3]->next == NULL);

    assert(level_pop_front(&level) == o[1]);
    assert(level_pop_front(&level) == o[3]);
    assert(level.head == NULL && level.tail == NULL);
    assert(level.total_qty == 0 && level.order_count == 0);
    check_invariant(&level);

    for (int i = 0; i < 5; i++) {
        order_pool_release(pool, o[i]);
    }
}

/* 부분 체결 후 합계 일치 — qty는 그대로, filled_qty만 는다 */
static void test_reduce_qty(order_pool_t *pool)
{
    price_level_t level = {0};
    order_t *a = make_order(pool, 1, 500);
    order_t *b = make_order(pool, 2, 300);
    assert(level_push_back(&level, a) == ERR_OK);
    assert(level_push_back(&level, b) == ERR_OK);
    assert(level.total_qty == 800);

    assert(level_reduce_qty(&level, a, 200) == ERR_OK);
    check_invariant(&level);
    assert(a->qty == 500);        /* 원 주문 수량은 보존 */
    assert(a->filled_qty == 200);
    assert(order_remaining_qty(a) == 300);
    assert(level.total_qty == 600);
    assert(level.order_count == 2); /* 부분 체결은 줄에서 빠지지 않는다 */
    assert(level.head == a);        /* 시간 우선순위도 그대로 */

    assert(level_reduce_qty(&level, a, 100) == ERR_OK); /* 두 번째 부분 체결 */
    check_invariant(&level);
    assert(order_remaining_qty(a) == 200);
    assert(level.total_qty == 500);

    /* 잔량 전부/초과는 받지 않는다. 전량 체결은 호출부가 pop_front로 한다 */
    assert(level_reduce_qty(&level, a, 200) == ERR_INVALID_QTY);
    assert(level_reduce_qty(&level, a, 999) == ERR_INVALID_QTY);
    assert(level_reduce_qty(&level, a, 0) == ERR_INVALID_QTY);
    assert(level_reduce_qty(&level, a, -5) == ERR_INVALID_QTY);
    check_invariant(&level);
    assert(level.total_qty == 500); /* 거부된 요청은 아무것도 바꾸지 않는다 */

    /* 잔량만큼 남은 주문을 떼면 합계에서 잔량만큼만 빠진다 */
    assert(level_remove(&level, a) == ERR_OK);
    check_invariant(&level);
    assert(level.total_qty == 300 && level.order_count == 1);

    order_pool_release(pool, a);
    order_pool_release(pool, b);
}

/* 잔량 없는 주문과 NULL 인자 */
static void test_rejects(order_pool_t *pool)
{
    price_level_t level = {0};
    order_t *o = make_order(pool, 1, 100);

    assert(level_push_back(NULL, o) == ERR_NULL_PTR);
    assert(level_push_back(&level, NULL) == ERR_NULL_PTR);
    assert(level_remove(&level, NULL) == ERR_NULL_PTR);
    assert(level_reduce_qty(&level, NULL, 1) == ERR_NULL_PTR);
    assert(level_pop_front(NULL) == NULL);

    o->filled_qty = o->qty; /* 잔량 0 */
    assert(level_push_back(&level, o) == ERR_INVALID_QTY);
    check_invariant(&level);
    assert(level.order_count == 0);

    order_pool_release(pool, o);
}

int main(void)
{
    order_pool_t *pool = order_pool_create(POOL_CAP);
    assert(pool != NULL);

    test_fifo_order(pool);
    test_remove_middle(pool);
    test_reduce_qty(pool);
    test_rejects(pool);

    order_pool_destroy(pool);
    return 0;
}
