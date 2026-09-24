package com.minisor.channel.store;

import java.util.List;
import org.springframework.stereotype.Component;

/**
 * 체결 기록을 되짚어 보유·현금·수익률을 낸다 (T11-02).
 *
 * <p><b>저장하지 않고 계산한다.</b> 보유와 현금을 따로 적어 두면 체결 기록과 언젠가
 * 어긋나고, 그때 어느 쪽이 맞는지 알 방법이 없다. 체결은 일어난 일이고 나머지는
 * 그것의 결과다.
 *
 * <p>순서대로 되짚는 이유: 평균 단가는 합계로 구할 수 없다. 팔 때 원가를 <b>그 시점의
 * 평균</b>으로 덜어 내야 실현 손익이 맞는다. 체결이 수백만 건이면 다시 볼 일이지만,
 * 그때는 되짚기가 아니라 일별 마감 잔고를 둘 때다.
 *
 * <p><b>통화를 섞지 않는다.</b> 국내는 원, 미국은 센트라 그냥 더하면 1억 원과
 * 100만 달러가 같은 수가 된다. {@code kind}가 다른 체결은 서로 보지 않는다.
 */
@Component
public class Portfolio {

    /**
     * 되짚은 결과.
     *
     * @param qty      보유 수량
     * @param cost     매입 원가 합. 평균 단가 = cost / qty
     * @param realized 실현 손익 누계 (판 것에서만 생긴다)
     * @param cash     지금 예수금 = 시작 자금 + 판 돈 - 산 돈
     * @param bought   산 금액 누계
     * @param sold     판 금액 누계
     * @param fills    체결 건수
     */
    public record Snapshot(
            long qty,
            long cost,
            long realized,
            long cash,
            long bought,
            long sold,
            int fills) {

        public static final Snapshot EMPTY = new Snapshot(0, 0, 0, 0, 0, 0, 0);

        /** 평균 단가. 보유가 없으면 0. */
        public long avgCost() {
            return qty > 0 ? cost / qty : 0;
        }

        /** 지금 시세로 친 평가 금액. */
        public long marketValue(long price) {
            return qty * price;
        }

        /** 평가 손익 — 들고 있는 것의 손익. 시세가 없으면(0) 0이다. */
        public long unrealized(long price) {
            return price > 0 ? qty * price - cost : 0;
        }

        /** 총 자산 = 예수금 + 평가 금액. */
        public long equity(long price) {
            return cash + marketValue(price);
        }

        /**
         * 수익률. (총 자산 - 시작 자금) / 시작 자금.
         *
         * <p>시작 자금이 0이면 나눌 수 없으므로 0을 돌려준다.
         */
        public double returnRate(long price, long seedCash) {
            if (seedCash <= 0) {
                return 0;
            }
            return (double) (equity(price) - seedCash) / (double) seedCash;
        }
    }

    private final FillStore fills;

    public Portfolio(FillStore fills) {
        this.fills = fills;
    }

    /**
     * 그 계좌의, 그 종목에 대한 지금 상태.
     *
     * @param symbol   보유를 셀 종목. 현금은 <b>같은 통화의 모든 종목</b>을 함께 센다 —
     *                 다른 종목을 사고판 것도 예수금을 움직였기 때문이다
     * @param kind     0=국내, 1=미국. 다른 통화의 체결은 아예 보지 않는다
     * @param seedCash 가입할 때 넣어 준 돈(그 통화)
     */
    public Snapshot of(String account, String symbol, int kind, long seedCash) {
        List<FillStore.Fill> all = fills.all(account);

        long qty = 0;
        long cost = 0;
        long realized = 0;
        long bought = 0;
        long sold = 0;
        int counted = 0;

        for (FillStore.Fill f : all) {
            if (f.kind() != kind) {
                continue; /* 통화가 다르다 */
            }
            counted++;
            if (f.side() == 0) { /* 매수 */
                bought += f.notional();
                if (f.symbol().equals(symbol)) {
                    qty += f.qty();
                    cost += f.notional();
                }
            } else { /* 매도 */
                sold += f.notional();
                if (f.symbol().equals(symbol) && qty > 0) {
                    /*
                     * 원가를 판 몫만큼 비례해 덜어 낸다. 마지막 수량을 팔면 남은
                     * 원가를 통째로 덜어 내 정확히 0이 된다 — 원장(C)의 계산과 같다.
                     */
                    long sell = Math.min(f.qty(), qty);
                    long costOut = (sell == qty) ? cost : (cost * sell) / qty;
                    realized += f.price() * sell - costOut;
                    cost -= costOut;
                    qty -= sell;
                }
            }
        }

        return new Snapshot(qty, cost, realized, seedCash + sold - bought, bought, sold,
                counted);
    }
}
