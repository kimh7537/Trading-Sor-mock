package com.minisor.channel.api;

import com.minisor.channel.wire.DetailAck;
import java.util.ArrayList;
import java.util.List;

/**
 * 화면에 보이는 주문 하나 — 원장의 주문 상세(T7-02)를 JSON으로 옮긴 것(T7-03).
 *
 * <p>{@code legs}가 "논리 → 물리"다. 이 주문이 시장별로 얼마나 나갔고 얼마나 체결·취소됐는지.
 * 보낸 수량이 0인 시장은 싣지 않는다.
 *
 * @param market 주문할 때 고른 시장. 0 KRX, 1 NXT, 255 SOR 자동
 * @param working 아직 호가창에 살아 있는 수량
 * @param avgPrice 평균 체결가(원, 버림). 체결이 없으면 0. 정확한 값은 notional / filled
 * @param done 살아 있는 수량이 없다 — 체결·취소·거부로 끝났다
 */
public record OrderView(
        long orderId,
        long clOrdId,
        int side,
        int type,
        int market,
        int price,
        int qty,
        int filled,
        int canceled,
        int working,
        long notional,
        int avgPrice,
        int status,
        boolean done,
        List<LegView> legs) {

    /** 시장 하나로 나간 몫. */
    public record LegView(int market, int sent, int filled, int canceled, long notional, int avgPrice) {}

    static OrderView from(DetailAck d, int type) {
        List<LegView> legs = new ArrayList<>();
        for (int m = 0; m < DetailAck.LEGS; m++) {
            if (d.legSent[m] > 0) {
                legs.add(
                        new LegView(
                                m,
                                d.legSent[m],
                                d.legFilled[m],
                                d.legCanceled[m],
                                d.legNotional[m],
                                avg(d.legNotional[m], d.legFilled[m])));
            }
        }
        return new OrderView(
                d.orderId,
                d.clOrdId,
                d.side,
                type,
                d.market,
                d.price,
                d.qty,
                d.filled,
                d.canceled,
                d.working,
                d.notional,
                avg(d.notional, d.filled),
                d.status,
                d.working == 0,
                List.copyOf(legs));
    }

    /**
     * 상세를 못 읽었을 때 접수 응답만으로 만든 것. 시장별 몫은 모르므로 비워 두고,
     * 끝나지 않은 것으로 둬서 {@link LedgerPoller}가 다시 읽게 한다.
     */
    static OrderView fromAccepted(OrderRequestDto req, OrderResponseDto res) {
        int notionalAvg = res.filledQty() > 0 ? res.avgPrice() : 0;
        return new OrderView(
                res.orderId(),
                req.clOrdId(),
                req.side(),
                req.type(),
                req.market(),
                req.price(),
                req.qty(),
                res.filledQty(),
                0,
                req.qty() - res.filledQty(),
                (long) notionalAvg * res.filledQty(),
                notionalAvg,
                res.status(),
                false,
                List.of());
    }

    private static int avg(long notional, int filled) {
        return filled > 0 ? (int) (notional / filled) : 0;
    }
}
