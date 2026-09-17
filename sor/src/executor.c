#include "executor.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"

/* 다리 하나를 그 시장의 엔진에 보낸다. 계획이 정한 주문 유형을 그대로 쓴다. */
static int send_leg(match_engine_t *eng, const order_t *req,
                    const plan_leg_t *leg, order_id_t phys_id,
                    exec_result_t *out)
{
    order_t phys;

    /*
     * 지정 초기화자로 만들면 패딩이 불특정이다. 엔진에 넘기는 값은 이벤트 스트림을
     * 타고 결정성 검증(T1-19)까지 흘러가므로 패딩까지 밀어 둔다.
     */
    memset(&phys, 0, sizeof(phys));
    phys.id = phys_id;
    phys.client_order_id = req->id; /* 어느 논리 주문에서 나왔는지 */
    phys.ts = req->ts;
    phys.price = leg->limit_price;
    phys.qty = leg->qty;
    phys.side = req->side;
    phys.type = leg->type;
    phys.market = leg->market;

    switch (leg->type) {
    case ORDER_MARKET:
        return match_market(eng, &phys, out);
    case ORDER_IOC:
        return match_ioc(eng, &phys, out);
    case ORDER_FOK:
        /*
         * 쪼갠 FOK는 원래 의미의 FOK가 아니다 — "전량 아니면 전무"가 다리 하나
         * 안에서만 성립한다. 시장을 가로지르는 전량 보장은 Phase 2 범위 밖이므로
         * 다리 단위로만 보낸다. 그 한계를 여기에 적어 둔다.
         */
        return match_fok(eng, &phys, out);
    case ORDER_LIMIT:
    case ORDER_MIDPOINT:
    default:
        /* 중간가는 엔진의 규칙 테이블이 가격을 다시 정한다(T1-14). */
        return match_limit(eng, &phys, out);
    }
}

/*
 * 체결을 매핑에 옮긴다.
 *
 * 건별로 옮긴다 — 체결 가격이 건마다 다르므로 평균 단가 하나로 뭉뚱그리면 나눗셈
 * 나머지만큼 체결 금액이 샌다. 평균 체결 단가가 이 프로젝트의 최종 산출물이라
 * 그 오차를 여기서 만들면 안 된다.
 *
 * 목록이 잘린 경우(EXEC_FILLS_MAX 초과)에만 남은 몫을 평균으로 넣는다. 집계값
 * (filled_qty, notional)은 정확하지만 평균 하나로 넣으면 나눗셈 나머지가 샌다 —
 * 그래서 "평균(버림) 가격 몫"과 "1원 높은 몫"으로 나눠 금액을 정확히 맞춘다(T7-07).
 */
static void record_fills(order_map_t *map, order_id_t phys_id,
                         const exec_result_t *res)
{
    qty_t   done = 0;
    int64_t done_notional = 0;

    for (int32_t i = 0; i < res->fill_count; i++) {
        int rc = omap_on_fill(map, phys_id, res->fills[i].qty,
                              res->fills[i].price);
        assert(rc == ERR_OK); /* 보낸 수량 안에서만 체결된다 */
        (void)rc;
        done += res->fills[i].qty;
        done_notional += (int64_t)res->fills[i].price * (int64_t)res->fills[i].qty;
    }

    if (done < res->filled_qty) {
        qty_t   rest = res->filled_qty - done;
        int64_t rest_notional = res->notional - done_notional;
        price_t avg = (price_t)(rest_notional / rest);
        qty_t   up = (qty_t)(rest_notional % rest); /* avg + 1원에 넣을 수량 */
        int     rc = ERR_OK;

        if (rest > up) {
            rc = omap_on_fill(map, phys_id, rest - up, avg);
            assert(rc == ERR_OK);
        }
        if (up > 0) {
            rc = omap_on_fill(map, phys_id, up, avg + 1);
            assert(rc == ERR_OK);
        }
        (void)rc;
    }
}

