/*
 * T5-03 정합성 대사.
 *
 * 완료 조건을 그대로 옮긴다: 맞는 경우, 어긋남 종류별, **여러 개가
 * 한꺼번에**, 담을 자리 부족.
 *
 * 숫자는 일부러 맞아떨어지지 않게 고른다. 100·200 같은 값을 쓰면 더하기와
 * 빼기를 뒤바꾼 구현도 우연히 통과한다(T3-11에서 한 번 당했다).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "recon.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

static recon_finding_t g_buf[16];
static recon_report_t  g_rep;

static void fresh(void)
{
    memset(g_buf, 0, sizeof(g_buf));
    recon_report_init(&g_rep, g_buf, 16);
}

/* 보고서에 이 종류가 몇 건 있는가. */
static int32_t count_of(recon_kind_t k)
{
    int32_t n = 0;
    for (int32_t i = 0; i < g_rep.count; i++) {
        if (g_rep.at[i].kind == k) {
            n++;
        }
    }
    return n;
}

static const recon_finding_t *first_of(recon_kind_t k)
{
    for (int32_t i = 0; i < g_rep.count; i++) {
        if (g_rep.at[i].kind == k) {
            return &g_rep.at[i];
        }
    }
    return NULL;
}

/*
 * 성한 주문. 논리 731주가 두 시장으로 437 + 294로 갈라졌다.
 * 첫 다리는 다 됐고(체결 291 + 취소 146), 둘째는 아직 살아 있다.
 */
static recon_order_t good_order(void)
{
    recon_order_t o;
    memset(&o, 0, sizeof(o));
    o.logical_id = 9137;
    o.order_qty = 731;
    o.leg_count = 2;

    o.legs[0].phys_id = 146193;
    o.legs[0].sent_qty = 437;
    o.legs[0].filled_qty = 291;
    o.legs[0].canceled_qty = 146;
    o.legs[0].notional = 291 * 68350;
    o.legs[0].live = false;

    o.legs[1].phys_id = 146194;
    o.legs[1].sent_qty = 294;
    o.legs[1].filled_qty = 113;
    o.legs[1].canceled_qty = 0;
    o.legs[1].notional = 113 * 68400;
    o.legs[1].live = true;

    return o;
}

static void test_clean_order(void)
{
    fresh();
    recon_order_t o = good_order();
    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(recon_clean(&g_rep));
    assert(g_rep.count == 0);
    assert(g_rep.dropped == 0);
}

/* 다리 합이 주문 수량과 다르다 — 주문 일부가 아예 안 나갔거나 두 번 나갔다 */
static void test_leg_sum(void)
{
    fresh();
    recon_order_t o = good_order();
    o.legs[1].sent_qty = 293; /* 한 주 모자라다 */
    o.legs[1].canceled_qty = 0;

    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(!recon_clean(&g_rep));
    assert(count_of(RECON_LEG_SUM) == 1);

    const recon_finding_t *f = first_of(RECON_LEG_SUM);
    assert(f != NULL);
    assert(f->logical_id == 9137);
    assert(f->expected == 731); /* 어디의 무엇이 얼마만큼인지 담는다 */
    assert(f->actual == 730);
}

/* 체결+취소가 보낸 수량을 넘었다 */
static void test_overfill(void)
{
    fresh();
    recon_order_t o = good_order();
    o.legs[0].filled_qty = 300; /* 300 + 146 = 446 > 437 */
    o.legs[0].notional = 300 * 68350;

    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_LEG_OVERFILL) == 1);

    const recon_finding_t *f = first_of(RECON_LEG_OVERFILL);
    assert(f->phys_id == 146193);
    assert(f->expected == 437);
    assert(f->actual == 446);
}

/* 살아 있다는데 남은 수량이 없다 — 끝났다는 통보를 놓쳤다 */
static void test_live_no_remain(void)
{
    fresh();
    recon_order_t o = good_order();
    o.legs[1].filled_qty = 294; /* 다 찼는데 live가 그대로다 */
    o.legs[1].notional = 294 * 68400;

    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_LIVE_NO_REMAIN) == 1);
    assert(first_of(RECON_LIVE_NO_REMAIN)->phys_id == 146194);

    /* 반대로 안 살아 있으면서 남은 것이 있는 것은 어긋남이 아니다
       — 취소된 뒤 잔량이 남는 것은 정상이다. */
    fresh();
    o = good_order();
    o.legs[1].live = false;
    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_LIVE_NO_REMAIN) == 0);
}

