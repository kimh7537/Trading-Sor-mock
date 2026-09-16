#ifndef MINI_SOR_ORDER_SDK_H
#define MINI_SOR_ORDER_SDK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "msg.h"
#include "types.h"

/*
 * 전략 엔진용 주문 SDK.
 *
 * T5-06이 시세를 **읽게** 했다면 이것은 주문을 **내게** 한다.
 * 바깥에서 붙는 전략 엔진이 링크하는 쪽이다.
 *
 * ===========================================================================
 * `core`만 의존한다
 * ===========================================================================
 *
 * `exchange`·`sor`·`ledger`를 링크하면 전략 엔진이 시뮬레이터 내부를 통째로
 * 끌고 가야 한다. 붙이는 쪽이 알아야 할 것은 **전문 형식뿐이다.**
 *
 * 그래서 `core/`가 아니라 최상위 `sdk/`에 둔다. `core`는 시뮬레이터의 공통
 * 부품이고 이것은 **바깥으로 나가는 물건**이라, 같은 서랍에 두면 언젠가
 * 시뮬레이터 내부를 하나만 끌어다 쓰게 된다.
 *
 * ===========================================================================
 * 주문번호는 SDK가 발급한다
 * ===========================================================================
 *
 * 전략이 직접 매기면 겹치거나 빠진다. 그리고 그 사실을 **응답이 오고 나서야
 * 안다** — 겹친 번호로 온 체결은 어느 주문 것인지 정할 수 없다.
 *
 * 발급을 한 군데로 모으면 겹칠 수가 없다. 전략은 번호를 받아 들고만 있으면
 * 되고, 그 번호가 응답·체결과 이어진다.
 *
 * ===========================================================================
 * 보낸 것을 기억한다
 * ===========================================================================
 *
 * 응답은 주문번호로 온다. 전략이 알고 싶은 것은 "내가 낸 그 주문"이다.
 * 그 사이를 SDK가 잇는다 — 종목, 방향, 낸 수량, 지금까지 체결된 수량,
 * 평균 단가.
 *
 * **떠 있는 주문 수를 셀 수 있어야 한다.** 몇 건이 응답을 기다리는지 모르면
 * 전략이 자기 위험을 계산할 수 없다. 그 수는 숨기지 않는다.
 *
 * ===========================================================================
 * 모르는 번호를 조용히 버리지 않는다
 * ===========================================================================
 *
 * 모르는 주문번호로 응답이 오면 SDK가 틀렸거나 상대가 틀린 것이다. 둘 다
 * 알아야 할 일이다. 조용히 버리면 **체결이 사라진 것을 아무도 모른다.**
 *
 * 자리가 차도 마찬가지다. 새 주문을 거절하지 **덮어쓰지 않는다** — 덮어쓰면
 * 그 주문의 체결이 갈 곳을 잃는다.
 */

/* 동시에 담을 수 있는 주문 수의 상한. */
#define SDK_ORDERS_MAX 1024

/* 전문 바디 하나를 담을 버퍼 크기. 헤더는 부르는 쪽이 쓴다(T3-02의 가름). */
#define SDK_BODY_MAX 64

/* 한 논리 주문의 상태. */
typedef enum {
    SDK_ORD_NONE = 0, /* 빈 자리 */
    SDK_ORD_PENDING,  /* 보냈고 응답을 기다린다 */
    SDK_ORD_LIVE,     /* 받아들여졌고 잔량이 있다 */
    SDK_ORD_DONE      /* 끝났다 — 전량 체결, 취소, 또는 거부 */
} sdk_ord_state_t;

const char *sdk_state_str(sdk_ord_state_t s);

/* 전략이 보는 주문 하나. */
typedef struct {
    uint64_t        cl_ord_id; /* SDK가 발급한 번호 */
    order_id_t      order_id;  /* 상대가 붙인 번호. 응답 전에는 0 */
    sdk_ord_state_t state;

    char    symbol[MSG_SYMBOL_LEN + 1];
    side_t  side;
    price_t price;
    qty_t   qty; /* 낸 수량 */

    qty_t   filled_qty;
    int64_t notional; /* 체결 금액 합. 평균 단가 = notional / filled_qty */
    qty_t   canceled_qty;

    int reject_reason; /* 거부됐으면 그 이유, 아니면 ERR_OK */

    ts_t sent_ts; /* 호출부가 준 논리 시각 */
} sdk_order_t;

typedef struct sdk sdk_t;

