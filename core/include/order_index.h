#ifndef MINI_SOR_ORDER_INDEX_H
#define MINI_SOR_ORDER_INDEX_H

#include <stdint.h>

#include "order.h"
#include "types.h"

/*
 * 주문번호 -> 주문 위치. 취소·정정이 주문번호만 들고 오므로 O(1) 조회가 필요하다.
 *
 * 오픈 어드레싱 + 선형 탐사. 체이닝은 노드마다 할당이 필요한데 핫 패스에서
 * malloc을 못 쓰므로 노드 풀이 하나 더 생긴다. 배열 하나면 그럴 일이 없다.
 *
 * 삭제는 톰스톤 대신 역방향 시프트다. 매칭 엔진은 주문을 끊임없이 넣고 빼므로
 * 톰스톤을 쌓으면 재해시 없이는 탐사 길이가 계속 늘어난다.
 *
 * 순회 API는 없다. 해시 순서에 의존하는 코드가 생기면 결정성이 깨진다.
 */

typedef struct order_index order_index_t;

/*
 * 동시에 담을 주문 수의 상한을 받는다. 주문 풀 용량을 그대로 주면 된다 —
 * 살아 있는 주문이 풀 용량을 넘을 수 없으므로 고갈되지 않는다.
 * capacity <= 0이거나 할당 실패면 NULL.
 */
order_index_t *index_create(int32_t capacity);
void index_destroy(order_index_t *idx);

/*
 * 등록한다. 같은 주문번호가 이미 있으면 ERR_DUPLICATE.
 * id가 ORDER_ID_INVALID면 ERR_INVALID_ARG — 0은 빈 칸 표시로 쓴다.
 * 용량을 넘으면 ERR_POOL_EXHAUSTED.
 */
int index_put(order_index_t *idx, order_id_t id, order_t *order);

/* 없으면 NULL. */
order_t *index_get(const order_index_t *idx, order_id_t id);

/* 없으면 ERR_NOT_FOUND. */
int index_remove(order_index_t *idx, order_id_t id);

/* 현재 담긴 개수. */
int32_t index_count(const order_index_t *idx);

#endif /* MINI_SOR_ORDER_INDEX_H */
