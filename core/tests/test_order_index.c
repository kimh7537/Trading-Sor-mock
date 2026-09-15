/*
 * T1-07 주문 인덱스.
 *
 * 위험 지점은 삭제다. 선형 탐사에서 칸을 그냥 비우면 그 뒤에 밀려 있던 항목으로 가는
 * 탐사 사슬이 끊겨 멀쩡한 키를 못 찾게 된다. 역방향 시프트가 그걸 막는지 보려면
 * 무리(cluster) 한가운데를 지워 봐야 한다 — 10만 건을 넣고 홀수 번호만 지운 뒤
 * 짝수 번호가 전부 살아 있는지 확인하는 것이 그 검사다.
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "order_index.h"

#define BIG 100000

static order_t DUMMY[8];

static void test_basic(void)
{
    assert(index_create(0) == NULL);
    assert(index_create(-1) == NULL);
    index_destroy(NULL);

    order_index_t *idx = index_create(8);
    assert(idx != NULL);
    assert(index_count(idx) == 0);

    assert(index_get(idx, 1) == NULL); /* 미존재 키 */
    assert(index_remove(idx, 1) == ERR_NOT_FOUND);

    assert(index_put(idx, 1, &DUMMY[0]) == ERR_OK);
    assert(index_put(idx, 2, &DUMMY[1]) == ERR_OK);
    assert(index_count(idx) == 2);
    assert(index_get(idx, 1) == &DUMMY[0]);
    assert(index_get(idx, 2) == &DUMMY[1]);
    assert(index_get(idx, 3) == NULL);

    /* 같은 번호를 두 번 등록하지 않는다 */
    assert(index_put(idx, 1, &DUMMY[2]) == ERR_DUPLICATE);
    assert(index_get(idx, 1) == &DUMMY[0]); /* 덮어쓰지 않았다 */
    assert(index_count(idx) == 2);

    /* 0은 '없음'이므로 키로 쓸 수 없다 */
    assert(index_put(idx, ORDER_ID_INVALID, &DUMMY[0]) == ERR_INVALID_ARG);
    assert(index_get(idx, ORDER_ID_INVALID) == NULL);
    assert(index_remove(idx, ORDER_ID_INVALID) == ERR_INVALID_ARG);

    assert(index_put(NULL, 1, &DUMMY[0]) == ERR_NULL_PTR);
    assert(index_put(idx, 1, NULL) == ERR_NULL_PTR);
    assert(index_get(NULL, 1) == NULL);
    assert(index_remove(NULL, 1) == ERR_NULL_PTR);
    assert(index_count(NULL) == 0);

    /* 지운 자리에 다시 넣을 수 있다 */
    assert(index_remove(idx, 1) == ERR_OK);
    assert(index_get(idx, 1) == NULL);
    assert(index_count(idx) == 1);
    assert(index_put(idx, 1, &DUMMY[3]) == ERR_OK);
    assert(index_get(idx, 1) == &DUMMY[3]);

    index_destroy(idx);
}

/* 용량을 넘으면 크래시가 아니라 에러다 */
static void test_exhaustion(void)
{
    order_index_t *idx = index_create(4);
    assert(idx != NULL);

    for (order_id_t id = 1; id <= 4; id++) {
        assert(index_put(idx, id, &DUMMY[0]) == ERR_OK);
    }
    assert(index_count(idx) == 4);
    assert(index_put(idx, 5, &DUMMY[0]) == ERR_POOL_EXHAUSTED);

    /* 고갈 상태에서도 조회는 정상이다 */
    assert(index_get(idx, 3) == &DUMMY[0]);
    assert(index_get(idx, 5) == NULL);

    assert(index_remove(idx, 2) == ERR_OK);
    assert(index_put(idx, 5, &DUMMY[0]) == ERR_OK); /* 한 자리 나면 다시 들어간다 */

    index_destroy(idx);
}

/*
 * 10만 건. 주문번호를 1씩 늘려 가며 넣으므로 해시가 흩어 주지 않으면 한 무리로 뭉친다.
 * 그 상태에서 절반을 지우고 나머지가 전부 살아 있는지 본다.
 */
static void test_bulk(void)
{
    order_index_t *idx = index_create(BIG);
    assert(idx != NULL);

    for (order_id_t id = 1; id <= BIG; id++) {
        assert(index_put(idx, id, &DUMMY[id % 8]) == ERR_OK);
    }
    assert(index_count(idx) == BIG);

    for (order_id_t id = 1; id <= BIG; id++) {
        assert(index_get(idx, id) == &DUMMY[id % 8]);
    }
    assert(index_get(idx, 0) == NULL);
    assert(index_get(idx, BIG + 1) == NULL);

    /* 무리 한가운데를 흩어 지운다. 탐사 사슬이 끊기면 짝수 번호부터 사라진다 */
    for (order_id_t id = 1; id <= BIG; id += 2) {
        assert(index_remove(idx, id) == ERR_OK);
    }
    assert(index_count(idx) == BIG / 2);

    for (order_id_t id = 1; id <= BIG; id++) {
        if (id % 2 == 0) {
            assert(index_get(idx, id) == &DUMMY[id % 8]);
        } else {
            assert(index_get(idx, id) == NULL);
            assert(index_remove(idx, id) == ERR_NOT_FOUND);
        }
    }

    /* 지운 번호를 다시 넣어도 자리가 제대로 잡힌다 */
    for (order_id_t id = 1; id <= BIG; id += 2) {
        assert(index_put(idx, id, &DUMMY[0]) == ERR_OK);
    }
    assert(index_count(idx) == BIG);
    for (order_id_t id = 1; id <= BIG; id++) {
        assert(index_get(idx, id) != NULL);
    }

    /* 전부 지우면 빈 표로 돌아간다 */
    for (order_id_t id = 1; id <= BIG; id++) {
        assert(index_remove(idx, id) == ERR_OK);
    }
    assert(index_count(idx) == 0);
    for (order_id_t id = 1; id <= BIG; id++) {
        assert(index_get(idx, id) == NULL);
    }

    index_destroy(idx);
}

/*
 * 충돌을 일부러 만든다. 칸 16개짜리 표를 거의 채운 채로 넣고 빼기를 반복하면
 * 무리 한가운데가 계속 뚫린다 — 역방향 시프트가 틀리면 여기서 바로 키가 사라진다.
 */
static void test_collisions(void)
{
    order_index_t *idx = index_create(8); /* 부하율 0.5 -> 칸 16개 */
    assert(idx != NULL);

    order_id_t live[8];
    int n = 0;

    for (order_id_t id = 1; id <= 200; id++) {
        if (n == 8) {
            /* 가장 오래된 것을 빼고 새 것을 넣는다 */
            assert(index_remove(idx, live[0]) == ERR_OK);
            for (int i = 0; i < 7; i++) {
                live[i] = live[i + 1];
            }
            n = 7;
        }
        assert(index_put(idx, id, &DUMMY[id % 8]) == ERR_OK);
        live[n++] = id;

        /* 매번 살아 있는 키가 전부 찾아지는지 본다 */
        assert(index_count(idx) == n);
        for (int i = 0; i < n; i++) {
            assert(index_get(idx, live[i]) == &DUMMY[live[i] % 8]);
        }
    }

    index_destroy(idx);
}

int main(void)
{
    test_basic();
    test_exhaustion();
    test_collisions();
    test_bulk();
    return 0;
}