/*
 * SDK를 만든다. 계좌번호와 **첫 주문번호**를 받는다.
 *
 * 첫 번호를 받는 이유: 재기동해도 앞서 쓴 번호를 다시 쓰면 안 된다.
 * 어디서부터 이어야 하는지는 **부르는 쪽이 안다**(저널이나 원장이 알려
 * 준다). SDK가 1부터 시작하기로 정해 버리면 재기동이 곧 번호 충돌이다.
 *
 * 인자가 잘못되면 NULL.
 */
sdk_t *sdk_create(const char *account, uint64_t first_cl_ord_id);
void   sdk_destroy(sdk_t *s);

/* --- 내보내기 --- */

/*
 * 주문을 만든다. 전문 바디를 `buf`에 쓰고 발급한 번호를 `out_cl_ord_id`에
 * 담는다. 성공하면 **쓴 바이트 수**(전문 규격과 같다).
 *
 * 자리가 없으면 ERR_POOL_EXHAUSTED — **덮어쓰지 않는다.**
 * 인자가 잘못되면 ERR_INVALID_ARG.
 *
 * 이 함수는 보내지 않는다. 보내는 것은 부르는 쪽의 일이다 — 소켓을 SDK가
 * 쥐면 전략이 자기 이벤트 루프를 못 쓴다.
 */
int sdk_new_order(sdk_t *s, const char *symbol, side_t side,
                  order_type_t type, market_t market, price_t price, qty_t qty,
                  ts_t now, uint8_t *buf, size_t cap,
                  uint64_t *out_cl_ord_id);

/*
 * 취소를 만든다. `cl_ord_id`는 `sdk_new_order`가 준 번호다.
 *
 * 모르는 번호면 ERR_NOT_FOUND. 이미 끝난 주문이면 ERR_NOT_SUPPORTED —
 * **끝난 주문을 취소하려는 것은 전략이 상태를 잘못 알고 있다는 뜻**이라
 * 알려 줘야 한다.
 */
int sdk_cancel(sdk_t *s, uint64_t cl_ord_id, uint8_t *buf, size_t cap);

/*
 * 정정을 만든다. 규칙은 취소와 같다.
 *
 * **SDK는 정정된 값을 아직 반영하지 않는다.** 상대가 받아 줬는지 모르기
 * 때문이다. 반영은 응답(`sdk_on_modify_ack`)이 온 뒤에 한다.
 */
int sdk_modify(sdk_t *s, uint64_t cl_ord_id, price_t new_price, qty_t new_qty,
               uint8_t *buf, size_t cap);

/* --- 받아들이기 --- */

/*
 * 응답과 체결을 반영한다. 성공하면 ERR_OK.
 *
 * 모르는 주문번호면 ERR_NOT_FOUND — **조용히 버리지 않는다.**
 */
int sdk_on_order_ack(sdk_t *s, const msg_order_ack_t *ack);
int sdk_on_cancel_ack(sdk_t *s, const msg_cancel_ack_t *ack);
int sdk_on_modify_ack(sdk_t *s, const msg_modify_ack_t *ack);
int sdk_on_fill(sdk_t *s, const msg_fill_noti_t *fill);

/* --- 보기 --- */

/* 내 번호로 찾는다. 없으면 NULL. */
const sdk_order_t *sdk_get(const sdk_t *s, uint64_t cl_ord_id);

/* 상대의 번호로 찾는다. 없으면 NULL. */
const sdk_order_t *sdk_get_by_order_id(const sdk_t *s, order_id_t order_id);

/*
 * **응답을 기다리는 주문 수.** 전략이 자기 위험을 세는 값이다.
 */
int32_t sdk_pending_count(const sdk_t *s);

/* 아직 잔량이 있는 주문 수. */
int32_t sdk_live_count(const sdk_t *s);

/* 담고 있는 주문 수(끝난 것 포함). */
int32_t sdk_count(const sdk_t *s);

/* 남은 잔량. 없는 주문이면 0. */
qty_t sdk_remaining(const sdk_t *s, uint64_t cl_ord_id);

/* 평균 체결 단가. 체결이 없으면 0. */
price_t sdk_avg_price(const sdk_t *s, uint64_t cl_ord_id);

/*
 * 모르는 번호로 온 응답의 누적 건수.
 *
 * 0이 아니면 **SDK나 상대가 틀렸다.** 조용히 넘어가지 않도록 세어 둔다.
 */
uint64_t sdk_orphans(const sdk_t *s);

/*
 * 끝난 주문들을 자리에서 비운다. 비운 수를 반환한다.
 *
 * 자동으로 비우지 않는다 — 전략이 끝난 주문을 언제까지 보고 싶은지는
 * 전략이 정할 일이다. 자리가 차기 전에 부르면 된다.
 */
int32_t sdk_reap_done(sdk_t *s);

#endif /* MINI_SOR_ORDER_SDK_H */
