#ifndef MINI_SOR_ORDER_MAP_H
#define MINI_SOR_ORDER_MAP_H

#include <stdbool.h>
#include <stdint.h>

#include "order.h"
#include "strategy.h"
#include "types.h"

/*
 * 논리 주문 <-> 물리 주문 매핑.
 *
 * 사용자가 낸 주문 하나(논리)가 시장별 주문 여럿(물리)으로 갈라진다. 체결 통보는
 * 물리 주문번호로 오는데 사용자에게 보여 줄 것은 논리 주문의 상태다. 그 사이를 잇는다.
 *
 * ---
 *
 * **물리 주문번호를 해시 테이블이 아니라 산술로 만든다.**
 *
 *   phys = logical x PHYS_ID_SLOTS + market + 1
 *
 * 한 논리 주문은 한 시장에 다리를 하나만 갖는다(T2-05의 규칙, plan_validate가 강제).
 * 그래서 (논리 주문번호, 시장) 쌍이 물리 주문을 유일하게 가리키고, 번호를 그 쌍의
 * 자리값 인코딩으로 만들 수 있다.
 *
 * 얻는 것이 셋이다.
 *  - 양방향 조회가 나눗셈/나머지 한 번이다. 해시 테이블도, 충돌도, 재해시도 없다
 *  - 물리 주문번호만 보고 **어느 시장 주문인지 바로 안다.** 통합 로그에서 시장을
 *    구분해야 한다는 완료 조건이 자료구조 없이 성립한다
 *  - 시장별로 번호가 겹치지 않는다 — 나머지가 다르므로 구조적으로 불가능하다
 *
 * 대가는 논리 주문번호의 상한이 UINT64_MAX / PHYS_ID_SLOTS로 줄어드는 것인데,
 * 그래도 10^18 규모라 실질적인 제약이 아니다.
 */

/* 시장 수보다 넉넉한 2의 거듭제곱. 시장이 늘어도 번호 체계를 안 바꾸려고 16으로 둔다. */
#define PHYS_ID_SLOTS 16

/* 논리 주문번호의 상한. 이보다 크면 물리 번호가 넘친다. */
#define LOGICAL_ID_MAX (UINT64_MAX / PHYS_ID_SLOTS)

/* (논리 주문번호, 시장) -> 물리 주문번호. 인자가 잘못되면 ORDER_ID_INVALID. */
order_id_t phys_id_make(order_id_t logical_id, market_t market);

/* 물리 주문번호 -> 논리 주문번호. 잘못된 번호면 ORDER_ID_INVALID. */
order_id_t phys_id_logical(order_id_t phys_id);

/* 물리 주문번호 -> 시장. 잘못된 번호면 false. */
bool phys_id_market(order_id_t phys_id, market_t *out_market);

/* 물리 주문 하나의 상태. */
typedef struct {
    order_id_t phys_id;
    market_t   market;
    qty_t      sent_qty;   /* 이 시장에 보낸 수량 */
    qty_t      filled_qty; /* 체결된 수량 */
    int64_t    notional;   /* 체결 금액 */
    qty_t      canceled_qty;
    bool       live; /* 아직 호가창에 남아 있는가 */
} phys_leg_t;

/*
 * 논리 주문 하나의 상태. 물리 다리들의 합이다.
 *
 * 불변조건: sum(legs[].sent_qty) == order_qty.
 * 계획의 불변조건(T2-05)을 그대로 이어받는다.
 */
typedef struct {
    order_id_t logical_id;
    side_t     side;
    price_t    limit_price;
    qty_t      order_qty;

    phys_leg_t legs[PLAN_LEGS_MAX];
    int32_t    leg_count;
} logical_order_t;

typedef struct order_map order_map_t;

/*
 * 동시에 담을 논리 주문 수의 상한을 받는다.
 * capacity <= 0이거나 할당 실패면 NULL.
 */
order_map_t *omap_create(int32_t capacity);
void omap_destroy(order_map_t *map);

/*
 * 계획을 물리 주문들로 펼쳐 등록한다.
 *
 * req의 주문번호가 논리 주문번호가 된다. 계획의 다리마다 물리 주문번호를 발급하고,
 * 그 번호를 out_phys[]에 계획의 다리 순서 그대로 채운다(out_phys는 NULL이어도 된다).
 *
 * 계획이 불변조건을 어기면(plan_validate 실패) 그 에러를 그대로 돌려준다.
 * 같은 논리 주문번호가 이미 있으면 ERR_DUPLICATE, 자리가 없으면 ERR_POOL_EXHAUSTED.
 */
int omap_register(order_map_t *map, const order_t *req, const exec_plan_t *plan,
                  order_id_t *out_phys);

/* 논리 주문 조회. 없으면 NULL. */
const logical_order_t *omap_get(const order_map_t *map, order_id_t logical_id);

/* 물리 주문번호로 논리 주문을 찾는다. 없으면 NULL. */
const logical_order_t *omap_get_by_phys(const order_map_t *map,
                                        order_id_t phys_id);

/* 물리 다리 하나를 찾는다. 없으면 NULL. */
const phys_leg_t *omap_leg(const order_map_t *map, order_id_t phys_id);

/*
 * 체결을 반영한다. 물리 주문번호로 온 체결 통보를 그 다리에 더하고,
 * 논리 주문의 합계가 따라 올라간다.
 *
 * 보낸 수량보다 많이 체결될 수 없다 — 넘으면 ERR_INVALID_QTY로 거절하고
 * 아무것도 바꾸지 않는다. 없는 물리 주문이면 ERR_NOT_FOUND.
 */
int omap_on_fill(order_map_t *map, order_id_t phys_id, qty_t qty, price_t price);

/*
 * 취소를 반영한다. 남은 잔량만큼만 취소된다.
 * 잔량보다 많이 취소할 수 없다 — 넘으면 ERR_INVALID_QTY.
 */
int omap_on_cancel(order_map_t *map, order_id_t phys_id, qty_t qty);

/* 논리 주문의 합계. 없는 주문이면 0. */
qty_t   omap_filled_qty(const order_map_t *map, order_id_t logical_id);
int64_t omap_notional(const order_map_t *map, order_id_t logical_id);
qty_t   omap_canceled_qty(const order_map_t *map, order_id_t logical_id);

/* 아직 체결도 취소도 안 된 수량. */
qty_t omap_remaining(const order_map_t *map, order_id_t logical_id);

/* 담긴 논리 주문 수. */
int32_t omap_count(const order_map_t *map);

#endif /* MINI_SOR_ORDER_MAP_H */
