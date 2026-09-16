/*
 * T3-07 주문 검증 로직.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 네 가지를 본다 — 거래가능종목 / 호가단위 / 주문 한도 / 증거금
 *  2. 거부 사유가 **서로 구분된다**
 *  3. 거부된 주문은 **증거금을 묶지 않는다**
 *  4. 증거금 확인과 묶기가 **한 번에** 일어난다 (경쟁에서 초과 배분되지 않는다)
 *  5. 가격을 알 수 없는 주문 유형을 받지 않는다
 *
 * 2번과 4번이 핵심이다. 사유를 뭉뚱그리면 주문을 낸 쪽이 엉뚱한 곳을 고치고,
 * 확인과 묶기가 갈라지면 **둘 다 통과하는 주문**이 생긴다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "errors.h"
#include "order_validate.h"
#include "tick_size.h"

#define STEP(fn)                                                            \
    do {                                                                   \
        fprintf(stderr, "[%s]\n", #fn);                                    \
        fn();                                                              \
    } while (0)

#define ACCT_CAP 8

typedef struct {
    shm_segment_t    *seg;
    account_store_t   store;
    validate_config_t cfg;
    int32_t           acct;
} fixture_t;

static void fx_init(fixture_t *fx, int64_t cash)
{
    int32_t counts[SHM_REGION_COUNT] = {ACCT_CAP, 4};
    size_t  sizes[SHM_REGION_COUNT] = {sizeof(account_t), 64};

    fx->seg = shm_create(counts, sizes);
    assert(fx->seg != NULL);
    assert(acct_store_init(&fx->store, fx->seg) == ERR_OK);

    fx->acct = acct_open(&fx->store, "ACC-001");
    assert(fx->acct >= 0);
    if (cash > 0) {
        assert(acct_deposit(&fx->store, fx->acct, cash) == ERR_OK);
    }

    vcfg_init(&fx->cfg);
    /* 증거금률 40%인 종목, 100%인 종목, 거래정지 종목. */
    assert(vcfg_add_symbol(&fx->cfg, "005930", 4000, true) == ERR_OK);
    assert(vcfg_add_symbol(&fx->cfg, "000660", MARGIN_BP_FULL, true) == ERR_OK);
    assert(vcfg_add_symbol(&fx->cfg, "HALTED", 4000, false) == ERR_OK);
    fx->cfg.max_order_notional = 100000000; /* 1억 */
}

static void fx_free(fixture_t *fx)
{
    acct_store_destroy(&fx->store);
    shm_destroy(fx->seg);
}

static msg_order_req_t make_req(const char *acct, const char *sym, uint8_t side,
                                uint8_t type, price_t price, qty_t qty)
{
    msg_order_req_t r;
    memset(&r, 0, sizeof(r));
    strncpy(r.account, acct, MSG_ACCOUNT_LEN);
    strncpy(r.symbol, sym, MSG_SYMBOL_LEN);
    r.cl_ord_id = 1;
    r.side = side;
    r.type = type;
    r.market = MARKET_KRX;
    r.price = price;
    r.qty = qty;
    return r;
}

/* --- 1. 종목 설정 --- */

static void test_symbol_config(void)
{
    validate_config_t cfg;
    vcfg_init(&cfg);
    assert(cfg.symbol_count == 0);
    assert(vcfg_find(&cfg, "005930") == NULL);

    assert(vcfg_add_symbol(&cfg, "005930", 4000, true) == ERR_OK);
    const symbol_rule_t *r = vcfg_find(&cfg, "005930");
    assert(r != NULL && r->margin_bp == 4000 && r->tradable);

    /* 같은 종목을 다시 넣으면 덮어쓴다 — 거래정지가 그렇게 들어온다. */
    assert(vcfg_add_symbol(&cfg, "005930", 5000, false) == ERR_OK);
    assert(cfg.symbol_count == 1);
    r = vcfg_find(&cfg, "005930");
    assert(r->margin_bp == 5000 && !r->tradable);

    /* 증거금률 범위. */
    assert(vcfg_add_symbol(&cfg, "X", 0, true) == ERR_INVALID_ARG);
    assert(vcfg_add_symbol(&cfg, "X", -1, true) == ERR_INVALID_ARG);
    assert(vcfg_add_symbol(&cfg, "X", MARGIN_BP_FULL + 1, true) ==
           ERR_INVALID_ARG);
    assert(vcfg_add_symbol(&cfg, "X", MARGIN_BP_FULL, true) == ERR_OK);

    assert(vcfg_add_symbol(NULL, "X", 100, true) == ERR_NULL_PTR);
    assert(vcfg_add_symbol(&cfg, NULL, 100, true) == ERR_NULL_PTR);
    assert(vcfg_add_symbol(&cfg, "", 100, true) == ERR_NULL_PTR);
    assert(vcfg_find(NULL, "X") == NULL);
    vcfg_init(NULL);

    /* 자리를 다 쓰면 더 못 넣는다. */
    vcfg_init(&cfg);
    for (int i = 0; i < VALIDATE_SYMBOLS_MAX; i++) {
        char s[MSG_SYMBOL_LEN + 1];
        snprintf(s, sizeof(s), "S%06d", i);
        assert(vcfg_add_symbol(&cfg, s, 1000, true) == ERR_OK);
    }
    assert(vcfg_add_symbol(&cfg, "OVERFLO", 1000, true) == ERR_POOL_EXHAUSTED);
}

