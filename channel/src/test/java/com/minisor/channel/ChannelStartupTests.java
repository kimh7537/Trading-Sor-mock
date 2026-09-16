package com.minisor.channel;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.ledger.LedgerProperties;
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
import org.springframework.test.context.TestPropertySource;

/**
 * T4-01 완료 조건을 그대로 옮긴다.
 *
 * <ol>
 *   <li>애플리케이션이 실제 포트로 뜬다
 *   <li>헬스 체크가 응답한다
 *   <li>원장 접속 정보를 <b>설정에서</b> 읽는다 (코드에 박혀 있지 않다)
 * </ol>
 *
 * <p>HTTP 호출에 JDK 내장 {@link HttpClient}를 쓴다. Spring Boot의 테스트 전용
 * 클라이언트는 판올림마다 모양이 바뀌는데, 이 테스트가 확인하려는 것은
 * <b>스프링이 아니라 우리 설정</b>이다. 흔들리지 않는 도구를 쓴다.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
@TestPropertySource(properties = "minisor.ledger.port=17001")
class ChannelStartupTests {

    @LocalServerPort private int port;

    @Autowired private LedgerProperties ledger;

    private HttpResponse<String> get(String path) throws Exception {
        HttpClient client =
                HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();
        HttpRequest req =
                HttpRequest.newBuilder()
                        .uri(URI.create("http://127.0.0.1:" + port + path))
                        .timeout(Duration.ofSeconds(5))
                        .GET()
                        .build();
        return client.send(req, HttpResponse.BodyHandlers.ofString());
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

    private WebSocket subscribe(Sink sink) throws Exception {
        return HttpClient.newHttpClient()
                .newWebSocketBuilder()
                .buildAsync(URI.create("ws://127.0.0.1:" + port + "/ws/stream"), sink)
                .get(5, TimeUnit.SECONDS);
    }

    /**
     * T6-10 — <b>원장이 없으면 화면이 그 사실을 받는다.</b> 이 컨텍스트의 원장 포트(17001)에는
     * 아무도 없다. 예전엔 {@code ledger-down}을 보내는 코드가 없었다.
     *
     * <p>한 번만 온다 — 화면이 1초마다 호가를 읽을 때마다 쌓이면 안 된다. 그리고 끊긴 뒤에
     * 들어온 화면도 받는다.
     */
    @Test
    void ledgerDownReachesSubscribers() throws Exception {
        Sink early = new Sink();
        WebSocket ws = subscribe(early);
        Thread.sleep(200);

        assertThat(get("/api/book?market=0").statusCode()).isEqualTo(503);
        assertThat(get("/api/book?market=1").statusCode()).isEqualTo(503);
        for (int i = 0; i < 150 && early.got.isEmpty(); i++) {
            Thread.sleep(20);
        }
        Thread.sleep(200);
        assertThat(early.got).hasSize(1);
        assertThat(early.got.get(0)).contains("ledger-down");

        Sink late = new Sink();
        WebSocket ws2 = subscribe(late);
        for (int i = 0; i < 150 && late.got.isEmpty(); i++) {
            Thread.sleep(20);
        }
        assertThat(late.got).hasSize(1);
        assertThat(late.got.get(0)).contains("ledger-down");

        ws.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
        ws2.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }

    @Test
    void healthResponds() throws Exception {
        HttpResponse<String> res = get("/actuator/health");

        assertThat(res.statusCode()).isEqualTo(200);
        assertThat(res.body()).contains("\"status\":\"UP\"");
    }

    /**
     * 열어 둔 것만 열려 있어야 한다. actuator를 통째로 열면 환경 변수와 빈
     * 목록이 그대로 나간다.
     */
    @Test
    void onlyHealthIsExposed() throws Exception {
        assertThat(get("/actuator/env").statusCode()).isNotEqualTo(200);
        assertThat(get("/actuator/beans").statusCode()).isNotEqualTo(200);
    }

    /**
     * 설정에서 읽는다는 것을 <b>설정을 바꿔서</b> 확인한다. 기본값(9100)을 그대로
     * 읽고 통과하면 "코드에 박힌 값을 읽었을 때"와 구분되지 않는다.
     */
    @Test
    void ledgerEndpointComesFromConfiguration() {
        assertThat(ledger.host()).isEqualTo("127.0.0.1");
        assertThat(ledger.port()).isEqualTo(17001); // 이 테스트가 덮어쓴 값
        assertThat(ledger.isConfigured()).isTrue();

        assertThat(ledger.connectTimeoutMs()).isEqualTo(3000);
        assertThat(ledger.readTimeoutMs()).isEqualTo(5000);
    }
}
