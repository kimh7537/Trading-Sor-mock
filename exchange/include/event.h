#ifndef MINI_SOR_EVENT_H
#define MINI_SOR_EVENT_H

#include "types.h"

/*
 * 주문 생애주기 이벤트.
 *
 * 매칭 엔진은 결과를 반환값으로도 준다(exec_result_t). 하지만 그것은 "이번 호출을 낸
 * 쪽"의 시점이다. 호가창에 있던 상대 주문의 주인도 자기 주문이 체결된 것을 알아야
 * 한다. 이벤트는 그 양쪽을 모두 다룬다.
 *
 * **순서가 결정적이어야 한다.** 같은 입력 시퀀스는 같은 이벤트 시퀀스를 만든다.
 * 이것이 T1-19 결정성 검증의 대상이고 전략 비교의 전제다. 한 호출 안의 순서는
 * 아래로 고정한다.
 *
 *   1. 접수 거부 시   : REJECTED 하나로 끝
 *   2. 체결마다       : 상대(maker) 이벤트 -> 들어온 쪽(taker) 이벤트
 *                       각 이벤트는 그 주문이 전량 체결됐으면 EXECUTED,
 *                       잔량이 남았으면 PARTIALLY_EXECUTED
 *   3. 잔량이 등록되면: ACCEPTED
 *   4. 잔량이 취소되면: CANCELED (시장가·IOC의 미체결분)
 */

typedef enum {
    EVENT_ACCEPTED = 0,       /* 잔량이 호가창에 등록됐다 */
    EVENT_EXECUTED,           /* 이 주문이 전량 체결됐다 */
    EVENT_PARTIALLY_EXECUTED, /* 일부 체결되고 잔량이 남았다 */
    EVENT_CANCELED,
    EVENT_REJECTED,
    EVENT_MODIFIED
} event_type_t;

typedef struct {
    event_type_t type;
    ts_t         ts;       /* 논리 시각. 이 일을 일으킨 주문이 들고 온 값 */
    order_id_t   order_id; /* 이 이벤트의 주인 */
    market_t     market;

    /* 체결이면 체결 가격·수량, 접수·정정이면 주문 가격과 그 시점의 수량. */
    price_t price;
    qty_t   qty;
    qty_t   remaining_qty; /* 이 이벤트 직후의 잔량 */

    order_id_t counterparty_id; /* 체결 상대. 체결이 아니면 ORDER_ID_INVALID */
    int        reason;          /* REJECTED일 때 에러 코드. 그 외에는 ERR_OK */
} order_event_t;

/*
 * 이벤트 소비자. ctx는 소비자가 쓰는 값이고 엔진은 손대지 않는다.
 * 콜백 안에서 엔진을 다시 호출하지 않는다 — 재진입은 이벤트 순서를 흐트러뜨린다.
 */
typedef void (*event_fn_t)(const order_event_t *ev, void *ctx);

typedef struct {
    event_fn_t fn;
    void      *ctx;
} event_sink_t;

/* 싱크가 없거나(fn == NULL) 인자가 NULL이면 아무 일도 하지 않는다. */
void event_emit(const event_sink_t *sink, const order_event_t *ev);

/* 이벤트 종류 이름. 정의되지 않은 값에도 NULL을 반환하지 않는다. */
const char *event_type_str(event_type_t type);

#endif /* MINI_SOR_EVENT_H */
