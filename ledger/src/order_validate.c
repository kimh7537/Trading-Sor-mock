#include "order_validate.h"

#include <stddef.h>
#include <string.h>

#include "errors.h"
#include "tick_size.h"
#include "types.h"

void vcfg_init(validate_config_t *cfg)
{
    if (cfg == NULL) {
        return;
    }
    memset(cfg, 0, sizeof(*cfg));
}

int vcfg_add_symbol(validate_config_t *cfg, const char *symbol,
                    int32_t margin_bp, bool tradable)
{
    if (cfg == NULL || symbol == NULL || symbol[0] == '\0') {
        return ERR_NULL_PTR;
    }
    /*
     * 0%는 증거금 없이 무한히 주문할 수 있다는 뜻이고, 100%를 넘는 값은
     * 이 프로젝트가 다루는 현물 주문에 근거가 없다. 둘 다 받지 않는다.
     */
    if (margin_bp <= 0 || margin_bp > MARGIN_BP_FULL) {
        return ERR_INVALID_ARG;
    }

    /* 이미 있으면 덮어쓴다 — 거래정지 해제 같은 변경이 그렇게 들어온다. */
    for (int32_t i = 0; i < cfg->symbol_count; i++) {
        if (strncmp(cfg->symbols[i].symbol, symbol, MSG_SYMBOL_LEN) == 0) {
            cfg->symbols[i].margin_bp = margin_bp;
            cfg->symbols[i].tradable = tradable;
            return ERR_OK;
        }
    }

    if (cfg->symbol_count >= VALIDATE_SYMBOLS_MAX) {
        return ERR_POOL_EXHAUSTED;
    }

    symbol_rule_t *r = &cfg->symbols[cfg->symbol_count];
    memset(r, 0, sizeof(*r));
    strncpy(r->symbol, symbol, MSG_SYMBOL_LEN);
    r->symbol[MSG_SYMBOL_LEN] = '\0';
    r->margin_bp = margin_bp;
    r->tradable = tradable;
    cfg->symbol_count++;

    return ERR_OK;
}

const symbol_rule_t *vcfg_find(const validate_config_t *cfg, const char *symbol)
{
    if (cfg == NULL || symbol == NULL) {
        return NULL;
    }
    for (int32_t i = 0; i < cfg->symbol_count; i++) {
        if (strncmp(cfg->symbols[i].symbol, symbol, MSG_SYMBOL_LEN) == 0) {
            return &cfg->symbols[i];
        }
    }
    return NULL;
}

int64_t margin_for(int64_t notional, int32_t margin_bp)
{
    if (notional <= 0 || margin_bp <= 0 || margin_bp > MARGIN_BP_FULL) {
        return 0;
    }
    /*
     * **올림한다.** 내림하면 증거금률 40%인 1,000,001원 주문에서 1원이 덜 묶인다.
     * 한 건에 1원이지만 그런 어긋남은 계좌 잔고를 맞춰 볼 때 원인을 찾기 어렵다.
     */
    return (notional * (int64_t)margin_bp + (MARGIN_BP_FULL - 1)) /
           MARGIN_BP_FULL;
}

