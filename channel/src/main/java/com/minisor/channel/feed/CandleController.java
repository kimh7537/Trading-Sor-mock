package com.minisor.channel.feed;

import java.io.IOException;
import java.util.List;
import java.util.Set;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/**
 * 캔들 차트 — {@code GET /api/candles?interval=1m|1d&count=N}.
 *
 * <p>화면이 MTS처럼 1분봉·일봉을 그리는 데 쓴다. <b>바깥 시세</b>이고 이 프로젝트의 원장
 * 호가창과는 별개다 — 화면에도 그렇게 적는다.
 *
 * <p>토스 설정이 없으면 409. 조회는 아무것도 바꾸지 않으므로 다시 불러도 안전하다.
 */
@RestController
public class CandleController {

    /** 토스가 받는 값만 통과시킨다. 그 밖의 값은 400 — 바깥에 그대로 넘기지 않는다. */
    private static final Set<String> INTERVALS = Set.of("1m", "1d");

    private static final int MAX_COUNT = 200;

    private final TossCandles candles;
    private final SimCandles sim;
    private final LiveFeed live;
    private final SymbolState symbols;

    public CandleController(
            TossCandles candles, SimCandles sim, LiveFeed live, SymbolState symbols) {
        this.candles = candles;
        this.sim = sim;
        this.live = live;
        this.symbols = symbols;
    }

    /** 시뮬 봉을 화면이 아는 모양으로 옮긴다. 두 쪽이 같은 모양이라 화면은 구분하지 않는다. */
    private static List<TossCandles.Candle> toChart(List<SimCandles.Candle> bars) {
        List<TossCandles.Candle> out = new java.util.ArrayList<>(bars.size());
        for (SimCandles.Candle b : bars) {
            out.add(
                    new TossCandles.Candle(
                            b.t(), b.open(), b.high(), b.low(), b.close(), b.volume()));
        }
        return List.copyOf(out);
    }

    @GetMapping("/api/candles")
    public ResponseEntity<TossCandles.Chart> chart(
            @RequestParam(defaultValue = "1m") String interval,
            @RequestParam(defaultValue = "120") int count) {

        if (!INTERVALS.contains(interval) || count < 1 || count > MAX_COUNT) {
            return ResponseEntity.badRequest().build();
        }

        /*
         * **시뮬 모드에는 시뮬의 봉을 준다.** 토스 봉은 바깥 시장의 체결이라 시뮬 호가창과
         * 아무 상관이 없다 — 한 화면에 두면 위 차트와 아래 호가가 따로 논다.
         *
         * 시뮬에는 일봉이 없다. 하루치를 모으려면 하루를 돌려야 하고, 원장을 다시 띄우면
         * 처음부터다. 1분봉만 준다.
         */
        if (live.mode() != LiveFeed.Mode.LIVE) {
            if (!"1m".equals(interval)) {
                return ResponseEntity.status(HttpStatus.CONFLICT).build();
            }
            List<SimCandles.Candle> bars = sim.candles(count);
            if (bars.isEmpty()) {
                return ResponseEntity.status(HttpStatus.CONFLICT).build();
            }
            return ResponseEntity.ok(
                    new TossCandles.Chart(symbols.code(), "1m", toChart(bars), 0));
        }

        if (!candles.usable()) {
            /* 키가 없으면 봉을 만들 길이 없다. 화면은 이때 중간가 선으로 되돌아간다 */
            return ResponseEntity.status(HttpStatus.CONFLICT).build();
        }
        try {
            return ResponseEntity.ok(candles.fetch(symbols.code(), interval, count));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        } catch (IOException | RuntimeException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        }
    }
}
