#ifndef MINI_SOR_RECON_H
#define MINI_SOR_RECON_H

#include <stdbool.h>
#include <stdint.h>

#include "types.h"

/*
 * 정합성 대사.
 *
 * ===========================================================================
 * 대사는 대상의 코드를 쓰지 않는다
 * ===========================================================================
 *
 * 이것이 이 모듈의 전부다. 대사가 `omap_get()`을 불러 다리 합을 얻으면,
 * `omap`이 합을 잘못 세고 있을 때 **대사도 같이 잘못 세고 "일치"라고 답한다.**
 * 같은 버그를 두 번 쓰는 것은 검사가 아니다.
 *
 * 그래서 이 모듈은 **아무것도 조회하지 않는다.** 원장이 들고 있는 숫자와
 * 체결이 남긴 숫자를 부르는 쪽이 각각 긁어 와서 넘기고, 여기서는 그 둘을
 * 처음부터 다시 계산해 맞춰만 본다.
 *
 * 대가가 있다. 부르는 쪽이 `logical_order_t`나 `account_t`를 이 모듈의
 * 모양으로 **한 번 옮겨 적어야 한다.** 그 옮겨 적는 일이 귀찮은 만큼이
 * 독립성의 값이다. 구조체를 그대로 받으면 편하지만 그 순간 `sor`·`ledger`에
 * 매이고, 매이는 순간 대사가 대상의 세계관을 물려받는다.
 *
 * (부수 효과로 `core`가 `sor`·`ledger` 어느 쪽도 링크하지 않아도 된다.
 *  그 둘은 서로를 못 보는 형제인데, 대사는 둘 다를 봐야 한다.)
 *
 * ===========================================================================
 * 첫 번째 어긋남에서 멈추지 않는다
 * ===========================================================================
 *
 * 하나만 보고하고 돌아오면 나머지는 **다음 대사 때까지 안 보인다.** 대사는
 * 하루 한 번 도는 일이므로 그 사이가 하루다. 찾은 것을 다 담아 돌려준다.
 *
 * 담을 자리가 모자라면 자른 사실을 숨기지 않는다 — "세 건"과 "세 건까지
 * 세다 말았다"는 완전히 다른 보고다.
 */

/* 어긋남의 종류. */
typedef enum {
    /* --- 논리 <-> 물리 --- */
    /* 다리들의 보낸 수량 합이 주문 수량과 다르다 */
    RECON_LEG_SUM,
    /* 한 다리에서 체결+취소가 보낸 수량을 넘었다 */
    RECON_LEG_OVERFILL,
    /* 아직 살아 있다는데 남은 수량이 없다 */
    RECON_LIVE_NO_REMAIN,
    /* 체결이 0인데 체결 금액이 있다 (또는 그 반대) */
    RECON_NOTIONAL,
    /* 물리 주문번호가 겹친다 */
    RECON_DUP_PHYS,

    /* --- 잔고 <-> 체결 <-> 원장 --- */
    /* 예수금이 처음 값 + 입출금 + 체결로 설명되지 않는다 */
    RECON_CASH,
    /* 묶인 금액이 미체결 주문들의 몫과 다르다 */
    RECON_RESERVED,
    /* 묶인 금액이 예수금을 넘었다 */
    RECON_RESERVED_OVER_CASH,

    RECON_KIND_COUNT
} recon_kind_t;

/* 사람이 읽을 이름. 범위를 벗어나면 "?". */
const char *recon_kind_str(recon_kind_t k);

/* 계좌번호를 담는 자리. 원장의 계좌번호보다 넉넉하게 잡는다. */
#define RECON_ACCT_LEN 32

/*
 * 어긋남 한 건.
 *
 * **"안 맞는다"만으로는 아무도 고치지 못한다.** 어디의 무엇이 얼마만큼
 * 틀렸는지를 같이 담는다.
 */
