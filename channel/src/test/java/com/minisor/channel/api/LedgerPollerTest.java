package com.minisor.channel.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.ledger.FakeLedger;
import com.minisor.channel.stream.StreamHub;
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
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

/**
 * T7-03 — 원장을 다시 읽어 <b>바뀐 것만</b> 밀어 보낸다.
 *
 * <p>주기 실행은 끄고 {@link LedgerPoller#tick()}을 직접 부른다 — 시간에 기대는 시험은 흔들린다.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class LedgerPollerTest {

    private static FakeLedger ledger;

    @DynamicPropertySource
    static void props(DynamicPropertyRegistry reg) throws Exception {
        ledger = new FakeLedger();
        reg.add("minisor.ledger.port", ledger::port);
        reg.add("minisor.ledger.read-timeout-ms", () -> 500);
        reg.add("minisor.poller.enabled", () -> false);
        reg.add("minisor.auth.users-file", () -> USERS_FILE.toString());
    }

    private static final java.nio.file.Path USERS_FILE =
            java.nio.file.Path.of(System.getProperty("java.io.tmpdir"),
                    "minisor-poller-users-" + System.nanoTime() + ".json");

    @org.junit.jupiter.api.AfterAll
    static void dropUsersFile() throws Exception {
        java.nio.file.Files.deleteIfExists(USERS_FILE);
    }

    /**
     * 로그인하고 세션 쿠키를 돌려준다(T9-04).
     *
     * <p>잔고와 내 주문은 <b>그 계좌로 붙은 접속에만</b> 간다. 그래서 WebSocket 악수에도
     * 같은 쿠키를 실어야 한다 — 실지 않으면 호가만 받고 잔고는 영영 오지 않는다.
     */
    private String logIn() throws Exception {
        String body = "{\"id\":\"poller\",\"password\":\"password1\"}";
        HttpResponse<String> res = HttpClient.newHttpClient().send(
                HttpRequest.newBuilder()
                        .uri(URI.create("http://127.0.0.1:" + port + "/api/auth/signup"))
                        .timeout(Duration.ofSeconds(10))
                        .header("Content-Type", "application/json")
                        .POST(HttpRequest.BodyPublishers.ofString(body))
                        .build(),
                HttpResponse.BodyHandlers.ofString());
        assertThat(res.statusCode()).isIn(200, 409);
        return res.headers().firstValue("set-cookie").orElseThrow().split(";")[0];
    }

    @LocalServerPort private int port;
    @Autowired private LedgerPoller poller;
    @Autowired private StreamHub hub;

    private static final class Sink implements WebSocket.Listener {
        final List<String> got = new CopyOnWriteArrayList<>();

        @Override
        public CompletionStage<?> onText(WebSocket ws, CharSequence data, boolean last) {
            got.add(data.toString());
            ws.request(1);
            return null;
        }
    }

    private static List<String> kind(Sink s, String k) {
        return s.got.stream().filter(m -> m.contains("\"kind\":\"" + k + "\"")).toList();
    }

    private static void settle() throws InterruptedException {
        Thread.sleep(250); // 방송이 구독자에게 닿을 시간
    }

    @Test
    void pushesOnlyChanges() throws Exception {
        String cookie = logIn();

        Sink sink = new Sink();
        WebSocket ws =
                HttpClient.newHttpClient()
                        .newWebSocketBuilder()
                        .header("Cookie", cookie)
                        .buildAsync(URI.create("ws://127.0.0.1:" + port + "/ws/stream"), sink)
                        .get(5, TimeUnit.SECONDS);
        for (int i = 0; i < 100 && hub.subscriberCount() == 0; i++) {
            Thread.sleep(20);
        }

        /* 걸어 두는 주문 하나 — 체결 없음 */
        String json =
                "{\"symbol\":\"005930\",\"clOrdId\":50,"
                        + "\"side\":0,\"type\":0,\"market\":1,\"price\":70000,\"qty\":10}";
        HttpResponse<String> res =
                HttpClient.newHttpClient()
                        .send(
                                HttpRequest.newBuilder()
                                        .uri(URI.create("http://127.0.0.1:" + port + "/api/orders"))
                                        .timeout(Duration.ofSeconds(10))
                                        .header("Content-Type", "application/json")
                                        .header("Cookie", cookie)
                                        .POST(HttpRequest.BodyPublishers.ofString(json))
                                        .build(),
                                HttpResponse.BodyHandlers.ofString());
        assertThat(res.statusCode()).isEqualTo(200);
        settle();
        sink.got.clear();

        /* 첫 바퀴: 처음 보는 호가 둘과 잔고를 보낸다. 주문은 바뀐 것이 없다 */
        poller.tick();
        settle();
        assertThat(kind(sink, "book")).hasSize(2);
        assertThat(kind(sink, "balance")).hasSize(1);
        assertThat(kind(sink, "order-update")).isEmpty();
        sink.got.clear();

        /* 아무것도 안 바뀌면 아무것도 안 보낸다 */
        poller.tick();
        settle();
        assertThat(sink.got).isEmpty();

        /* 나중 체결: 주문 상태와 체결(시장·가격·수량)을 보낸다 */
        ledger.fillLater(100050, 4, 70000);
        poller.tick();
        settle();
        assertThat(kind(sink, "order-update")).hasSize(1);
        assertThat(kind(sink, "order-update").get(0))
                .contains("\"filled\":4")
                .contains("\"working\":6");
        List<String> fills = kind(sink, "fill");
        assertThat(fills).hasSize(1);
        assertThat(fills.get(0))
                .contains("\"market\":1")
                .contains("\"qty\":4")
                .contains("\"price\":70000")
                .contains("\"orderId\":100050");
        sink.got.clear();

        /* 두 번째 나중 체결은 늘어난 몫만 — 가격은 금액 차이 / 수량 차이 */
        ledger.fillLater(100050, 2, 69900);
        poller.tick();
        settle();
        fills = kind(sink, "fill");
        assertThat(fills).hasSize(1);
        assertThat(fills.get(0)).contains("\"qty\":2").contains("\"price\":69900");
        sink.got.clear();

        /* 호가가 바뀌면 호가만, 잔고가 바뀌면 잔고만 */
        ledger.bumpBook(5);
        ledger.setBalance(100_000_000L, 280_000L);
        poller.tick();
        settle();
        assertThat(kind(sink, "book")).hasSize(2); // bump는 두 시장의 매수 1단에 모두 더해진다
        assertThat(kind(sink, "balance")).hasSize(1);
        assertThat(kind(sink, "balance").get(0)).contains("\"available\":99720000");
        assertThat(kind(sink, "order-update")).isEmpty();
        sink.got.clear();

        /* 끝난 주문은 다시 묻지 않는다 — 목록이 500건이면 매초 500번 원장을 부르게 된다 */
        ledger.fillLater(100050, 4, 70000);
        poller.tick();
        settle();
        assertThat(kind(sink, "order-update").get(0)).contains("\"done\":true");
        int before = ledger.detailCalls();
        poller.tick();
        assertThat(ledger.detailCalls()).isEqualTo(before);

        ws.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }
}
