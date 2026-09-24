#ifndef MINI_SOR_ORDER_VALIDATE_H
#define MINI_SOR_ORDER_VALIDATE_H

#include <stdbool.h>
#include <stdint.h>

#include "account.h"
#include "msg.h"
#include "tick_size.h"

/*
 * 주문 검증 — 거래소로 보내기 전에 원장이 막을 것을 막는다.
 *
 * ===========================================================================
 * 검사 순서 (완료 조건이 요구한 문서화)
 * ===========================================================================
 *
 *   1. 계좌가 있는가
 *   2. 종목이 거래 가능한가
 *   3. 주문 유형을 받을 수 있는가
 *   4. 수량이 범위 안인가
 *   5. 가격이 범위 안이고 호가 단위에 맞는가
 *   6. 주문 금액이 한도 안인가
 *   7. 증거금을 묶을 수 있는가          <- 여기서만 계좌 락을 잡는다
 *
 * **1~6은 락 없이 계산만으로 끝난다.** 계좌 락은 워커끼리 부딪히는 유일한
 * 자원이고, 형식이 틀린 주문이 그 락을 잡고 있으면 멀쩡한 주문이 기다린다.
 *
 * 값싼 검사를 앞에 두는 것이 성능 때문만은 아니다 — **거부 사유를 정확히
 * 말하기 위해서**이기도 하다. 증거금부터 보면 "호가 단위가 틀린 주문"이
 * "증거금 부족"으로 거부되어, 주문을 낸 쪽이 엉뚱한 곳을 고치게 된다.
 *
 * ===========================================================================
 * 증거금은 확인과 묶기가 한 번에 일어난다
 * ===========================================================================
 *
 * "쓸 수 있는 돈이 충분한가"를 본 뒤에 따로 묶으면, 그 사이에 다른 워커의 주문이
 * 끼어들어 **둘 다 통과한다.** 계좌 하나에 주문이 동시에 오는 일은 드물지만
 * 드물다는 것이 안 생긴다는 뜻은 아니다.
 *
 * 그래서 확인하지 않는다. **묶어 보고, 실패하면 그것이 곧 증거금 부족이다.**
 * `acct_reserve()`가 계좌 락 안에서 잔고를 보고 묶으므로 그 자체로 원자적이다.
 *
 * ===========================================================================
 * 가격을 알 수 없는 주문 (완료 조건이 요구한 문서화)
 * ===========================================================================
 *
 * **시장가와 중간가는 받지 않는다.**
 *
 * 증거금은 `가격 x 수량 x 증거금률`인데 두 유형은 **체결되어 봐야 가격을 안다.**
 * 원장이 증거금을 계산할 수 없으므로 묶을 금액도 정할 수 없다.
 *
 * 실제 증권사는 상한가나 직전가로 어림잡아 묶고 체결 뒤 정산한다. 그러려면
 * 종목별 참조 가격을 원장이 들고 있어야 하는데, 그 값을 어디서 받아 언제
 * 갱신할지가 또 하나의 주제다. **여기서는 받지 않는 쪽을 골랐고 그 한계를
 * 적어 둔다** — 지정가만으로도 이 계층이 맡은 일을 보이는 데 모자라지 않다.
 */

/* 등록 가능한 종목 수. */
#define VALIDATE_SYMBOLS_MAX 64

/* 증거금률의 분모. 10000 = 100%. */
#define MARGIN_BP_FULL 10000

typedef struct {
    char symbol[MSG_SYMBOL_LEN + 1];

    /*
     * 증거금률(bp). 10000이면 주문 금액 전액을 묶는다.
     * 국내 주식은 종목별로 20~100%가 매겨진다.
     */
    int32_t margin_bp;

    bool tradable; /* 거래정지 종목은 false */
} symbol_rule_t;

typedef struct {
    symbol_rule_t symbols[VALIDATE_SYMBOLS_MAX];
    int32_t       symbol_count;

    /* 주문 한 건의 금액 한도(원). 0 이하면 한도를 보지 않는다. */
    int64_t max_order_notional;

    /*
     * 호가 단위 표(T10-01). 이 원장이 다루는 종목이 국내인가 미국인가.
     * 검증이 호가창과 **같은 표**를 봐야 한다 — 다르면 엔진은 받아들이는 가격을
     * 검증이 막거나, 그 반대가 된다.
     */
    tick_table_t tick_table;
} validate_config_t;

/* 검증 결과. 거부돼도 왜 거부됐는지 남는다. */
typedef struct {
    int reason; /* ERR_OK면 통과 */

    int32_t account_index; /* 찾았으면 자리 번호, 못 찾았으면 -1 */
    int64_t notional;      /* 가격 x 수량 */
    int64_t margin;        /* 실제로 묶은(묶으려 한) 금액 */
} validate_result_t;

/* 설정을 비운다. */
void vcfg_init(validate_config_t *cfg);

/*
 * 종목을 등록한다. 이미 있으면 덮어쓴다.
 * 자리가 없으면 ERR_POOL_EXHAUSTED, 증거금률이 범위 밖이면 ERR_INVALID_ARG.
 */
int vcfg_add_symbol(validate_config_t *cfg, const char *symbol,
                    int32_t margin_bp, bool tradable);

/* 종목 규칙을 찾는다. 없으면 NULL. */
const symbol_rule_t *vcfg_find(const validate_config_t *cfg,
                               const char *symbol);

/*
 * 주문 금액에 대한 증거금. **올림한다** — 내리면 묶는 돈이 모자란다.
 * 인자가 잘못되면 0.
 */
int64_t margin_for(int64_t notional, int32_t margin_bp);

/*
 * 주문을 검증하고, 통과하면 **증거금을 묶는다.**
 *
 * 거부되면 `out->reason`에 사유가 담기고 **아무것도 묶지 않는다.**
 * 반환값은 `out->reason`과 같다.
 *
 * 사유는 서로 구분된다.
 *   ERR_NOT_FOUND      계좌가 없다
 *   ERR_NOT_SUPPORTED  거래 불가 종목이거나 받지 않는 주문 유형
 *   ERR_INVALID_QTY    수량이 범위 밖
 *   ERR_INVALID_PRICE  가격이 범위 밖
 *   ERR_INVALID_TICK   호가 단위에 맞지 않음
 *   ERR_LIMIT_EXCEEDED 주문 한도 초과
 *   ERR_NO_MARGIN      증거금이 모자람
 */
int validate_order(account_store_t *store, const validate_config_t *cfg,
                   const msg_order_req_t *req, validate_result_t *out);

#endif /* MINI_SOR_ORDER_VALIDATE_H */