int exec_submit(order_map_t *map, venues_t *venues, const order_t *req,
                const exec_plan_t *plan, exec_report_t *out)
{
    if (map == NULL || venues == NULL || req == NULL || plan == NULL ||
        out == NULL) {
        return ERR_NULL_PTR;
    }

    memset(out, 0, sizeof(*out));

    order_id_t phys[PLAN_LEGS_MAX];
    int        rc = omap_register(map, req, plan, phys);
    if (rc != ERR_OK) {
        out->status = STATUS_REJECTED;
        return rc; /* 아무 시장에도 보내지 않았다 */
    }

    out->order_qty = req->qty;
    out->leg_count = plan->leg_count;

    int first_reject = ERR_OK;

    for (int32_t i = 0; i < plan->leg_count; i++) {
        const plan_leg_t *leg = &plan->legs[i];
        leg_result_t     *lr = &out->legs[i];
        exec_result_t     res;

        lr->phys_id = phys[i];
        lr->market = leg->market;
        lr->sent_qty = leg->qty;

        match_engine_t *eng = venues->eng[leg->market];
        if (eng == NULL) {
            lr->rc = ERR_NULL_PTR;
        } else {
            memset(&res, 0, sizeof(res));
            lr->rc = send_leg(eng, req, leg, phys[i], &res);
        }

        if (lr->rc != ERR_OK) {
            /*
             * 거부됐다. **다른 다리는 건드리지 않는다** — 이미 체결된 것을 되돌릴
             * 방법이 없다. 이 다리의 수량은 영원히 체결되지 않으므로 취소로 기록해
             * 살아 있는 잔량에서 뺀다.
             */
            out->rejected_count++;
            if (first_reject == ERR_OK) {
                first_reject = lr->rc;
            }
            int crc = omap_on_cancel(map, phys[i], leg->qty);
            assert(crc == ERR_OK);
            (void)crc;
            continue;
        }

        int arc = omap_on_accept(map, phys[i]);
        assert(arc == ERR_OK);
        (void)arc;

        lr->filled_qty = res.filled_qty;
        lr->notional = res.notional;
        lr->remaining_qty = res.remaining_qty;
        lr->resting = res.resting;

        if (lr->filled_qty > 0) {
            record_fills(map, phys[i], &res);
        }

        /*
         * 잔량이 호가창에 등록되지 않았다면(IOC·시장가) 그 수량은 취소된 것이다.
         * 지정가처럼 등록된 잔량은 아직 살아 있으므로 건드리지 않는다.
         */
        if (!lr->resting && lr->remaining_qty > 0) {
            int crc = omap_on_cancel(map, phys[i], lr->remaining_qty);
            assert(crc == ERR_OK);
            (void)crc;
        }

        out->filled_qty += lr->filled_qty;
        out->notional += lr->notional;
    }

    out->unfilled_qty = out->order_qty - out->filled_qty;
    out->working_qty = omap_remaining(map, req->id);

    int st_rc = exec_status(map, req->id, &out->status);
    assert(st_rc == ERR_OK); /* 방금 등록했으므로 반드시 있다 */
    (void)st_rc;

    /* 한 다리도 접수되지 못했을 때만 논리 주문이 거부된 것이다. */
    if (out->rejected_count == out->leg_count) {
        return first_reject;
    }
    return ERR_OK;
}

