package com.minisor.channel;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.ledger.LedgerProperties;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
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
     * 설정에서 읽는다는 것을 <b>설정을 바꿔서</b> 확인한다. 기본값(0)을 그대로
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