typedef struct {
    recon_kind_t kind;
    order_id_t   logical_id; /* 논리 쪽 어긋남이면 주문번호, 아니면 0 */
    order_id_t   phys_id;    /* 다리 단위 어긋남이면 물리 번호, 아니면 0 */
    char         account_no[RECON_ACCT_LEN]; /* 계좌 쪽이 아니면 "" */
    int64_t      expected;
    int64_t      actual;
} recon_finding_t;

/* 대사 결과를 담는 자리. 부르는 쪽이 배열을 준다. */
typedef struct {
    recon_finding_t *at;
    int32_t          cap;
    int32_t          count;
    /*
     * 자리가 모자라 못 담은 건수.
     *
     * **자른 사실을 숨기지 않는다.** "세 건"과 "세 건까지 세다 말았다"는
     * 완전히 다른 보고다.
     */
    int32_t dropped;
} recon_report_t;

/* 보고서를 비운다. at/cap을 주고 count/dropped를 0으로 만든다. */
void recon_report_init(recon_report_t *r, recon_finding_t *buf, int32_t cap);

/* 어긋남이 하나도 없으면 true. */
bool recon_clean(const recon_report_t *r);

/* --- 논리 <-> 물리 --- */

/* 물리 다리 하나의 숫자. `sor`의 `phys_leg_t`에서 옮겨 적는다. */
typedef struct {
    order_id_t phys_id;
    qty_t      sent_qty;
    qty_t      filled_qty;
    qty_t      canceled_qty;
    int64_t    notional;
    bool       live;
} recon_leg_t;

/* 한 논리 주문에 붙는 다리 수의 상한. `sor`의 PLAN_LEGS_MAX와 같은 값이다. */
#define RECON_LEGS_MAX 8

/* 논리 주문 하나의 숫자. */
typedef struct {
    order_id_t  logical_id;
    qty_t       order_qty;
    recon_leg_t legs[RECON_LEGS_MAX];
    int32_t     leg_count;
} recon_order_t;

/*
 * 논리 주문 하나를 대사한다. 찾은 어긋남을 `r`에 담는다.
 *
 * 인자가 잘못되면(NULL, leg_count가 범위 밖) ERR_*. 어긋남을 찾은 것은
 * **오류가 아니다** — 그것이 이 함수의 일이므로 ERR_OK를 돌려준다.
 * 어긋남 여부는 `recon_clean(r)`로 본다.
 */
int recon_order(const recon_order_t *o, recon_report_t *r);

/* --- 잔고 <-> 체결 <-> 원장 --- */

/*
 * 계좌 하나의 숫자.
 *
 * `cash_now`/`reserved_now`는 **원장이 들고 있는 값**이고, 나머지는
 * **체결과 입출금이 남긴 값**이다. 둘은 서로 다른 곳에서 와야 한다.
 * 같은 곳에서 오면 대사가 자기 자신과 맞춰 보는 셈이다.
 */
typedef struct {
    char account_no[RECON_ACCT_LEN];

    int64_t cash_start;    /* 대사 구간 시작 시점의 예수금 */
    int64_t deposits;      /* 구간 중 입금 합 */
    int64_t withdrawals;   /* 구간 중 출금 합 */
    int64_t buy_notional;  /* 구간 중 매수 체결 금액 합 (나간 돈) */
    int64_t sell_notional; /* 구간 중 매도 체결 금액 합 (들어온 돈) */

    int64_t cash_now;     /* 원장이 들고 있는 예수금 */
    int64_t reserved_now; /* 원장이 들고 있는 묶인 금액 */

    int64_t open_reserved; /* 미체결 주문들이 묶고 있어야 할 금액 합 */
} recon_account_t;

/* 계좌 하나를 대사한다. 규칙은 `recon_order`와 같다. */
int recon_account(const recon_account_t *a, recon_report_t *r);

#endif /* MINI_SOR_RECON_H */
