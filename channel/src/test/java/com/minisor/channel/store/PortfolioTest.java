package com.minisor.channel.store;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.within;

import java.nio.file.Path;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/**
 * T11-02 — 체결을 되짚어 보유·평균 단가·실현 손익·수익률을 낸다.
 *
 * <p>이 시험이 지키는 것: <b>평균 단가와 실현 손익은 합계로 구할 수 없다.</b> 팔 때
 * 원가를 그 시점의 평균으로 덜어 내야 맞고, 다 팔면 원가가 정확히 0이어야 한다.
 */
class PortfolioTest {

    private static final String ACC = "u00000000001";
    private static final String KR = "005930";
    private static final long SEED = 100_000_000L;

    @TempDir Path dir;

    private FillStore fills;
    private Portfolio portfolio;

    @BeforeEach
    void open() {
        Db db = new Db(dir.resolve("minisor.db").toString());
        fills = new FillStore(db);
        portfolio = new Portfolio(fills);
    }

    private void buy(String symbol, int kind, long price, long qty, long orderId) {
        fills.record(ACC, symbol, kind, 0, 0, price, qty, orderId, orderId);
    }

    private void sell(String symbol, int kind, long price, long qty, long orderId) {
        fills.record(ACC, symbol, kind, 1, 0, price, qty, orderId, orderId);
    }

    /** 거래가 없으면 시작 자금 그대로다. */
    @Test
    void emptyAccountIsJustSeedCash() {
        Portfolio.Snapshot s = portfolio.of(ACC, KR, 0, SEED);
        assertThat(s.qty()).isZero();
        assertThat(s.cash()).isEqualTo(SEED);
        assertThat(s.realized()).isZero();
        assertThat(s.returnRate(70_000, SEED)).isZero();
    }

    /** 사면 보유와 원가가 쌓이고 현금이 준다. */
    @Test
    void buyingBuildsPositionAndSpendsCash() {
        buy(KR, 0, 70_000, 10, 1);
        buy(KR, 0, 80_000, 10, 2);

        Portfolio.Snapshot s = portfolio.of(ACC, KR, 0, SEED);
        assertThat(s.qty()).isEqualTo(20);
        assertThat(s.cost()).isEqualTo(1_500_000);
        assertThat(s.avgCost()).isEqualTo(75_000);
        assertThat(s.cash()).isEqualTo(SEED - 1_500_000);
        assertThat(s.realized()).isZero();
    }

    /**
     * 팔면 실현 손익이 생기고, <b>다 팔면 원가가 정확히 0</b>이다.
     *
     * <p>평균 75,000에 산 20주 중 10주를 90,000에 판다 → (90,000-75,000) x 10 = 150,000.
     */
    @Test
    void sellingRealizesProfitAndClearsCostWhenFlat() {
        buy(KR, 0, 70_000, 10, 1);
        buy(KR, 0, 80_000, 10, 2);
        sell(KR, 0, 90_000, 10, 3);

        Portfolio.Snapshot s = portfolio.of(ACC, KR, 0, SEED);
        assertThat(s.qty()).isEqualTo(10);
        assertThat(s.realized()).isEqualTo(150_000);
        assertThat(s.avgCost()).isEqualTo(75_000); /* 남은 것의 평균은 그대로 */
        assertThat(s.cash()).isEqualTo(SEED - 1_500_000 + 900_000);

        /* 나머지도 판다 — 보유도 원가도 0 */
        sell(KR, 0, 60_000, 10, 4);
        Portfolio.Snapshot flat = portfolio.of(ACC, KR, 0, SEED);
        assertThat(flat.qty()).isZero();
        assertThat(flat.cost()).isZero();
        /* 두 번째는 주당 15,000 손해라 이익과 정확히 상쇄된다 */
        assertThat(flat.realized()).isZero();
        /* 다 팔았으니 총 자산은 현금뿐이고, 본전으로 돌아왔다 */
        assertThat(flat.cash()).isEqualTo(SEED);
        assertThat(flat.equity(70_000)).isEqualTo(SEED);
    }

    /** 평가 손익과 수익률은 지금 시세로 친다. */
    @Test
    void unrealizedAndReturnUseCurrentPrice() {
        buy(KR, 0, 70_000, 100, 1); /* 700만 원어치 */

        Portfolio.Snapshot s = portfolio.of(ACC, KR, 0, SEED);
        assertThat(s.unrealized(77_000)).isEqualTo(700_000); /* 주당 7,000 이익 */
        assertThat(s.equity(77_000)).isEqualTo(SEED + 700_000);
        assertThat(s.returnRate(77_000, SEED)).isCloseTo(0.007, within(1e-9));

        /* 시세를 모르면(0) 평가 손익은 0이다 — 0원으로 치지 않는다 */
        assertThat(s.unrealized(0)).isZero();
    }

    /**
     * <b>통화를 섞지 않는다.</b> 국내는 원, 미국은 센트라 그냥 더하면
     * 1억 원과 100만 달러가 같은 수가 된다.
     */
    @Test
    void currenciesDoNotMix() {
        buy(KR, 0, 70_000, 10, 1);     /* 국내 */
        buy("AAPL", 1, 25_500, 10, 2); /* 미국 */

        Portfolio.Snapshot kr = portfolio.of(ACC, KR, 0, SEED);
        assertThat(kr.qty()).isEqualTo(10);
        assertThat(kr.cash()).isEqualTo(SEED - 700_000); /* 미국 매수가 섞이지 않았다 */

        Portfolio.Snapshot us = portfolio.of(ACC, "AAPL", 1, 10_000_000);
        assertThat(us.qty()).isEqualTo(10);
        assertThat(us.cash()).isEqualTo(10_000_000 - 255_000);
    }

    /** 같은 체결이 두 번 들어와도 한 번만 센다 — 주문 응답과 주기 작업이 같은 것을 본다. */
    @Test
    void duplicateFillIsIgnored() {
        assertThat(fills.record(ACC, KR, 0, 0, 0, 70_000, 10, 100, 1)).isTrue();
        assertThat(fills.record(ACC, KR, 0, 0, 0, 70_000, 10, 100, 1)).isFalse();

        Portfolio.Snapshot s = portfolio.of(ACC, KR, 0, SEED);
        assertThat(s.qty()).isEqualTo(10);
        assertThat(s.fills()).isEqualTo(1);
    }

    /** 기록은 파일에 남는다 — 채널계를 다시 띄워도 어제 거래가 그대로 있다. */
    @Test
    void historySurvivesRestart() {
        buy(KR, 0, 70_000, 10, 1);

        Db again = new Db(dir.resolve("minisor.db").toString());
        Portfolio reopened = new Portfolio(new FillStore(again));

        Portfolio.Snapshot s = reopened.of(ACC, KR, 0, SEED);
        assertThat(s.qty()).isEqualTo(10);
        assertThat(s.cash()).isEqualTo(SEED - 700_000);
    }
}
