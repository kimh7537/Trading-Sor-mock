#include "recon.h"

#include <stdio.h>
#include <string.h>

#include "errors.h"

const char *recon_kind_str(recon_kind_t k)
{
    switch (k) {
    case RECON_LEG_SUM:
        return "다리 합 불일치";
    case RECON_LEG_OVERFILL:
        return "체결+취소가 보낸 수량 초과";
    case RECON_LIVE_NO_REMAIN:
        return "남아 있다는데 잔량 없음";
    case RECON_NOTIONAL:
        return "체결 수량과 체결 금액 불일치";
    case RECON_DUP_PHYS:
        return "물리 주문번호 중복";
    case RECON_CASH:
        return "예수금 불일치";
    case RECON_RESERVED:
        return "묶인 금액 불일치";
    case RECON_RESERVED_OVER_CASH:
        return "묶인 금액이 예수금 초과";
    case RECON_KIND_COUNT:
    default:
        return "?";
    }
}

void recon_report_init(recon_report_t *r, recon_finding_t *buf, int32_t cap)
{
    if (r == NULL) {
        return;
    }
    r->at = buf;
    r->cap = (buf != NULL && cap > 0) ? cap : 0;
    r->count = 0;
    r->dropped = 0;
}

bool recon_clean(const recon_report_t *r)
{
    /* 못 담은 것이 있으면 깨끗하지 않다. 못 본 것은 없는 것이 아니다. */
    return r != NULL && r->count == 0 && r->dropped == 0;
}

/*
 * 한 건 담는다. 자리가 없으면 `dropped`만 올린다.
 *
 * 자리가 없다고 **멈추지 않는다.** 남은 검사를 마저 돌아야 `dropped`가
 * 실제 건수가 된다 — 거기서 그만두면 "몇 건을 못 담았는지"조차 틀린다.
 */
static void add(recon_report_t *r, recon_kind_t kind, order_id_t logical_id,
                order_id_t phys_id, const char *account_no, int64_t expected,
                int64_t actual)
{
    if (r->count >= r->cap) {
        r->dropped++;
        return;
    }

    recon_finding_t *f = &r->at[r->count];
    memset(f, 0, sizeof(*f));
    f->kind = kind;
    f->logical_id = logical_id;
    f->phys_id = phys_id;
    f->expected = expected;
    f->actual = actual;
    if (account_no != NULL) {
        /*
         * 읽는 쪽과 쓰는 쪽을 **둘 다** 묶는다.
         *
         * `strncpy`는 쓰는 쪽만 묶는다. 그래서 (a) 꽉 찬 원본에서 끝의 NUL이
         * 사라지고, (b) `-O2`에서 `-Wstringop-truncation`으로 빌드가 깨진다
         * (T3-11에서 같은 덫에 걸렸다 — Debug에서는 안 보인다).
         *
         * `%.*s`는 정확히 그만큼만 읽고, `snprintf`가 NUL을 보장한다.
         * 원본이 끝나지 않은 배열이어도 넘겨 읽지 않는다.
         */
        snprintf(f->account_no, sizeof(f->account_no), "%.*s",
                 (int)(sizeof(f->account_no) - 1), account_no);
    }
    r->count++;
}

int recon_order(const recon_order_t *o, recon_report_t *r)
{
    if (o == NULL || r == NULL) {
        return ERR_NULL_PTR;
    }
    if (o->leg_count < 0 || o->leg_count > RECON_LEGS_MAX) {
        return ERR_INVALID_ARG;
    }

    /*
     * `qty_t`는 32비트다. 여덟 다리를 더하면 넘을 수 있으므로 합은 64비트로
     * 센다. **대사가 넘쳐서 틀리면 대사할 방법이 없다.**
     */
    int64_t sent_sum = 0;

    for (int32_t i = 0; i < o->leg_count; i++) {
        const recon_leg_t *lg = &o->legs[i];

        sent_sum += lg->sent_qty;

        int64_t done = (int64_t)lg->filled_qty + lg->canceled_qty;
        if (done > lg->sent_qty) {
            add(r, RECON_LEG_OVERFILL, o->logical_id, lg->phys_id, NULL,
                lg->sent_qty, done);
        }

        /*
         * 살아 있다는 것은 호가창에 남은 수량이 있다는 뜻이다. 남은 것이
         * 없는데 살아 있다고 되어 있으면 **끝났다는 통보를 놓친 것**이다 —
         * 그 주문은 영영 안 끝난 채로 남는다.
         */
        if (lg->live && done >= lg->sent_qty) {
            add(r, RECON_LIVE_NO_REMAIN, o->logical_id, lg->phys_id, NULL, 0,
                (int64_t)lg->sent_qty - done);
        }

        /* 체결이 없는데 금액이 있거나, 체결이 있는데 금액이 0이다. */
        if ((lg->filled_qty == 0) != (lg->notional == 0)) {
            add(r, RECON_NOTIONAL, o->logical_id, lg->phys_id, NULL,
                lg->filled_qty, lg->notional);
        }

        /*
         * 물리 번호가 겹치면 체결 통보가 어느 다리 것인지 정할 수 없다.
         * 다리는 최대 여덟이라 겹쳐 세도 64번이다 — 자료구조를 두지 않는다.
         */
        for (int32_t j = 0; j < i; j++) {
            if (o->legs[j].phys_id == lg->phys_id) {
                add(r, RECON_DUP_PHYS, o->logical_id, lg->phys_id, NULL, j, i);
                break;
            }
        }
    }

    if (sent_sum != o->order_qty) {
        add(r, RECON_LEG_SUM, o->logical_id, ORDER_ID_INVALID, NULL,
            o->order_qty, sent_sum);
    }

    return ERR_OK;
}

int recon_account(const recon_account_t *a, recon_report_t *r)
{
    if (a == NULL || r == NULL) {
        return ERR_NULL_PTR;
    }

    /*
     * **예수금은 설명될 수 있어야 한다.** 처음 값에서 들어온 것을 더하고
     * 나간 것을 뺀 결과가 지금 값이어야 한다. 한 푼이라도 남으면 어딘가에서
     * 돈이 생겼거나 사라진 것이고, 그것을 하루 뒤에 알면 늦다.
     */
    int64_t expect_cash = a->cash_start + a->deposits - a->withdrawals -
                          a->buy_notional + a->sell_notional;

    if (expect_cash != a->cash_now) {
        add(r, RECON_CASH, ORDER_ID_INVALID, ORDER_ID_INVALID, a->account_no,
            expect_cash, a->cash_now);
    }

    if (a->open_reserved != a->reserved_now) {
        add(r, RECON_RESERVED, ORDER_ID_INVALID, ORDER_ID_INVALID,
            a->account_no, a->open_reserved, a->reserved_now);
    }

    /*
     * 묶인 금액이 예수금을 넘으면 **없는 돈으로 주문을 받은 것**이다.
     * 위의 두 검사가 다 통과해도 이것이 틀릴 수 있다 — 원장의 두 값이
     * 서로 맞는지는 아무도 안 봤기 때문이다.
     */
    if (a->reserved_now > a->cash_now) {
        add(r, RECON_RESERVED_OVER_CASH, ORDER_ID_INVALID, ORDER_ID_INVALID,
            a->account_no, a->cash_now, a->reserved_now);
    }

    return ERR_OK;
}
