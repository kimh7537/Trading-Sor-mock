#ifndef MINI_SOR_EXECUTOR_H
#define MINI_SOR_EXECUTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "match.h"
#include "order_map.h"
#include "strategy.h"
#include "types.h"

/*
 * 집행기 — 계획(T2-05)을 실제 주문으로 각 시장에 보내고, 돌아온 결과를 논리 주문의
 * 상태로 합산한다(T2-09).
 *
 * ---
 *
 * **한쪽이 거부돼도 되돌리지 않는다.**
 *
 * 두 시장에 보낸 주문은 서로 다른 거래소의 서로 다른 트랜잭션이다. KRX에서 40주가
 * 체결된 뒤 NXT가 거부했다고 해서 그 40주를 없던 일로 만들 방법이 없다 — 되돌리려면
 * 반대매매를 내야 하고, 그것은 취소가 아니라 **새로운 손실 있는 거래**다.
 *
 * 그래서 집행은 전부-아니면-전무가 아니다. 다리마다 독립적으로 성공하거나 실패하고,
 * 논리 주문의 상태는 그 결과의 **합**이다. 한 다리도 보내지 못했을 때만 논리 주문이
 * 거부된 것으로 본다.
 *
 * 이것이 T1-10의 FOK(전량 아니면 전무)와 다른 이유는 알갱이가 다르기 때문이다.
 * FOK는 한 거래소 안에서 호가창을 건드리기 전에 미리 세어 볼 수 있다. 두 거래소
 * 사이에는 그런 지점이 없다.
 *
 * ---
 *
 * 거부된 다리의 수량은 **취소된 것으로 기록한다.** 그 수량은 영원히 체결되지 않으므로
 * 살아 있는 잔량에서 빼야 한다. 그러지 않으면 "아직 체결을 기다리는 수량"이 실제보다
 * 많게 보인다.
 */

/* 시장별 매칭 엔진 묶음. 집행기는 소유하지 않는다 — 포인터만 빌린다. */
typedef struct {
    match_engine_t *eng[MARKET_COUNT];
} venues_t;

/* 다리 하나를 보낸 결과. */
typedef struct {
    order_id_t phys_id;
    market_t   market;
    qty_t      sent_qty;
    int        rc; /* 거래소가 돌려준 코드. ERR_OK가 아니면 거부다 */
    qty_t      filled_qty;
    int64_t    notional;
    qty_t      remaining_qty; /* 보낸 수량 중 체결되지 않은 것 */
    bool       resting;       /* 잔량이 그 시장 호가창에 등록됐는가 */
} leg_result_t;

/*
 * 논리 주문 하나를 집행한 결과.
 *
 * 불변조건: `filled_qty`는 모든 다리의 체결 합이고,
 *           `unfilled_qty == order_qty - filled_qty`다.
 */
typedef struct {
    leg_result_t legs[PLAN_LEGS_MAX];
    int32_t      leg_count;
    int32_t      rejected_count; /* 거부된 다리 수 */

    qty_t   order_qty;
    qty_t   filled_qty;   /* 모든 다리의 체결 합 */
    int64_t notional;     /* 모든 다리의 체결 금액 합 */
    qty_t   unfilled_qty; /* order_qty - filled_qty */
    qty_t   working_qty;  /* 아직 시장에 살아 있는 수량 */

    order_status_t status;
} exec_report_t;

/*
 * 계획대로 각 시장에 주문을 보낸다.
 *
 * 매핑에 논리 주문을 먼저 등록해 물리 주문번호를 받고(T2-09), 다리마다 그 번호로
 * 주문을 보낸다. 다리의 주문 유형은 계획이 정한 것을 그대로 쓴다.
 *
 * **한 다리라도 접수되면 ERR_OK다.** 거부된 다리는 out->legs[i].rc에 그 이유가 남고
 * out->rejected_count가 올라간다. 모든 다리가 거부되면 첫 거부 이유를 그대로 돌려준다.
 *
 * 매핑 등록 자체가 실패하면(계획이 불변조건을 어겼거나 번호가 겹치거나 자리가 없으면)
 * 아무 시장에도 보내지 않고 그 에러를 돌려준다.
 */
int exec_submit(order_map_t *map, venues_t *venues, const order_t *req,
                const exec_plan_t *plan, exec_report_t *out);

/*
 * 매핑에 쌓인 상태만 보고 논리 주문의 현재 상태를 다시 계산한다.
 *
 * 집행 직후가 아니라 **나중에 온 체결 통보까지 반영된** 상태를 알아야 할 때 쓴다.
 * 없는 논리 주문이면 ERR_NOT_FOUND.
 */
int exec_status(const order_map_t *map, order_id_t logical_id,
                order_status_t *out_status);

/* 상태 이름. 로그와 테스트 메시지용. NULL을 반환하지 않는다. */
const char *exec_status_name(order_status_t status);

#endif /* MINI_SOR_EXECUTOR_H */
