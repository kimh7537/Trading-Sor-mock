package com.minisor.channel.api;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.ledger.FakeLedger;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
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
    }

    @LocalServerPort private int port;

    @Autowired private OrderService service; // 컨텍스트가 떴는지 확인용

    @AfterEach
    void reset() {
        ledger.setSilent(false);
        ledger.setDelayMs(0);
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

    private static String order(long clOrdId) {
        return """
               {"account":"123456789012","symbol":"005930","clOrdId":%d,
                "side":1,"type":1,"market":1,"price":70000,"qty":10}
               """
                .formatted(clOrdId);
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
