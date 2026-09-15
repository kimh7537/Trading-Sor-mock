#ifndef MINI_SOR_ORDER_BOOK_H
#define MINI_SOR_ORDER_BOOK_H

#include <stdint.h>

#include "price_level.h"
#include "types.h"

/*
 * 한 종목 한 시장의 호가창 (docs/SPEC.md 3.1, 4.1).
 *
 * 기준가(전일 종가)의 ±30%가 다룰 수 있는 가격의 전부이므로, 그 구간의 유효 호가를
 * 배열로 펼쳐 놓고 가격을 인덱스로 접는다. 유효 호가만 칸을 차지한다 — 원 단위로
 * 펼치면 호가 단위가 100원인 구간에서 칸 100개 중 1개만 쓰게 된다.
 */

/* 최우선호가가 없을 때의 센티넬. 0은 어떤 가격대에서도 유효한 호가가 아니다. */
#define BOOK_PRICE_NONE ((price_t)0)

/* N단 호가 조회 결과 한 줄. */
typedef struct {
    price_t price;
    qty_t   total_qty;
    int32_t order_count;
} level_view_t;

typedef struct order_book order_book_t;

/*
 * 기준가로 호가창을 만든다. 다룰 가격 범위는 기준가 ±30%를 호가 단위로 정렬한 구간이다.
 * base_price가 [PRICE_MIN, PRICE_MAX] 밖이거나 할당에 실패하면 NULL.
 */
order_book_t *book_create(price_t base_price);
void book_destroy(order_book_t *book);

/* 이 호가창이 다루는 가격 구간. 제한폭을 호가 단위로 정렬한 값이다. */
price_t book_price_low(const order_book_t *book);
price_t book_price_high(const order_book_t *book);

/*
 * 주문을 제 가격 레벨 뒤에 붙인다. 주문의 side/price를 그대로 쓴다.
 * 제한폭 밖이면 ERR_PRICE_LIMIT, 호가 단위에 안 맞으면 ERR_INVALID_TICK,
 * 잔량이 없으면 ERR_INVALID_QTY.
 */
int book_insert(order_book_t *book, order_t *order);

/* 주문을 뗀다. 이 호가창에 없는 주문이면 내부 버그이므로 assert. */
int book_remove(order_book_t *book, order_t *order);

/* 최우선호가. 비어 있으면 BOOK_PRICE_NONE. 캐시된 값이라 O(1). */
price_t book_best_bid(const order_book_t *book);
price_t book_best_ask(const order_book_t *book);

/* 특정 가격의 잔량 합계. 범위 밖이거나 비어 있으면 0. */
qty_t book_qty_at(const order_book_t *book, side_t side, price_t price);

/*
 * 최우선호가부터 depth단까지 채운다. 매수는 높은 가격부터, 매도는 낮은 가격부터.
 * 빈 레벨은 건너뛴다. 채운 줄 수를 반환하고, 인자가 잘못되면 음수 에러 코드.
 */
int book_snapshot(const order_book_t *book, side_t side, int depth,
                  level_view_t *out);

#endif /* MINI_SOR_ORDER_BOOK_H */