/* 체결 수량과 체결 금액이 서로를 배반한다 */
static void test_notional(void)
{
    /* 체결은 0인데 금액이 있다 */
    fresh();
    recon_order_t o = good_order();
    o.legs[1].filled_qty = 0;
    o.legs[1].canceled_qty = 0;
    /* notional은 그대로 둔다 */
    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_NOTIONAL) == 1);

    /* 체결이 있는데 금액이 0이다 */
    fresh();
    o = good_order();
    o.legs[0].notional = 0;
    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_NOTIONAL) == 1);
    assert(first_of(RECON_NOTIONAL)->phys_id == 146193);

    /* 둘 다 0이면 어긋남이 아니다 */
    fresh();
    o = good_order();
    o.legs[1].filled_qty = 0;
    o.legs[1].notional = 0;
    o.legs[1].canceled_qty = 0;
    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_NOTIONAL) == 0);
}

/* 물리 번호가 겹치면 체결 통보를 어느 다리에 붙일지 정할 수 없다 */
static void test_dup_phys(void)
{
    fresh();
    recon_order_t o = good_order();
    o.legs[1].phys_id = o.legs[0].phys_id;

    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_DUP_PHYS) == 1);
    assert(first_of(RECON_DUP_PHYS)->phys_id == 146193);
}

/*
 * **여러 개가 한꺼번에.** 첫 번째에서 멈추면 나머지는 다음 대사 때까지
 * 안 보인다.
 */
static void test_many_at_once(void)
{
    fresh();
    recon_order_t o;
    memset(&o, 0, sizeof(o));
    o.logical_id = 5521;
    o.order_qty = 900; /* 다리 합은 617 + 284 = 901 — 하나 더 */
    o.leg_count = 2;

    o.legs[0].phys_id = 88353;
    o.legs[0].sent_qty = 617;
    o.legs[0].filled_qty = 500;
    o.legs[0].canceled_qty = 200; /* 700 > 617 — 초과 */
    o.legs[0].notional = 0;       /* 체결이 있는데 금액 0 — 불일치 */
    o.legs[0].live = true;        /* 남은 것이 없는데 live — 불일치 */

    o.legs[1].phys_id = 88353; /* 겹침 */
    o.legs[1].sent_qty = 284;
    o.legs[1].filled_qty = 0;
    o.legs[1].canceled_qty = 0;
    o.legs[1].notional = 0;
    o.legs[1].live = true;

    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(g_rep.dropped == 0);

    /* 다섯 종류가 모두 잡혀야 한다 */
    assert(count_of(RECON_LEG_OVERFILL) == 1);
    assert(count_of(RECON_LIVE_NO_REMAIN) == 1);
    assert(count_of(RECON_NOTIONAL) == 1);
    assert(count_of(RECON_DUP_PHYS) == 1);
    assert(count_of(RECON_LEG_SUM) == 1);
    assert(g_rep.count == 5);
}

/*
 * 담을 자리가 모자라면 **자른 사실을 숨기지 않는다.**
 * "두 건"과 "두 건까지 세다 말았다"는 완전히 다른 보고다.
 */
static void test_report_full(void)
{
    recon_finding_t small[2];
    recon_report_t  rep;
    recon_report_init(&rep, small, 2);

    recon_order_t o;
    memset(&o, 0, sizeof(o));
    o.logical_id = 5521;
    o.order_qty = 900;
    o.leg_count = 2;
    o.legs[0].phys_id = 88353;
    o.legs[0].sent_qty = 617;
    o.legs[0].filled_qty = 500;
    o.legs[0].canceled_qty = 200;
    o.legs[0].notional = 0;
    o.legs[0].live = true;
    o.legs[1].phys_id = 88353;
    o.legs[1].sent_qty = 284;
    o.legs[1].live = true;

    assert(recon_order(&o, &rep) == ERR_OK);
    assert(rep.count == 2);
    /* 자리가 없다고 멈추지 않았으므로 나머지 셋을 정확히 세었다 */
    assert(rep.dropped == 3);
    assert(!recon_clean(&rep)); /* 못 본 것은 없는 것이 아니다 */

    /* 자리를 아예 안 주면 전부 dropped로 센다 */
    recon_report_t none;
    recon_report_init(&none, NULL, 0);
    assert(recon_order(&o, &none) == ERR_OK);
    assert(none.count == 0);
    assert(none.dropped == 5);
    assert(!recon_clean(&none));

    /*
     * **버퍼가 없는데 자리가 있다고 말하는 경우.** 부르는 쪽의 실수이지만
     * 여기는 바깥에서 값이 들어오는 자리라 믿지 않는다 — cap을 곧이곧대로
     * 받으면 NULL을 타고 써서 죽는다.
     */
    recon_report_t lying;
    recon_report_init(&lying, NULL, 16);
    assert(lying.cap == 0);
    assert(recon_order(&o, &lying) == ERR_OK);
    assert(lying.count == 0);
    assert(lying.dropped == 5);

    /* 음수 자리도 마찬가지다 */
    recon_report_init(&lying, small, -3);
    assert(lying.cap == 0);
}