/* --- 2. 증거금 계산 --- */

/*
 * **올림이다.** 내림하면 묶는 돈이 모자란다.
 * 손으로 계산한 값과 맞춰 본다.
 */
static void test_margin_for(void)
{
    /* 100% — 전액 */
    assert(margin_for(1000000, MARGIN_BP_FULL) == 1000000);

    /* 40% — 1,000,000 x 0.4 = 400,000 (딱 떨어진다) */
    assert(margin_for(1000000, 4000) == 400000);

    /*
     * 40% — 1,000,001 x 0.4 = 400,000.4 -> 올림 400,001
     * 내림 구현이면 400,000이 나온다.
     */
    assert(margin_for(1000001, 4000) == 400001);

    /* 1bp — 10,000원의 0.01% = 1원 */
    assert(margin_for(10000, 1) == 1);
    /* 9,999원의 0.01% = 0.9999 -> 올림 1 */
    assert(margin_for(9999, 1) == 1);

    /* 잘못된 인자. */
    assert(margin_for(0, 4000) == 0);
    assert(margin_for(-1, 4000) == 0);
    assert(margin_for(1000, 0) == 0);
    assert(margin_for(1000, MARGIN_BP_FULL + 1) == 0);
}

/* --- 3. 거부 사유가 구분되는가 --- */

/*
 * 완료 조건 2. **사유가 서로 다른 코드로 나온다.**
 *
 * 하나로 뭉뚱그리면 주문을 낸 쪽이 어디를 고쳐야 할지 모른다. 그리고 거부될
 * 때마다 **증거금이 묶이지 않았는지**도 같이 본다 — 거부하면서 돈만 묶어 두면
 * 그 계좌는 조용히 말라붙는다.
 */
static void test_rejection_reasons_are_distinct(void)
{
    fixture_t fx;
    fx_init(&fx, 10000000); /* 1천만 원 */

    int64_t before = acct_available(&fx.store, fx.acct);

    struct {
        const char     *what;
        msg_order_req_t req;
        int             want;
    } cases[] = {
        {"없는 계좌",
         make_req("NO-SUCH", "005930", SIDE_BUY, ORDER_LIMIT, 10000, 10),
         ERR_NOT_FOUND},
        {"모르는 종목",
         make_req("ACC-001", "999999", SIDE_BUY, ORDER_LIMIT, 10000, 10),
         ERR_NOT_SUPPORTED},
        {"거래정지 종목",
         make_req("ACC-001", "HALTED", SIDE_BUY, ORDER_LIMIT, 10000, 10),
         ERR_NOT_SUPPORTED},
        {"시장가(가격을 모른다)",
         make_req("ACC-001", "005930", SIDE_BUY, ORDER_MARKET, 10000, 10),
         ERR_NOT_SUPPORTED},
        {"중간가(가격을 모른다)",
         make_req("ACC-001", "005930", SIDE_BUY, ORDER_MIDPOINT, 10000, 10),
         ERR_NOT_SUPPORTED},
        {"수량 0",
         make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 10000, 0),
         ERR_INVALID_QTY},
        {"수량 초과",
         make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 10000,
                  QTY_MAX + 1),
         ERR_INVALID_QTY},
        {"가격 0",
         make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 0, 10),
         ERR_INVALID_PRICE},
        {"호가 단위 위반",
         make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 10005, 10),
         ERR_INVALID_TICK},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        validate_result_t res;
        int rc = validate_order(&fx.store, &fx.cfg, &cases[i].req, &res);
        assert(rc == cases[i].want);
        assert(res.reason == cases[i].want);
        /* 거부했으면 묶은 것이 없다. */
        assert(res.margin == 0);
        assert(acct_available(&fx.store, fx.acct) == before);
    }

    /* 한도 초과는 또 다른 코드다. */
    fx.cfg.max_order_notional = 1000000; /* 100만 원 */
    msg_order_req_t big =
        make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 10000, 200);
    validate_result_t res;
    assert(validate_order(&fx.store, &fx.cfg, &big, &res) ==
           ERR_LIMIT_EXCEEDED);
    assert(res.notional == 2000000);
    assert(res.margin == 0);
    assert(acct_available(&fx.store, fx.acct) == before);

    /* 인자 검사. */
    assert(validate_order(NULL, &fx.cfg, &big, &res) == ERR_NULL_PTR);
    assert(validate_order(&fx.store, NULL, &big, &res) == ERR_NULL_PTR);
    assert(validate_order(&fx.store, &fx.cfg, NULL, &res) == ERR_NULL_PTR);
    /* 결과를 안 받아도 검증은 돈다. */
    assert(validate_order(&fx.store, &fx.cfg, &big, NULL) ==
           ERR_LIMIT_EXCEEDED);

    fx_free(&fx);
}

