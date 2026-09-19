package com.minisor.channel.feed;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.wire.BookAck;
import java.util.List;
import org.junit.jupiter.api.Test;

/** 시뮬 봉 만들기 (T8-09). */
class SimCandlesTest {

    private static BookAck ack(int lastPrice, long tradedQty) {
        BookAck a = new BookAck();
        a.symbol = "005930";
        a.lastPrice = lastPrice;
        a.tradedQty = tradedQty;
        return a;
    }

    @Test
    void 체결이_없으면_봉도_없다() {
        SimCandles sim = new SimCandles();
        sim.sample(ack(0, 0), ack(0, 0));
        sim.sample(null, null);

        assertThat(sim.candles(10)).isEmpty();
    }

    @Test
    void 같은_분의_표본은_한_봉에_묶인다() {
        SimCandles sim = new SimCandles();
        sim.sample(ack(70_000, 10), null); // 첫 표본은 기준만 잡는다
        sim.sample(ack(71_000, 30), null);
        sim.sample(ack(69_500, 45), null);
        sim.sample(ack(70_200, 45), null);

        List<SimCandles.Candle> bars = sim.candles(10);
        assertThat(bars).hasSize(1);
        SimCandles.Candle b = bars.get(0);
        assertThat(b.open()).isEqualTo(70_000);
        assertThat(b.high()).isEqualTo(71_000);
        assertThat(b.low()).isEqualTo(69_500);
        assertThat(b.close()).isEqualTo(70_200);
        /* 첫 표본 이전의 10주는 세지 않는다. 그 뒤로 20 + 15 + 0 */
        assertThat(b.volume()).isEqualTo(35);
    }

    @Test
    void 두_시장의_거래량을_더한다() {
        SimCandles sim = new SimCandles();
        sim.sample(ack(70_000, 100), ack(70_000, 200));
        sim.sample(ack(70_100, 140), ack(70_000, 260));

        assertThat(sim.candles(10).get(0).volume()).isEqualTo(100);
    }

    @Test
    void 체결이_많은_시장의_가격을_쓴다() {
        SimCandles sim = new SimCandles();
        sim.sample(ack(70_000, 10), ack(80_000, 5));
        sim.sample(ack(70_500, 10), ack(80_000, 999));

        assertThat(sim.candles(10).get(0).close()).isEqualTo(80_000);
    }

    @Test
    void 원장이_다시_뜨면_거래량이_튀지_않는다() {
        SimCandles sim = new SimCandles();
        sim.sample(ack(70_000, 5_000), null);
        sim.sample(ack(70_000, 5_100), null);
        /* 원장 재시작: 누적값이 0으로 돌아간다 */
        sim.sample(ack(70_000, 20), null);

        assertThat(sim.candles(10).get(0).volume()).isEqualTo(100);
    }

    @Test
    void 초기화하면_처음부터_다시_센다() {
        SimCandles sim = new SimCandles();
        sim.sample(ack(70_000, 10), null);
        sim.sample(ack(70_000, 90), null);
        sim.reset();

        assertThat(sim.candles(10)).isEmpty();

        sim.sample(ack(71_000, 500), null);
        sim.sample(ack(71_000, 505), null);
        assertThat(sim.candles(10).get(0).volume()).isEqualTo(5);
    }

    @Test
    void 최근_것만_요청한_만큼_준다() {
        SimCandles sim = new SimCandles();
        sim.sample(ack(70_000, 0), null);
        assertThat(sim.candles(1)).hasSize(1);
        assertThat(sim.candles(50)).hasSize(1);
    }
}