/* 다리가 없는 주문 — 수량이 0이면 맞고, 아니면 합이 안 맞는다 */
static void test_no_legs(void)
{
    fresh();
    recon_order_t o;
    memset(&o, 0, sizeof(o));
    o.logical_id = 3;
    o.order_qty = 0;
    o.leg_count = 0;
    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(recon_clean(&g_rep));

    fresh();
    o.order_qty = 55; /* 보내지도 않고 주문만 있다 */
    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_LEG_SUM) == 1);
    assert(first_of(RECON_LEG_SUM)->actual == 0);
}

/*
 * **대사가 넘쳐서 틀리면 대사할 방법이 없다.**
 *
 * `qty_t`는 32비트다. 여덟 다리를 더하면 넘을 수 있는데, 대사가 보는 것은
 * 이미 어긋났을지 모르는 기록이므로 "정상이면 안 넘는다"는 전제를 둘 수
 * 없다. 넘친 합으로 비교하면 어긋남을 못 보거나 엉뚱한 수치를 보고한다.
 */
static void test_leg_sum_does_not_overflow(void)
{
    fresh();
    recon_order_t o;
    memset(&o, 0, sizeof(o));
    o.logical_id = 77;
    o.order_qty = 2000000000;
    o.leg_count = 2;
    o.legs[0].phys_id = 1;
    o.legs[0].sent_qty = 1500000000;
    o.legs[0].canceled_qty = 1500000000;
    o.legs[1].phys_id = 2;
    o.legs[1].sent_qty = 1500000000;
    o.legs[1].canceled_qty = 1500000000;

    assert(recon_order(&o, &g_rep) == ERR_OK);
    assert(count_of(RECON_LEG_SUM) == 1);

    /* 32비트로 더했다면 감싸 돌아 음수가 나온다 */
    assert(first_of(RECON_LEG_SUM)->actual == 3000000000LL);
}

/* --- 잔고 <-> 체결 <-> 원장 --- */

static recon_account_t good_account(void)
{
    recon_account_t a;
    memset(&a, 0, sizeof(a));
    strncpy(a.account_no, "31940771", sizeof(a.account_no) - 1);

    a.cash_start = 17431500;
    a.deposits = 2500000;
    a.withdrawals = 380000;
    a.buy_notional = 9885450;
    a.sell_notional = 3117200;
    /* 17431500 + 2500000 - 380000 - 9885450 + 3117200 */
    a.cash_now = 12783250;

    a.reserved_now = 4116800;
    a.open_reserved = 4116800;
    return a;
}

static void test_clean_account(void)
{
    fresh();
    recon_account_t a = good_account();
    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(recon_clean(&g_rep));
}

/* 예수금이 입출금과 체결로 설명되지 않는다 */
static void test_cash_mismatch(void)
{
    fresh();
    recon_account_t a = good_account();
    a.cash_now += 17; /* 어디선가 17원이 생겼다 */

    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(count_of(RECON_CASH) == 1);

    const recon_finding_t *f = first_of(RECON_CASH);
    assert(strcmp(f->account_no, "31940771") == 0);
    assert(f->expected == 12783250);
    assert(f->actual == 12783267);

    /*
     * **매수와 매도의 부호가 반대라는 것을 못 박는다.**
     *
     * 한쪽만 움직이고 기대값이 어느 쪽으로 가는지를 본다. 양쪽에 같은
     * 금액을 더하면 올바른 구현에서도 상쇄되어 아무것도 안 잡힌다 —
     * 그러면 이 검사가 통과해도 아무 뜻이 없다.
     */
    fresh();
    a = good_account();
    a.buy_notional += 1000; /* 나간 돈이 늘면 남을 돈은 줄어야 한다 */
    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(count_of(RECON_CASH) == 1);
    assert(first_of(RECON_CASH)->expected == 12783250 - 1000);

    fresh();
    a = good_account();
    a.sell_notional += 1000; /* 들어온 돈이 늘면 남을 돈도 늘어야 한다 */
    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(count_of(RECON_CASH) == 1);
    assert(first_of(RECON_CASH)->expected == 12783250 + 1000);

    /* 입금과 출금도 마찬가지다 */
    fresh();
    a = good_account();
    a.withdrawals += 700;
    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(first_of(RECON_CASH)->expected == 12783250 - 700);

    fresh();
    a = good_account();
    a.deposits += 700;
    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(first_of(RECON_CASH)->expected == 12783250 + 700);
}