/* --- 4. 통과와 증거금 --- */

static void test_accept_reserves_margin(void)
{
    fixture_t fx;
    fx_init(&fx, 10000000);

    /* 10,000원 x 100주 = 1,000,000원. 증거금률 40% -> 400,000원이 묶인다. */
    msg_order_req_t req =
        make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 10000, 100);
    validate_result_t res;
    assert(validate_order(&fx.store, &fx.cfg, &req, &res) == ERR_OK);
    assert(res.account_index == fx.acct);
    assert(res.notional == 1000000);
    assert(res.margin == 400000);

    int64_t cash = 0;
    int64_t reserved = 0;
    assert(acct_snapshot(&fx.store, fx.acct, &cash, &reserved) == ERR_OK);
    assert(cash == 10000000);
    assert(reserved == 400000);
    assert(acct_available(&fx.store, fx.acct) == 9600000);

    /* 증거금률 100% 종목은 전액을 묶는다. */
    msg_order_req_t full =
        make_req("ACC-001", "000660", SIDE_BUY, ORDER_LIMIT, 10000, 100);
    assert(validate_order(&fx.store, &fx.cfg, &full, &res) == ERR_OK);
    assert(res.margin == 1000000);
    assert(acct_available(&fx.store, fx.acct) == 8600000);

    /*
     * 매도는 현금 증거금을 묶지 않는다 — 주식 잔고 확인은 이 프로젝트의 범위
     * 밖이라는 것을 여기서 못 박는다.
     */
    int64_t         before = acct_available(&fx.store, fx.acct);
    msg_order_req_t sell =
        make_req("ACC-001", "005930", SIDE_SELL, ORDER_LIMIT, 10000, 100);
    assert(validate_order(&fx.store, &fx.cfg, &sell, &res) == ERR_OK);
    assert(res.margin == 0);
    assert(acct_available(&fx.store, fx.acct) == before);

    /* IOC·FOK도 지정가이므로 받는다. */
    msg_order_req_t ioc =
        make_req("ACC-001", "005930", SIDE_BUY, ORDER_IOC, 10000, 10);
    assert(validate_order(&fx.store, &fx.cfg, &ioc, &res) == ERR_OK);
    msg_order_req_t fok =
        make_req("ACC-001", "005930", SIDE_BUY, ORDER_FOK, 10000, 10);
    assert(validate_order(&fx.store, &fx.cfg, &fok, &res) == ERR_OK);

    fx_free(&fx);
}