int validate_order(account_store_t *store, const validate_config_t *cfg,
                   const msg_order_req_t *req, validate_result_t *out)
{
    validate_result_t tmp;
    if (out == NULL) {
        out = &tmp; /* 결과를 안 받겠다면 버린다. 검증은 그대로 한다 */
    }

    memset(out, 0, sizeof(*out));
    out->account_index = -1;

    if (store == NULL || cfg == NULL || req == NULL) {
        out->reason = ERR_NULL_PTR;
        return out->reason;
    }

    /* --- 1. 계좌 --- */
    int idx = acct_find(store, req->account);
    if (idx < 0) {
        out->reason = ERR_NOT_FOUND;
        return out->reason;
    }
    out->account_index = idx;

    /* --- 2. 종목 --- */
    const symbol_rule_t *rule = vcfg_find(cfg, req->symbol);
    if (rule == NULL || !rule->tradable) {
        out->reason = ERR_NOT_SUPPORTED;
        return out->reason;
    }

    /* --- 3. 주문 유형 --- */
    if (req->type != ORDER_LIMIT && req->type != ORDER_IOC &&
        req->type != ORDER_FOK) {
        /*
         * 시장가·중간가는 체결되어 봐야 가격을 안다. 증거금을 계산할 수 없으므로
         * 받지 않는다(헤더의 설명 참조).
         */
        out->reason = ERR_NOT_SUPPORTED;
        return out->reason;
    }
    if (req->side != SIDE_BUY && req->side != SIDE_SELL) {
        out->reason = ERR_INVALID_ARG;
        return out->reason;
    }

    /* --- 4. 수량 --- */
    if (req->qty < QTY_MIN || req->qty > QTY_MAX) {
        out->reason = ERR_INVALID_QTY;
        return out->reason;
    }

    /* --- 5. 가격과 호가 단위 --- */
    if (req->price < PRICE_MIN || req->price > PRICE_MAX) {
        out->reason = ERR_INVALID_PRICE;
        return out->reason;
    }
    if (!is_valid_tick(req->price)) {
        out->reason = ERR_INVALID_TICK;
        return out->reason;
    }

    out->notional = (int64_t)req->price * (int64_t)req->qty;

    /* --- 6. 한도 --- */
    if (cfg->max_order_notional > 0 && out->notional > cfg->max_order_notional) {
        out->reason = ERR_LIMIT_EXCEEDED;
        return out->reason;
    }

    /*
     * --- 7. 증거금 ---
     *
     * 매도는 주식을 내놓는 것이라 현금 증거금을 묶지 않는다. 보유 수량 확인은
     * 주식 잔고 원장의 일이고 이 프로젝트의 범위 밖이다 — 그 한계를 여기 적어
     * 둔다. 매수만 묶는다.
     */
    if (req->side == SIDE_SELL) {
        out->margin = 0;
        out->reason = ERR_OK;
        return out->reason;
    }

    out->margin = margin_for(out->notional, rule->margin_bp);
    if (out->margin <= 0) {
        out->reason = ERR_INVALID_ARG;
        return out->reason;
    }

    /*
     * **확인하지 않고 묶는다.** 확인 뒤에 묶으면 그 사이에 다른 워커가 끼어들어
     * 둘 다 통과한다. `acct_reserve()`가 계좌 락 안에서 잔고를 보고 묶으므로
     * 그 자체로 원자적이다 — 실패가 곧 증거금 부족이다.
     *
     * **원자성을 지키는 곳은 여기가 아니라 T3-06이다.** 불변조건 검사가 계좌
     * 락 안에 있어서, 설령 호출부가 미리 잔고를 봐 두었더라도 초과 배분이
     * 일어나지 않는다. 그 보호는 T3-06의 변이 검사가 확인한다(불변조건 검사를
     * 지우면 잡힌다).
     *
     * 아래 경쟁 테스트(T3-07)는 **그 조합이 실제로 맞물리는지**를 본다.
     * 경쟁 자체로 원자성을 증명하지는 못한다 — 검사와 묶기 사이의 틈은 명령어
     * 몇 개라 재현이 확률에 달려 있다. 증명은 불변조건이 락 안에 있다는
     * 구조에서 나오고, 테스트는 그 구조가 무너지지 않았는지를 본다.
     */
    int rc = acct_reserve(store, idx, out->margin);
    if (rc != ERR_OK) {
        out->margin = 0; /* 묶이지 않았다 */
        out->reason = (rc == ERR_INVALID_QTY) ? ERR_NO_MARGIN : rc;
        return out->reason;
    }

    out->reason = ERR_OK;
    return out->reason;
}