/* 묶인 금액이 미체결 주문들의 몫과 다르다 */
static void test_reserved_mismatch(void)
{
    fresh();
    recon_account_t a = good_account();
    a.open_reserved = 3990000; /* 취소된 주문의 몫을 안 풀었다 */

    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(count_of(RECON_RESERVED) == 1);
    assert(first_of(RECON_RESERVED)->expected == 3990000);
    assert(first_of(RECON_RESERVED)->actual == 4116800);
    assert(count_of(RECON_CASH) == 0); /* 예수금은 그대로 맞다 */
}

/*
 * 묶인 금액이 예수금을 넘었다 — **없는 돈으로 주문을 받았다.**
 * 앞의 두 검사가 다 통과해도 이것이 틀릴 수 있다.
 */
static void test_reserved_over_cash(void)
{
    fresh();
    recon_account_t a = good_account();
    a.reserved_now = 13000000; /* > cash_now 12783250 */
    a.open_reserved = 13000000;

    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(count_of(RECON_CASH) == 0);
    assert(count_of(RECON_RESERVED) == 0);
    assert(count_of(RECON_RESERVED_OVER_CASH) == 1);
    assert(first_of(RECON_RESERVED_OVER_CASH)->expected == 12783250);
    assert(first_of(RECON_RESERVED_OVER_CASH)->actual == 13000000);

    /* 딱 같은 금액은 넘은 것이 아니다 */
    fresh();
    a = good_account();
    a.reserved_now = a.cash_now;
    a.open_reserved = a.cash_now;
    assert(recon_account(&a, &g_rep) == ERR_OK);
    assert(recon_clean(&g_rep));
}

/* --- 이름과 인자 --- */

static void test_kind_str(void)
{
    for (int k = 0; k < RECON_KIND_COUNT; k++) {
        const char *s = recon_kind_str((recon_kind_t)k);
        assert(s != NULL);
        assert(strcmp(s, "?") != 0); /* 빠뜨린 종류가 없다 */
    }
    assert(strcmp(recon_kind_str(RECON_KIND_COUNT), "?") == 0);
    assert(strcmp(recon_kind_str((recon_kind_t)999), "?") == 0);
}

static void test_args(void)
{
    recon_order_t   o = good_order();
    recon_account_t a = good_account();

    fresh();
    assert(recon_order(NULL, &g_rep) == ERR_NULL_PTR);
    assert(recon_order(&o, NULL) == ERR_NULL_PTR);
    assert(recon_account(NULL, &g_rep) == ERR_NULL_PTR);
    assert(recon_account(&a, NULL) == ERR_NULL_PTR);

    o.leg_count = RECON_LEGS_MAX + 1;
    assert(recon_order(&o, &g_rep) == ERR_INVALID_ARG);
    o.leg_count = -1;
    assert(recon_order(&o, &g_rep) == ERR_INVALID_ARG);

    /* 인자가 잘못되면 보고서를 건드리지 않는다 */
    assert(g_rep.count == 0);
    assert(g_rep.dropped == 0);

    /* 보고서 초기화는 NULL을 견딘다 */
    recon_report_init(NULL, g_buf, 16);
    assert(!recon_clean(NULL));
}

int main(void)
{
    STEP(test_clean_order);
    STEP(test_leg_sum);
    STEP(test_overfill);
    STEP(test_live_no_remain);
    STEP(test_notional);
    STEP(test_dup_phys);
    STEP(test_many_at_once);
    STEP(test_report_full);
    STEP(test_no_legs);
    STEP(test_leg_sum_does_not_overflow);
    STEP(test_clean_account);
    STEP(test_cash_mismatch);
    STEP(test_reserved_mismatch);
    STEP(test_reserved_over_cash);
    STEP(test_kind_str);
    STEP(test_args);
    return 0;
}