int exec_cancel(order_map_t *map, venues_t *venues, order_id_t logical_id,
                ts_t ts, cancel_report_t *out)
{
    if (map == NULL || venues == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    memset(out, 0, sizeof(*out));

    const logical_order_t *lo = omap_get(map, logical_id);
    if (lo == NULL) {
        return ERR_NOT_FOUND;
    }

    out->leg_count = lo->leg_count;

    int first_fail = ERR_OK;

    for (int32_t i = 0; i < lo->leg_count; i++) {
        cancel_leg_t *cl = &out->legs[i];
        order_id_t    phys_id = lo->legs[i].phys_id;

        cl->phys_id = phys_id;
        cl->market = lo->legs[i].market;

        /*
         * 이미 끝난 다리(전량 체결·거부·이미 취소)는 건너뛴다. 실패가 아니다 —
         * 취소할 것이 없을 뿐이다. "한쪽 이미 체결"이 바로 이 경우다.
         */
        if (!lo->legs[i].live) {
            cl->was_live = false;
            continue;
        }

        cl->was_live = true;
        out->attempted++;

        match_engine_t *eng = venues->eng[lo->legs[i].market];
        if (eng == NULL) {
            cl->rc = ERR_NULL_PTR;
        } else {
            exec_result_t res;
            memset(&res, 0, sizeof(res));
            cl->rc = match_cancel(eng, phys_id, ts, &res);
            if (cl->rc == ERR_OK) {
                cl->canceled_qty = res.remaining_qty;
            }
        }

        if (cl->rc != ERR_OK) {
            /*
             * 이 다리는 취소되지 못했다. **앞서 취소한 다리는 되돌리지 않는다** —
             * 되돌리는 것은 주문을 다시 내는 것이고 시간 우선순위를 복원할 수 없다.
             * 실패한 다리는 여전히 살아 있으므로 호출자가 재시도할 수 있다.
             */
            out->failed++;
            if (first_fail == ERR_OK) {
                first_fail = cl->rc;
            }
            continue;
        }

        int crc = omap_on_cancel(map, phys_id, cl->canceled_qty);
        assert(crc == ERR_OK);
        (void)crc;
        out->canceled_qty += cl->canceled_qty;
    }

    out->working_qty = omap_remaining(map, logical_id);

    int st_rc = exec_status(map, logical_id, &out->status);
    assert(st_rc == ERR_OK);
    (void)st_rc;

    if (out->attempted == 0) {
        /* 살아 있는 물리 주문이 없다. 매칭 엔진의 취소와 같은 뜻으로 답한다. */
        return ERR_NOT_FOUND;
    }
    return first_fail;
}

int exec_status(const order_map_t *map, order_id_t logical_id,
                order_status_t *out_status)
{
    if (out_status == NULL) {
        return ERR_NULL_PTR;
    }

    const logical_order_t *lo = omap_get(map, logical_id);
    if (lo == NULL) {
        return ERR_NOT_FOUND;
    }

    qty_t filled = omap_filled_qty(map, logical_id);
    qty_t working = omap_remaining(map, logical_id);

    if (filled >= lo->order_qty) {
        *out_status = STATUS_FILLED;
        return ERR_OK;
    }
    if (filled > 0) {
        /*
         * 체결이 한 주라도 있으면 그 사실이 상태를 지배한다. 한쪽이 거부돼도,
         * 나머지를 취소했어도 "부분 체결"이다 — 거부나 취소로 부르면 체결된 수량이
         * 상태에서 사라진다. 아직 진행 중인지는 working_qty가 말해 준다.
         */
        *out_status = STATUS_PARTIAL;
        return ERR_OK;
    }
    if (working > 0) {
        *out_status = STATUS_NEW; /* 아직 아무것도 안 됐지만 시장에 살아 있다 */
        return ERR_OK;
    }

    /*
     * 체결도 없고 살아 있는 것도 없다. 거부와 취소를 수량만으로는 구별할 수 없다 —
     * 둘 다 "체결 0, 취소 = 보낸 수량"이다. 거래소가 한 번이라도 받아 준 다리가
     * 있었는지로 가른다.
     */
    for (int32_t i = 0; i < lo->leg_count; i++) {
        if (lo->legs[i].accepted) {
            *out_status = STATUS_CANCELED;
            return ERR_OK;
        }
    }
    *out_status = STATUS_REJECTED;
    return ERR_OK;
}

const char *exec_status_name(order_status_t status)
{
    switch (status) {
    case STATUS_NEW:
        return "접수";
    case STATUS_PARTIAL:
        return "부분체결";
    case STATUS_FILLED:
        return "전량체결";
    case STATUS_CANCELED:
        return "취소";
    case STATUS_REJECTED:
        return "거부";
    default:
        return "알 수 없음";
    }
}