/* 증거금이 모자라면 ERR_NO_MARGIN이고 아무것도 묶이지 않는다. */
static void test_insufficient_margin(void)
{
    fixture_t fx;
    fx_init(&fx, 100000); /* 10만 원뿐 */

    /* 1,000,000원 주문의 40% = 400,000원 > 100,000원 */
    msg_order_req_t req =
        make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 10000, 100);
    validate_result_t res;
    assert(validate_order(&fx.store, &fx.cfg, &req, &res) == ERR_NO_MARGIN);
    assert(res.margin == 0);
    assert(acct_available(&fx.store, fx.acct) == 100000);

    /* 딱 맞는 크기는 통과한다 — 한도가 지나치게 빡빡하지 않다. */
    msg_order_req_t fit =
        make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 10000, 25);
    assert(validate_order(&fx.store, &fx.cfg, &fit, &res) == ERR_OK);
    assert(res.margin == 100000);
    assert(acct_available(&fx.store, fx.acct) == 0);

    /* 이제 1주짜리도 안 된다. */
    msg_order_req_t one =
        make_req("ACC-001", "005930", SIDE_BUY, ORDER_LIMIT, 10000, 1);
    assert(validate_order(&fx.store, &fx.cfg, &one, &res) == ERR_NO_MARGIN);

    fx_free(&fx);
}

/* --- 5. 경쟁에서 초과 배분되지 않는가 --- */

/*
 * 완료 조건 4. **이 태스크에서 가장 말할 만한 부분이다.**
 *
 * 계좌에 정확히 10건치 증거금만 넣어 두고, 자식 넷이 각자 10건씩 주문을 낸다.
 * 총 40번 시도하지만 **통과는 정확히 10건**이어야 한다.
 *
 * "쓸 수 있는 돈이 충분한가"를 본 뒤에 따로 묶는 구현이면 그 사이에 다른
 * 프로세스가 끼어들어 10건을 넘긴다. 확인과 묶기가 한 번에 일어나야만 정확히
 * 맞는다.
 *
 * 자식은 통과 건수를 파이프로 알린다(T3-04와 같은 방식 — 1바이트 쓰기는
 * 커널이 원자성을 보장한다).
 */
static void test_no_overcommit_under_contention(void)
{
    fixture_t fx;

    /* 10,000원 x 10주 = 100,000원, 40% -> 40,000원. 10건이면 400,000원. */
    const int64_t PER_ORDER_MARGIN = 40000;
    const int     ALLOWED = 10;

    fx_init(&fx, PER_ORDER_MARGIN * ALLOWED);

    int fds[2];
    assert(pipe(fds) == 0);

    const int KIDS = 4;
    const int TRIES = 10;

    for (int k = 0; k < KIDS; k++) {
        pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            close(fds[0]);
            for (int i = 0; i < TRIES; i++) {
                msg_order_req_t req = make_req("ACC-001", "005930", SIDE_BUY,
                                               ORDER_LIMIT, 10000, 10);
                validate_result_t res;
                int rc = validate_order(&fx.store, &fx.cfg, &req, &res);
                if (rc == ERR_OK) {
                    uint8_t one = 1;
                    if (write(fds[1], &one, 1) != 1) {
                        _exit(1);
                    }
                } else if (rc != ERR_NO_MARGIN) {
                    _exit(2); /* 다른 사유로 떨어지면 시험이 성립하지 않는다 */
                }
            }
            close(fds[1]);
            _exit(0);
        }
    }

    close(fds[1]);

    for (int k = 0; k < KIDS; k++) {
        int status = 0;
        assert(wait(&status) > 0);
        assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    /* 통과 건수를 센다. */
    int     accepted = 0;
    uint8_t buf[128];
    for (;;) {
        ssize_t r = read(fds[0], buf, sizeof(buf));
        if (r <= 0) {
            break;
        }
        accepted += (int)r;
    }
    close(fds[0]);

    /* **정확히 10건.** 초과 배분도, 과소 배분도 없다. */
    assert(accepted == ALLOWED);

    /* 잔고도 맞는다 — 쓸 수 있는 돈이 0이고 묶인 돈이 전부다. */
    int64_t cash = 0;
    int64_t reserved = 0;
    assert(acct_snapshot(&fx.store, fx.acct, &cash, &reserved) == ERR_OK);
    assert(reserved == PER_ORDER_MARGIN * ALLOWED);
    assert(cash - reserved == 0);

    fx_free(&fx);
}

int main(void)
{
    STEP(test_symbol_config);
    STEP(test_margin_for);
    STEP(test_rejection_reasons_are_distinct);
    STEP(test_accept_reserves_margin);
    STEP(test_insufficient_margin);
    STEP(test_no_overcommit_under_contention);
    return 0;
}
