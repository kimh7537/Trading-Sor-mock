#include "order_index.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "errors.h"

/*
 * 부하율 상한. 선형 탐사는 부하율이 0.7을 넘으면 탐사 길이가 급격히 길어진다.
 * 0.5로 잡아 칸을 두 배 잡는다 — 칸 하나가 16바이트라 10만 건이라도 4MB다.
 */
#define INDEX_LOAD_NUM 1
#define INDEX_LOAD_DEN 2

typedef struct {
    order_id_t key; /* ORDER_ID_INVALID(0)이면 빈 칸 */
    order_t *val;
} index_slot_t;

struct order_index {
    index_slot_t *slots;
    int32_t mask;     /* 칸 수 - 1. 칸 수는 2의 거듭제곱이라 나머지 대신 AND를 쓴다 */
    int32_t capacity; /* 담을 수 있는 주문 수 상한 */
    int32_t count;
};

/*
 * splitmix64 마무리 함수. 주문번호는 대개 1씩 증가하는 연속값이라 그대로 쓰면
 * 인접 번호가 인접 칸에 몰려 무리(cluster)가 길어진다.
 * 시드도 난수도 쓰지 않으므로 같은 입력에 항상 같은 배치가 나온다 — 결정성 유지.
 */
static uint64_t hash_id(order_id_t id)
{
    uint64_t x = (uint64_t)id;

    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static int32_t slot_of(const order_index_t *idx, order_id_t id)
{
    return (int32_t)(hash_id(id) & (uint64_t)idx->mask);
}

/* id가 있으면 그 칸, 없으면 -1. */
static int32_t find_slot(const order_index_t *idx, order_id_t id)
{
    int32_t i = slot_of(idx, id);

    while (idx->slots[i].key != ORDER_ID_INVALID) {
        if (idx->slots[i].key == id) {
            return i;
        }
        i = (i + 1) & idx->mask;
    }
    return -1;
}

order_index_t *index_create(int32_t capacity)
{
    if (capacity <= 0) {
        return NULL;
    }

    /* 부하율 상한을 지키는 가장 작은 2의 거듭제곱. */
    int64_t want = (int64_t)capacity * INDEX_LOAD_DEN / INDEX_LOAD_NUM;
    int64_t buckets = 1;
    while (buckets < want) {
        if (buckets > INT32_MAX / 4) {
            return NULL;
        }
        buckets *= 2;
    }

    order_index_t *idx = calloc(1, sizeof(*idx));
    if (idx == NULL) {
        return NULL;
    }
    idx->slots = calloc((size_t)buckets, sizeof(*idx->slots));
    if (idx->slots == NULL) {
        free(idx);
        return NULL;
    }
    idx->mask = (int32_t)(buckets - 1);
    idx->capacity = capacity;
    idx->count = 0;

    return idx;
}

void index_destroy(order_index_t *idx)
{
    if (idx == NULL) {
        return;
    }
    free(idx->slots);
    free(idx);
}

int index_put(order_index_t *idx, order_id_t id, order_t *order)
{
    if (idx == NULL || order == NULL) {
        return ERR_NULL_PTR;
    }
    if (id == ORDER_ID_INVALID) {
        return ERR_INVALID_ARG;
    }
    if (idx->count >= idx->capacity) {
        return ERR_POOL_EXHAUSTED;
    }

    int32_t i = slot_of(idx, id);
    while (idx->slots[i].key != ORDER_ID_INVALID) {
        if (idx->slots[i].key == id) {
            return ERR_DUPLICATE;
        }
        i = (i + 1) & idx->mask;
    }

    idx->slots[i].key = id;
    idx->slots[i].val = order;
    idx->count++;

    /* 부하율 상한을 지키므로 빈 칸이 반드시 남는다 — 탐사가 무한히 돌지 않는 근거. */
    assert(idx->count <= idx->mask);

    return ERR_OK;
}

order_t *index_get(const order_index_t *idx, order_id_t id)
{
    if (idx == NULL || id == ORDER_ID_INVALID) {
        return NULL;
    }

    int32_t i = find_slot(idx, id);
    return i >= 0 ? idx->slots[i].val : NULL;
}

int index_remove(order_index_t *idx, order_id_t id)
{
    if (idx == NULL) {
        return ERR_NULL_PTR;
    }
    if (id == ORDER_ID_INVALID) {
        return ERR_INVALID_ARG;
    }

    int32_t hole = find_slot(idx, id);
    if (hole < 0) {
        return ERR_NOT_FOUND;
    }

    /*
     * 역방향 시프트 삭제(Knuth 6.4 알고리즘 R).
     * 빈 칸을 만든 뒤 그 뒤에 이어지는 무리를 훑어, 제자리보다 뒤로 밀려 있던 항목을
     * 빈 칸으로 당겨 온다. 톰스톤 없이 탐사 사슬이 끊기지 않게 유지한다.
     */
    int32_t probe = hole;
    for (;;) {
        idx->slots[hole].key = ORDER_ID_INVALID;
        idx->slots[hole].val = NULL;

        for (;;) {
            probe = (probe + 1) & idx->mask;
            if (idx->slots[probe].key == ORDER_ID_INVALID) {
                idx->count--;
                return ERR_OK;
            }

            int32_t home = slot_of(idx, idx->slots[probe].key);
            /* home이 (hole, probe] 안이면 probe는 제자리다 — 당기면 못 찾게 된다. */
            bool settled = (hole <= probe) ? (hole < home && home <= probe)
                                           : (hole < home || home <= probe);
            if (!settled) {
                break;
            }
        }

        idx->slots[hole] = idx->slots[probe];
        hole = probe;
    }
}

int32_t index_count(const order_index_t *idx)
{
    return idx != NULL ? idx->count : 0;
}
