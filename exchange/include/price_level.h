#ifndef MINI_SOR_PRICE_LEVEL_H
#define MINI_SOR_PRICE_LEVEL_H

#include <stdint.h>

#include "order.h"

/*
 * 한 가격에 쌓인 주문들의 시간 우선 대기열(docs/SPEC.md 3.3 — 가격 우선 → 시간 우선).
 *
 * 이중 연결 리스트인 이유는 취소·정정이 큐 중간의 주문을 지목해서 들어오기 때문이다.
 * 단일 연결이면 앞 주문을 찾느라 O(n)이 된다.
 *
 * 전부 0인 상태가 곧 빈 레벨이다. 호가창이 레벨 배열을 calloc으로 잡으므로
 * 별도 초기화 함수를 두지 않는다.
 */
typedef struct price_level {
    order_t *head;        /* 가장 먼저 접수된 주문 */
    order_t *tail;        /* 가장 나중에 접수된 주문 */
    qty_t    total_qty;   /* 소속 주문 잔량의 합. 모든 연산 후 유지되는 불변조건 */
    int32_t  order_count;
} price_level_t;

/*
 * 대기열 뒤에 붙인다. 시간 우선순위는 접수 순서이므로 항상 뒤다.
 * 잔량이 없는 주문은 거부한다(ERR_INVALID_QTY) — 줄에 세울 이유가 없다.
 * total_qty가 넘칠 상황이면 ERR_BOOK_FULL.
 */
int level_push_back(price_level_t *level, order_t *order);

/* 최우선 주문을 떼어 반환한다. 비어 있으면 NULL. */
order_t *level_pop_front(price_level_t *level);

/* 임의 주문을 뗀다. O(1). 이 레벨에 없는 주문이면 내부 버그이므로 assert. */
int level_remove(price_level_t *level, order_t *order);

/*
 * 부분 체결을 반영한다. filled_qty를 늘리고 total_qty를 줄인다.
 * 원 주문 수량(qty)은 건드리지 않는다 — 평균 체결 단가를 내려면 보존해야 한다.
 * 엄격히 부분 체결만 받는다. 전량 소진은 호출부가 pop_front/remove로 명시한다.
 */
int level_reduce_qty(price_level_t *level, order_t *order, qty_t qty);

/*
 * 원 주문 수량을 줄인다(수량 감소 정정, docs/SPEC.md 4.4).
 * 줄이기만 한다 — 늘리면 시간 우선순위를 잃어야 하므로 떼었다 다시 붙여야 한다.
 * filled_qty는 건드리지 않는다. 체결이 아니라 주문 자체가 작아지는 것이다.
 * new_qty가 기체결 수량 이하이거나 현재 수량 이상이면 ERR_INVALID_QTY.
 */
int level_amend_qty(price_level_t *level, order_t *order, qty_t new_qty);

#endif /* MINI_SOR_PRICE_LEVEL_H */
