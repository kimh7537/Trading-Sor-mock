#ifndef MINI_SOR_ORDER_H
#define MINI_SOR_ORDER_H

#include <stdint.h>

#include "types.h"

/*
 * 주문과 주문 풀.
 *
 * 매칭 엔진은 주문마다 malloc을 하지 않는다. 풀을 미리 잡아 두고 슬롯을 빌려 쓴다.
 * 할당 지연이 측정에 섞이면 전략 간 체결 단가 비교가 흔들리기 때문이다.
 */

typedef struct order {
    order_id_t   id;              /* 거래소가 부여한 주문번호. 0이면 미배정 */
    uint64_t     client_order_id; /* 증권사(채널계)가 부여한 주문번호 */
    ts_t         ts;              /* 논리 시각. 시간 우선 원칙의 기준 */
    price_t      price;
    qty_t        qty;             /* 원 주문 수량 */
    qty_t        filled_qty;      /* 체결된 수량. 잔량은 qty - filled_qty */
    side_t       side;
    order_type_t type;
    market_t     market;

    /* 가격 레벨 FIFO 링크. 어느 리스트에도 속하지 않으면 둘 다 NULL이다.
     * 주문 안에 링크를 두면 임의 위치 제거가 역참조 없이 O(1)이 된다.
     * 대신 한 주문은 한 번에 한 리스트에만 들어간다. */
    struct order *prev;
    struct order *next;
} order_t;

/* 미체결 잔량. 호가창이 다루는 수량은 언제나 이 값이다. */
static inline qty_t order_remaining_qty(const order_t *order)
{
    return order->qty - order->filled_qty;
}

/* 내부 구조는 order_pool.c에만 있다. 호출부는 슬롯 배치를 알 필요가 없다. */
typedef struct order_pool order_pool_t;

/* 용량만큼 슬롯을 미리 잡는다. capacity <= 0이거나 할당 실패면 NULL. */
order_pool_t *order_pool_create(int32_t capacity);
void order_pool_destroy(order_pool_t *pool);

/*
 * 슬롯 하나를 빌린다. 풀이 고갈되면 NULL을 반환한다(크래시하지 않는다).
 * 반환된 주문의 필드는 항상 초기 상태다 — 이전 사용 흔적이 남지 않는다.
 * 소유권은 이동하지 않는다. 슬롯은 풀의 것이고 호출부는 빌려 쓸 뿐이다.
 */
order_t *order_pool_acquire(order_pool_t *pool);

/*
 * 슬롯을 돌려준다. 이중 해제는 내부 버그이므로 assert로 잡는다.
 * NDEBUG 빌드에서는 assert가 사라지므로, 프리리스트가 순환하지 않도록
 * 무시하고 반환한다 — 조용한 자료구조 손상이 크래시보다 나쁘다.
 */
void order_pool_release(order_pool_t *pool, order_t *order);

#endif /* MINI_SOR_ORDER_H */
