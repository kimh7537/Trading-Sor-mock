package com.minisor.channel.feed;

import java.io.IOException;
import java.math.BigDecimal;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;

/**
 * 토스증권의 캔들(OHLCV) 차트 조회.
 *
 * <h2>이것은 바깥 시세다</h2>
 *
 * 봉은 <b>실제 시장에서 일어난 체결</b>을 집계한 것이고, 이 프로젝트의 원장 호가창과는 별개다.
 * 시뮬 모드에서 가상 참가자가 만든 체결은 여기 섞이지 않는다 — 섞으면 "실제 시장이 이렇게
 * 움직였다"와 "내 시뮬이 이렇게 움직였다"를 구분할 수 없게 된다. 화면에도 그렇게 적는다.
 *
 * <h2>왜 캐시하나</h2>
 *
 * 차트 조회에는 별도 레이트리밋(`MARKET_DATA_CHART`)이 걸려 있다. 화면이 새로고침될 때마다
 * 부르면 금세 429다. 1분봉은 20초, 일봉은 5분 동안 같은 답을 돌려준다 — 봉이 그보다 자주
 * 바뀌지 않는다.
 */
@Service
public class TossCandles {

    private static final Logger log = LoggerFactory.getLogger(TossCandles.class);
    private static final JsonMapper JSON = JsonMapper.builder().build();

    /** 봉 하나. 가격·거래량은 정수다 — 부동소수를 쓰지 않는다(CLAUDE.md). */
    public record Candle(long t, int open, int high, int low, int close, long volume) {}

    public record Chart(String symbol, String interval, List<Candle> candles, long fetchedAt) {}

    private record Cached(Chart chart, long until) {}

    private final FeedProperties props;
    private final TossTokenSource tokens;
    private final HttpClient http =
            HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();
    private final Map<String, Cached> cache = new ConcurrentHashMap<>();

    @Autowired
    public TossCandles(FeedProperties props) {
        this(props, new TossTokenSource(props));
    }

    TossCandles(FeedProperties props, TossTokenSource tokens) {
        this.props = props;
        this.tokens = tokens;
    }

    public boolean usable() {
        return props.usable();
    }

    /** 얼마나 캐시할 것인가. 1분봉은 20초, 일봉은 5분. */
    private static long ttlMs(String interval) {
        return "1d".equals(interval) ? 300_000 : 20_000;
    }

    /**
     * 봉을 가져온다. 캐시가 살아 있으면 그것을 준다.
     *
     * <p>토스가 최신순으로 주는 것을 <b>오래된 것부터</b>로 뒤집어 돌려준다 — 화면은 왼쪽이
     * 과거다. 뒤집는 자리를 한 곳으로 모아 두면 화면이 순서를 다시 고민하지 않는다.
     */
    public Chart fetch(String interval, int count) throws IOException, InterruptedException {
        String key = interval + ":" + count;
        Cached c = cache.get(key);
        long now = System.currentTimeMillis();
        if (c != null && now < c.until()) {
            return c.chart();
        }

        String token = tokens.token();
        URI uri =
                URI.create(
                        props.baseUrl()
                                + "/api/v1/candles?symbol="
                                + props.symbol()
                                + "&interval="
                                + interval
                                + "&count="
                                + count);
        HttpRequest req =
                HttpRequest.newBuilder(uri)
                        .header("Authorization", "Bearer " + token)
                        .header("Accept", "application/json")
                        .timeout(Duration.ofSeconds(10))
                        .GET()
                        .build();

        HttpResponse<byte[]> res = http.send(req, HttpResponse.BodyHandlers.ofByteArray());
        String body = TossTokenSource.text(res);
        if (res.statusCode() == 429) {
            throw new TossTokenSource.FeedBackoff(
                    TossTokenSource.retryAfterMs(res), "캔들 조회 429");
        }
        if (res.statusCode() / 100 != 2) {
            /* 토큰이 죽었을 수 있다. 다음 호출이 새로 받게 한다 */
            tokens.invalidate();
            throw new IOException("캔들 조회 실패 " + res.statusCode() + ": " + body);
        }

        JsonNode arr = JSON.readTree(body).path("result").path("candles");
        List<Candle> out = new ArrayList<>();
        for (JsonNode n : arr) {
            Candle one = one(n);
            if (one != null) {
                out.add(one);
            }
        }
        /* 최신순으로 오므로 뒤집는다 — 화면은 왼쪽이 과거다 */
        java.util.Collections.reverse(out);

        Chart chart = new Chart(props.symbol(), interval, List.copyOf(out), now);
        cache.put(key, new Cached(chart, now + ttlMs(interval)));
        log.debug("캔들 {} {}개", interval, out.size());
        return chart;
    }

    /** 읽지 못하는 봉은 버린다 — 추측한 값을 차트에 올리지 않는다. */
    private static Candle one(JsonNode n) {
        int o = intOf(n, "openPrice");
        int h = intOf(n, "highPrice");
        int l = intOf(n, "lowPrice");
        int c = intOf(n, "closePrice");
        if (o <= 0 || h <= 0 || l <= 0 || c <= 0) {
            return null;
        }
        long t = 0;
        try {
            t = java.time.OffsetDateTime.parse(n.path("timestamp").asString()).toInstant().toEpochMilli();
        } catch (RuntimeException e) {
            return null;
        }
        return new Candle(t, o, h, l, c, longOf(n, "volume"));
    }

    private static int intOf(JsonNode n, String f) {
        try {
            return new BigDecimal(n.path(f).asString()).intValue();
        } catch (RuntimeException e) {
            return 0;
        }
    }

    private static long longOf(JsonNode n, String f) {
        try {
            return new BigDecimal(n.path(f).asString()).longValue();
        } catch (RuntimeException e) {
            return 0;
        }
    }
}
