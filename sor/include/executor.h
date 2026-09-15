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

/* --- 취소 --- */

/*
 * 논리 주문 취소 — 살아 있는 물리 주문을 전부 취소한다.
 *
 * **한쪽 취소가 실패하면 성공한 취소를 되돌리지 않는다.** 이유가 셋이다.
 *
 *  1. 취소를 되돌리는 것은 곧 **주문을 다시 내는 것**이다. 큐의 원래 자리는 이미
 *     비었고 다시 붙으면 맨 뒤다. 시간 우선순위를 복원할 방법이 없다
 *  2. 되돌리는 사이에 체결될 수 있다. 사용자는 취소됐다고 믿는데 체결이 나는 것이
 *     이 상황에서 가장 나쁜 결과다
 *  3. **실패한 취소는 재시도할 수 있다.** 되돌리기는 되돌릴 수 없지만 재시도는
 *     안전하다. 그래서 보상하지 않고, 어느 다리가 남았는지 보고서에 정확히 남긴다
 *
 * 보상 처리의 범위는 여기까지다 — 집행기는 되돌리지 않고, 재시도는 호출자가 한다.
 */
typedef struct {
    order_id_t phys_id;
    market_t   market;
    bool       was_live;     /* 취소를 시도했는가 (이미 끝난 다리는 건너뛴다) */
    int        rc;           /* 시도했을 때 거래소가 돌려준 코드 */
    qty_t      canceled_qty; /* 이번 취소로 사라진 수량 */
} cancel_leg_t;

typedef struct {
    cancel_leg_t legs[PLAN_LEGS_MAX];
    int32_t      leg_count;
    int32_t      attempted; /* 살아 있어서 취소를 시도한 다리 수 */
    int32_t      failed;    /* 그중 실패한 다리 수 */

    qty_t          canceled_qty; /* 이번 취소로 사라진 총 수량 */
    qty_t          working_qty;  /* 취소 뒤에도 시장에 남아 있는 수량 */
    order_status_t status;
} cancel_report_t;

/*
 * 살아 있는 모든 물리 주문을 취소한다.
 *
 * 전부 성공하면 ERR_OK. 일부가 실패하면 **성공한 것은 그대로 두고** 첫 실패 코드를
 * 돌려준다 — 실패한 다리는 out->legs[i].rc에 이유가 남고 out->failed가 올라간다.
 * 취소할 살아 있는 주문이 하나도 없으면 ERR_NOT_FOUND(매칭 엔진의 취소와 같은 뜻).
 * 없는 논리 주문도 ERR_NOT_FOUND.
 */
int exec_cancel(order_map_t *map, venues_t *venues, order_id_t logical_id,
                ts_t ts, cancel_report_t *out);

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
