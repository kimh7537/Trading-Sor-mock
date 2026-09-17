package com.minisor.channel.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.ledger.FakeLedger;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.net.http.WebSocket;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

/**
 * T4-04 완료 조건을 그대로 옮긴다.
 *
 * <p>상태 코드가 이 태스크의 핵심이다 — <b>전부 500으로 뭉개면 부르는 쪽이
 * 무엇을 해야 할지 알 수 없다.</b>
 *
 * <p>HTTP는 JDK 내장 {@link HttpClient}로 부른다(T4-01과 같은 이유).
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class OrderApiTest {

    /** 테스트 전체가 쓰는 상대역. 포트를 설정에 넣어야 해서 static이다. */
    private static FakeLedger ledger;

    @DynamicPropertySource
    static void ledgerProps(DynamicPropertyRegistry reg) throws Exception {
        ledger = new FakeLedger();
        reg.add("minisor.ledger.port", ledger::port);
        reg.add("minisor.ledger.read-timeout-ms", () -> 500);
        /* 주기 작업이 끼어들면 방송 개수를 세는 시험이 흔들린다. 주기 작업은 LedgerPollerTest가 본다 */
        reg.add("minisor.poller.enabled", () -> false);
    }

    @LocalServerPort private int port;

    @Autowired private OrderService service; // 컨텍스트가 떴는지 확인용

    @Autowired private com.minisor.channel.stream.StreamHub hub;

    @AfterEach
    void reset() {
        ledger.setSilent(false);
        ledger.setDelayMs(0);
        ledger.setFillQty(0);
    }

    private HttpResponse<String> get(String path) throws Exception {
        HttpClient c = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();
        HttpRequest r =
                HttpRequest.newBuilder()
                        .uri(URI.create("http://127.0.0.1:" + port + path))
                        .timeout(Duration.ofSeconds(10))
                        .GET()
                        .build();
        return c.send(r, HttpResponse.BodyHandlers.ofString());
    }

    private HttpResponse<String> post(String json) throws Exception {
        HttpClient c = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();
        HttpRequest r =
                HttpRequest.newBuilder()
                        .uri(URI.create("http://127.0.0.1:" + port + "/api/orders"))
                        .timeout(Duration.ofSeconds(10))
                        .header("Content-Type", "application/json")
                        .POST(HttpRequest.BodyPublishers.ofString(json))
                        .build();
        return c.send(r, HttpResponse.BodyHandlers.ofString());
    }

    /**
     * KRX 지정가 매수. 값은 {@code WireEnums}를 따른다 — 매수=0, 지정가=0, KRX=0.
     *
     * <p>T6-01 전에는 여기서 1,1,1을 보냈고 주석 없이 "매수"라고 믿었다. C는
     * 그것을 NXT 시장가 매도로 읽었다.
     */
    private static String order(long clOrdId) {
        return """
               {"account":"123456789012","symbol":"005930","clOrdId":%d,
                "side":0,"type":0,"market":0,"price":70000,"qty":10}
               """
                .formatted(clOrdId);
    }

    /**
     * <b>옛 번호 체계(1부터)로 온 값은 경계에서 막는다.</b>
     *
     * <p>매도는 이제 1이고 2는 없는 값이다. 여기서 막지 않으면 원장이 모르는
     * 방향을 받는다.
     */
    @Test
    void outOfRangeEnumReturns400() throws Exception {
        int before = ledger.requests();

        assertThat(post(order(16).replace("\"side\":0", "\"side\":2")).statusCode())
                .isEqualTo(400);
        assertThat(post(order(17).replace("\"market\":0", "\"market\":2")).statusCode())
                .isEqualTo(400);
        assertThat(post(order(18).replace("\"type\":0", "\"type\":5")).statusCode())
                .isEqualTo(400);

        assertThat(ledger.requests()).isEqualTo(before);
    }

    /** 자동(SOR) 시장값 255는 받는다. 원장이 시장을 정한다(T6-03). */
    @Test
    void autoMarketIsAccepted() throws Exception {
        HttpResponse<String> res = post(order(19).replace("\"market\":0", "\"market\":255"));
        assertThat(res.statusCode()).isEqualTo(200);
    }

    /**
     * T6-04 — <b>원장의 호가를 그대로 돌려준다.</b> 빈 단(가격 0)은 싣지 않고, 시장 값은
     * 경계에서 거른다.
     */
    @Test
    void bookComesFromLedger() throws Exception {
        HttpResponse<String> res = get("/api/book?market=1");
        assertThat(res.statusCode()).isEqualTo(200);
        assertThat(res.body())
                .contains("\"market\":1")
                .contains("{\"price\":70000,\"qty\":110}")
                .contains("{\"price\":69800,\"qty\":112}")
                .contains("{\"price\":70200,\"qty\":21}")
                .doesNotContain("\"price\":0");

        int before = ledger.requests();
        assertThat(get("/api/book?market=2").statusCode()).isEqualTo(400);
        assertThat(get("/api/book?market=255").statusCode()).isEqualTo(400);
        assertThat(ledger.requests()).isEqualTo(before);
    }

    /** 받은 것을 모으는 구독자. */
    private static final class Sink implements WebSocket.Listener {
        final List<String> got = new CopyOnWriteArrayList<>();

        @Override
        public CompletionStage<?> onText(WebSocket ws, CharSequence data, boolean last) {
            got.add(data.toString());
            ws.request(1);
            return null;
        }
    }

    private static void waitFor(List<String> got, int n) throws InterruptedException {
        for (int i = 0; i < 250 && got.size() < n; i++) {
            Thread.sleep(20);
        }
    }

    /**
     * T6-04 — <b>주문이 처리되면 결과와 체결을 방송한다.</b> 예전엔 {@code broadcast()}를
     * 부르는 곳이 없었다.
     */
    @Test
    void orderAndFillAreBroadcast() throws Exception {
        Sink sink = new Sink();
        WebSocket ws =
                HttpClient.newHttpClient()
                        .newWebSocketBuilder()
                        .buildAsync(URI.create("ws://127.0.0.1:" + port + "/ws/stream"), sink)
                        .get(5, TimeUnit.SECONDS);
        for (int i = 0; i < 100 && hub.subscriberCount() == 0; i++) {
            Thread.sleep(20);
        }

        /* 앞 테스트가 원장을 "응답 없음"으로 만들었을 수 있다. 그 회복 알림을 먼저 흘려보낸다 */
        assertThat(get("/api/book?market=0").statusCode()).isEqualTo(200);
        Thread.sleep(200);
        sink.got.clear();

        ledger.setFillQty(4);
        assertThat(post(order(20)).statusCode()).isEqualTo(200);
        waitFor(sink.got, 2);
        assertThat(sink.got).hasSize(2);
        assertThat(sink.got.get(0)).contains("\"kind\":\"order\"").contains("ACCEPTED");
        assertThat(sink.got.get(1))
                .contains("\"kind\":\"fill\"")
                .contains("\"qty\":4")
                .contains("\"price\":70000");

        /* 체결이 없으면 결과만 간다 */
        ledger.setFillQty(0);
        assertThat(post(order(21)).statusCode()).isEqualTo(200);
        waitFor(sink.got, 3);
        Thread.sleep(100);
        assertThat(sink.got).hasSize(3);
        assertThat(sink.got.get(2)).contains("\"kind\":\"order\"");

        ws.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }

    /**
     * T6-10 — 원장이 답하지 않으면 {@code ledger-down}, 다시 답하면 {@code ledger-up}을
     * 방송한다. 화면의 "원장 끊김" 표시가 켜지고 꺼진다.
     */
    @Test
    void ledgerDownAndUpAreBroadcast() throws Exception {
        Sink sink = new Sink();
        WebSocket ws =
                HttpClient.newHttpClient()
                        .newWebSocketBuilder()
                        .buildAsync(URI.create("ws://127.0.0.1:" + port + "/ws/stream"), sink)
                        .get(5, TimeUnit.SECONDS);
        for (int i = 0; i < 100 && hub.subscriberCount() == 0; i++) {
            Thread.sleep(20);
        }
        assertThat(get("/api/book?market=0").statusCode()).isEqualTo(200);
        Thread.sleep(200);
        sink.got.clear();

        ledger.setSilent(true);
        assertThat(post(order(22)).statusCode()).isEqualTo(202);
        ledger.setSilent(false);
        assertThat(post(order(23)).statusCode()).isEqualTo(200);

        for (int i = 0; i < 250 && sink.got.stream().noneMatch(m -> m.contains("ledger-up")); i++) {
            Thread.sleep(20);
        }
        List<String> status =
                sink.got.stream().filter(m -> m.contains("\"kind\":\"ledger-")).toList();
        assertThat(status).hasSize(2);
        assertThat(status.get(0)).contains("ledger-down");
        assertThat(status.get(1)).contains("ledger-up");

        ws.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }

    /** T7-03 — 조회(게이트웨이 경유)가 원장 응답을 못 받아도 {@code ledger-down}을 방송한다. */
    @Test
    void readFailureIsBroadcast() throws Exception {
        Sink sink = new Sink();
        WebSocket ws =
                HttpClient.newHttpClient()
                        .newWebSocketBuilder()
                        .buildAsync(URI.create("ws://127.0.0.1:" + port + "/ws/stream"), sink)
                        .get(5, TimeUnit.SECONDS);
        for (int i = 0; i < 100 && hub.subscriberCount() == 0; i++) {
            Thread.sleep(20);
        }
        assertThat(get("/api/book?market=0").statusCode()).isEqualTo(200);
        Thread.sleep(200);
        sink.got.clear();

        ledger.setSilent(true);
        try {
            assertThat(get("/api/balance").statusCode()).isEqualTo(503);
        } finally {
            ledger.setSilent(false);
        }
        for (int i = 0; i < 100 && sink.got.stream().noneMatch(m -> m.contains("ledger-down")); i++) {
            Thread.sleep(20);
        }
        assertThat(sink.got).anyMatch(m -> m.contains("\"kind\":\"ledger-down\""));

        assertThat(get("/api/balance").statusCode()).isEqualTo(200);
        ws.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }

    private HttpResponse<String> delete(String path) throws Exception {
        HttpClient c = HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();
        HttpRequest r =
                HttpRequest.newBuilder()
                        .uri(URI.create("http://127.0.0.1:" + port + path))
                        .timeout(Duration.ofSeconds(10))
                        .DELETE()
                        .build();
        return c.send(r, HttpResponse.BodyHandlers.ofString());
    }

    /**
     * T7-03 — 접수된 주문은 <b>목록에 남고, 하나씩 원장에서 다시 읽힌다.</b> 시장별 몫(legs)이 실린다.
     */
    @Test
    void ordersAreListedAndReadable() throws Exception {
        assertThat(post(order(30)).statusCode()).isEqualTo(200);

        HttpResponse<String> list = get("/api/orders");
        assertThat(list.statusCode()).isEqualTo(200);
        assertThat(list.body()).contains("\"orderId\":100030").contains("\"working\":10");

        HttpResponse<String> one = get("/api/orders/100030");
        assertThat(one.statusCode()).isEqualTo(200);
        assertThat(one.body())
                .contains("\"clOrdId\":30")
                .contains("\"legs\":[{\"market\":0,\"sent\":10")
                .contains("\"done\":false");

        assertThat(get("/api/orders/424242").statusCode()).isEqualTo(404);
    }

    /**
     * T7-03 — 취소: 잔량이 있으면 200, <b>같은 주문을 다시 취소하면 409</b>(끝났다), 모르는 주문은 404.
     */
    @Test
    void cancelFlow() throws Exception {
        assertThat(post(order(31)).statusCode()).isEqualTo(200);

        HttpResponse<String> first = delete("/api/orders/100031");
        assertThat(first.statusCode()).isEqualTo(200);
        assertThat(first.body()).contains("\"canceledQty\":10").contains("\"done\":true");

        assertThat(delete("/api/orders/100031").statusCode()).isEqualTo(409);
        assertThat(delete("/api/orders/424243").statusCode()).isEqualTo(404);

        /* 목록도 취소를 반영한다 — 주기 작업을 기다리지 않는다 */
        assertThat(get("/api/orders").body())
                .contains("\"orderId\":100031,\"clOrdId\":31")
                .containsPattern("\"orderId\":100031,[^}]*\"canceled\":10,[^}]*\"done\":true");
        assertThat(get("/api/orders/100031").body()).contains("\"canceled\":10");
    }

    /** T7-03 — 잔고: 예수금, 묶인 금액, 주문 가능 금액. */
    @Test
    void balanceComesFromLedger() throws Exception {
        ledger.setBalance(100_000_000L, 700_000L);
        HttpResponse<String> res = get("/api/balance");
        assertThat(res.statusCode()).isEqualTo(200);
        assertThat(res.body())
                .contains("\"account\":\"123456789012\"")
                .contains("\"cash\":100000000")
                .contains("\"reserved\":700000")
                .contains("\"available\":99300000");
        ledger.setBalance(100_000_000L, 0);
    }

    /**
     * T7-03 — SOR 자동 주문의 체결 알림에는 <b>실제로 체결된 시장</b>이 실린다(255가 아니라).
     * 예전엔 고른 값(255)을 그대로 실어 화면이 "SOR"이라고만 보였다.
     */
    @Test
    void sorFillCarriesActualMarket() throws Exception {
        Sink sink = new Sink();
        WebSocket ws =
                HttpClient.newHttpClient()
                        .newWebSocketBuilder()
                        .buildAsync(URI.create("ws://127.0.0.1:" + port + "/ws/stream"), sink)
                        .get(5, TimeUnit.SECONDS);
        for (int i = 0; i < 100 && hub.subscriberCount() == 0; i++) {
            Thread.sleep(20);
        }
        assertThat(get("/api/book?market=0").statusCode()).isEqualTo(200);
        Thread.sleep(200);
        sink.got.clear();

        ledger.setFillQty(3);
        assertThat(post(order(32).replace("\"market\":0", "\"market\":255")).statusCode())
                .isEqualTo(200);
        waitFor(sink.got, 2);
        List<String> fills = sink.got.stream().filter(m -> m.contains("\"kind\":\"fill\"")).toList();
        assertThat(fills).hasSize(1);
        assertThat(fills.get(0)).contains("\"market\":1").contains("\"qty\":3");

        ws.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }

    @Test
    void acceptedOrderReturns200() throws Exception {
        assertThat(service).isNotNull();

        HttpResponse<String> res = post(order(11));

        assertThat(res.statusCode()).isEqualTo(200);
        assertThat(res.body()).contains("ACCEPTED");
        assertThat(res.body()).contains("100011"); // 상대역이 매긴 주문번호
    }

    /**
     * 형식이 틀리면 <b>원장까지 가지 않고</b> 400이다. 고쳐서 다시 보내면 된다.
     */
    @Test
    void malformedOrderReturns400() throws Exception {
        int before = ledger.requests();

        String bad = order(12).replace("\"123456789012\"", "\"12\"");
        HttpResponse<String> res = post(bad);

        assertThat(res.statusCode()).isEqualTo(400);
        /* **원장에 가지 않았다.** 그것이 경계에서 검증하는 이유다. */
        assertThat(ledger.requests()).isEqualTo(before);
    }

    /**
     * 응답이 안 오면 <b>200도 5xx도 아니다.</b> 모른다고 답한다 —
     * 다시 보내면 중복 주문이 될 수 있다.
     */
    @Test
    void noAnswerReturns202InDoubt() throws Exception {
        ledger.setSilent(true);

        HttpResponse<String> res = post(order(13));

        assertThat(res.statusCode()).isEqualTo(202);
        assertThat(res.body()).contains("IN_DOUBT");
        assertThat(res.body()).contains("조회");
    }

    /**
     * 답을 못 받은 뒤에도 <b>다음 주문은 멀쩡히 나간다.</b> 깨진 접속을
     * 버렸다는 뜻이다 — 되돌려 썼다면 응답이 한 칸씩 밀렸을 것이다.
     */
    @Test
    void recoversAfterInDoubt() throws Exception {
        ledger.setSilent(true);
        assertThat(post(order(14)).statusCode()).isEqualTo(202);

        ledger.setSilent(false);
        HttpResponse<String> res = post(order(15));

        assertThat(res.statusCode()).isEqualTo(200);
        assertThat(res.body()).contains("100015"); // 15번의 답이다. 14번이 아니다
    }
}
