package com.minisor.channel.feed;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * 캔들 조회(Phase 8). 진짜 토스에 붙지 않는다 — 같은 규약으로 답하는 작은 HTTP 서버를 세운다.
 *
 * <p>확인하는 것은 셋이다. <b>최신순으로 오는 것을 오래된 것부터로 뒤집는가</b>(화면은 왼쪽이
 * 과거다), <b>읽지 못하는 봉을 버리는가</b>(추측한 값을 차트에 올리지 않는다),
 * <b>캐시가 같은 답을 돌려주는가</b>(차트 조회에는 별도 레이트리밋이 있다).
 */
class TossCandlesTest {

    private HttpServer server;
    private final AtomicInteger calls = new AtomicInteger();
    private final AtomicReference<String> lastQuery = new AtomicReference<>("");
    private volatile int status = 200;
    private volatile String body = "";

    @BeforeEach
    void up() throws IOException {
        server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext(
                "/api/v1/candles",
                (HttpExchange ex) -> {
                    calls.incrementAndGet();
                    lastQuery.set(ex.getRequestURI().getQuery());
                    byte[] out = body.getBytes(StandardCharsets.UTF_8);
                    ex.sendResponseHeaders(status, out.length);
                    ex.getResponseBody().write(out);
                    ex.close();
                });
        server.createContext(
                "/oauth2/token",
                (HttpExchange ex) -> {
                    byte[] out =
                            "{\"access_token\":\"tok\",\"expires_in\":3600}"
                                    .getBytes(StandardCharsets.UTF_8);
                    ex.sendResponseHeaders(200, out.length);
                    ex.getResponseBody().write(out);
                    ex.close();
                });
        server.start();
    }

    @AfterEach
    void down() {
        server.stop(0);
    }

    private TossCandles candles() {
        FeedProperties p =
                new FeedProperties(
                        true,
                        0,
                        "005930",
                        "wss://example.invalid/ws",
                        "http://127.0.0.1:" + server.getAddress().getPort(),
                        "id",
                        "secret",
                        "",
                        "",
                        1);
        return new TossCandles(p);
    }

    private static String candle(String ts, String o, String h, String l, String c, String v) {
        return "{\"timestamp\":\""
                + ts
                + "\",\"openPrice\":\""
                + o
                + "\",\"highPrice\":\""
                + h
                + "\",\"lowPrice\":\""
                + l
                + "\",\"closePrice\":\""
                + c
                + "\",\"volume\":\""
                + v
                + "\",\"currency\":\"KRW\"}";
    }

    /** 토스는 최신순으로 준다. 화면은 왼쪽이 과거이므로 <b>뒤집어</b> 돌려준다. */
    @Test
    void reversesToOldestFirst() throws Exception {
        body =
                "{\"result\":{\"candles\":["
                        + candle("2026-09-18T00:00:00+09:00", "259500", "262000", "257500", "260000", "24067181")
                        + ","
                        + candle("2026-09-17T00:00:00+09:00", "251500", "259000", "251000", "256000", "16971358")
                        + "]}}";

        TossCandles.Chart chart = candles().fetch("1d", 2);

        assertThat(chart.candles()).hasSize(2);
        assertThat(chart.candles().get(0).close()).isEqualTo(256000); // 9/17이 먼저
        assertThat(chart.candles().get(1).close()).isEqualTo(260000); // 9/18이 나중
        assertThat(chart.candles().get(1).volume()).isEqualTo(24067181L);
        assertThat(lastQuery.get()).contains("symbol=005930").contains("interval=1d");
    }

    /** 문자열 decimal이 정수가 된다 — 부동소수를 쓰지 않는다. */
    @Test
    void parsesStringDecimals() throws Exception {
        body =
                "{\"result\":{\"candles\":["
                        + candle("2026-09-18T09:01:00+09:00", "260000.0", "260500", "259500", "260000", "70029")
                        + "]}}";

        TossCandles.Candle c = candles().fetch("1m", 1).candles().get(0);
        assertThat(c.open()).isEqualTo(260000);
        assertThat(c.high()).isEqualTo(260500);
        assertThat(c.low()).isEqualTo(259500);
        assertThat(c.volume()).isEqualTo(70029L);
    }

    /** 읽지 못하는 봉은 버린다. 추측한 값을 차트에 올리지 않는다. */
    @Test
    void dropsUnreadableCandles() throws Exception {
        body =
                "{\"result\":{\"candles\":["
                        + candle("2026-09-18T00:00:00+09:00", "259500", "262000", "257500", "260000", "1")
                        + ","
                        + candle("모르는-시각", "1", "2", "3", "4", "5")
                        + ","
                        + candle("2026-09-16T00:00:00+09:00", "0", "0", "0", "0", "0")
                        + "]}}";

        assertThat(candles().fetch("1d", 3).candles()).hasSize(1);
    }

    /** 차트 조회에는 별도 레이트리밋이 있다. 같은 요청은 캐시가 답한다. */
    @Test
    void cachesWithinTtl() throws Exception {
        body =
                "{\"result\":{\"candles\":["
                        + candle("2026-09-18T00:00:00+09:00", "1000", "1000", "1000", "1000", "1")
                        + "]}}";

        TossCandles c = candles();
        c.fetch("1d", 1);
        c.fetch("1d", 1);
        assertThat(calls.get()).isEqualTo(1);

        /* 다른 요청은 따로 받는다 */
        c.fetch("1m", 1);
        assertThat(calls.get()).isEqualTo(2);
    }

    /** 실패는 삼키지 않는다. 본문을 실어 올린다 — 왜 안 오는지 밖에서 보여야 한다. */
    @Test
    void surfacesFailure() {
        status = 500;
        body = "{\"error\":\"boom\"}";
        assertThatThrownBy(() -> candles().fetch("1d", 1))
                .isInstanceOf(IOException.class)
                .hasMessageContaining("500")
                .hasMessageContaining("boom");
    }
}
