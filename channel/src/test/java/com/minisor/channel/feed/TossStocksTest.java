package com.minisor.channel.feed;

import static org.assertj.core.api.Assertions.assertThat;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * 종목 찾기(T8-10). 진짜 토스에 붙지 않는다 — 같은 규약으로 답하는 작은 HTTP 서버를 세운다.
 *
 * <p>확인하는 것은 셋이다. <b>순서</b>(정확히 맞는 것이 먼저다), <b>목록을 한 번만 받는가</b>
 * (전체 종목 조회는 레이트리밋이 짜다), <b>429를 기다렸다 다시 부르는가</b>.
 */
class TossStocksTest {

    private HttpServer server;
    private final AtomicInteger listCalls = new AtomicInteger();
    private volatile int firstStatus = 200;

    private static final String KOSPI =
            "{\"result\":["
                    + "{\"symbol\":\"005380\",\"name\":\"현대차\"},"
                    + "{\"symbol\":\"001500\",\"name\":\"현대차증권\"},"
                    + "{\"symbol\":\"005930\",\"name\":\"삼성전자\"}]}";
    private static final String KOSDAQ =
            "{\"result\":[{\"symbol\":\"247540\",\"name\":\"에코프로비엠\"}]}";

    @BeforeEach
    void up() throws IOException {
        server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext(
                "/api/v1/stocks/all",
                (HttpExchange ex) -> {
                    int n = listCalls.incrementAndGet();
                    String q = ex.getRequestURI().getQuery();
                    if (n == 1 && firstStatus != 200) {
                        ex.getResponseHeaders().add("Retry-After", "0");
                        ex.sendResponseHeaders(firstStatus, -1);
                        ex.close();
                        return;
                    }
                    byte[] out =
                            (q != null && q.contains("KOSDAQ") ? KOSDAQ : KOSPI)
                                    .getBytes(StandardCharsets.UTF_8);
                    ex.sendResponseHeaders(200, out.length);
                    ex.getResponseBody().write(out);
                    ex.close();
                });
        server.createContext(
                "/api/v1/prices",
                (HttpExchange ex) -> {
                    byte[] out =
                            "{\"result\":[{\"symbol\":\"005380\",\"lastPrice\":\"238500\"}]}"
                                    .getBytes(StandardCharsets.UTF_8);
                    ex.sendResponseHeaders(200, out.length);
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

    private TossStocks stocks() {
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
        return new TossStocks(p, new TossTokenSource(p));
    }

    @Test
    void 정확히_맞는_이름이_먼저다() throws Exception {
        List<TossStocks.Stock> hits = stocks().search("현대차");

        assertThat(hits).hasSize(2);
        assertThat(hits.get(0).name()).isEqualTo("현대차");
        assertThat(hits.get(1).name()).isEqualTo("현대차증권");
    }

    @Test
    void 코드로도_찾는다() throws Exception {
        assertThat(stocks().search("00593"))
                .singleElement()
                .satisfies(
                        s -> {
                            assertThat(s.symbol()).isEqualTo("005930");
                            assertThat(s.name()).isEqualTo("삼성전자");
                        });
    }

    @Test
    void 코스닥도_함께_본다() throws Exception {
        assertThat(stocks().search("에코프로"))
                .singleElement()
                .satisfies(s -> assertThat(s.market()).isEqualTo("KOSDAQ"));
    }

    @Test
    void 목록은_한_번만_받는다() throws Exception {
        TossStocks s = stocks();
        s.search("삼성");
        s.search("현대");
        s.find("005930");

        /* 마켓 둘(KOSPI·KOSDAQ)뿐 — 검색마다 다시 받지 않는다 */
        assertThat(listCalls.get()).isEqualTo(2);
    }

    @Test
    void 없는_종목은_null이다() throws Exception {
        assertThat(stocks().find("999999")).isNull();
    }

    @Test
    void 레이트리밋이면_기다렸다_다시_부른다() throws Exception {
        firstStatus = 429;

        assertThat(stocks().search("삼성전자")).hasSize(1);
        assertThat(listCalls.get()).isEqualTo(3); // 429 한 번 + KOSPI + KOSDAQ
    }

    @Test
    void 현재가는_문자열_십진수로_온다() throws Exception {
        assertThat(stocks().price("005380")).isEqualTo(238500);
        assertThat(stocks().price("000000")).isZero();
    }
}
