package com.minisor.channel.auth;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.ledger.FakeLedger;
import java.net.CookieManager;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Duration;
import org.junit.jupiter.api.AfterAll;
import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

/**
 * T9-03·T9-04 — 로그인해야 거래할 수 있고, 계좌는 <b>세션에서만</b> 나온다.
 *
 * <p>가장 중요한 시험은 {@link #ordersAreIsolatedPerUser()}다. 사용자가 여럿인 전제에서
 * 남의 주문이 내 목록에 보이면 그것만으로 이 기능은 쓸 수 없다.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class AuthApiTest {

    private static FakeLedger ledger;

    private static final Path USERS_FILE =
            Path.of(System.getProperty("java.io.tmpdir"),
                    "minisor-auth-users-" + System.nanoTime() + ".json");

    private static final Path DB_FILE =
            Path.of(System.getProperty("java.io.tmpdir"),
                    "minisor-auth-db-" + System.nanoTime() + ".db");

    @DynamicPropertySource
    static void props(DynamicPropertyRegistry reg) throws Exception {
        ledger = new FakeLedger();
        reg.add("minisor.ledger.port", ledger::port);
        reg.add("minisor.ledger.read-timeout-ms", () -> 500);
        reg.add("minisor.poller.enabled", () -> false);
        reg.add("minisor.auth.users-file", () -> USERS_FILE.toString());
        /* 시험마다 새 저장소. 같은 파일을 쓰면 계좌번호가 시험 사이에 이어진다 */
        reg.add("minisor.db.file", () -> DB_FILE.toString());
    }

    @AfterAll
    static void cleanup() throws Exception {
        Files.deleteIfExists(USERS_FILE);
    }

    @LocalServerPort private int port;

    /** 사람 한 명 몫의 브라우저. 쿠키를 따로 들고 다닌다. */
    private HttpClient browser() {
        return HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(5))
                .cookieHandler(new CookieManager())
                .build();
    }

    private HttpResponse<String> post(HttpClient c, String path, String json) throws Exception {
        return c.send(
                HttpRequest.newBuilder()
                        .uri(URI.create("http://127.0.0.1:" + port + path))
                        .timeout(Duration.ofSeconds(10))
                        .header("Content-Type", "application/json")
                        .POST(HttpRequest.BodyPublishers.ofString(json))
                        .build(),
                HttpResponse.BodyHandlers.ofString());
    }

    private HttpResponse<String> get(HttpClient c, String path) throws Exception {
        return c.send(
                HttpRequest.newBuilder()
                        .uri(URI.create("http://127.0.0.1:" + port + path))
                        .timeout(Duration.ofSeconds(10))
                        .GET()
                        .build(),
                HttpResponse.BodyHandlers.ofString());
    }

    private static String creds(String id) {
        return "{\"id\":\"" + id + "\",\"password\":\"password1\"}";
    }

    private static String order(long clOrdId) {
        return "{\"symbol\":\"005930\",\"clOrdId\":" + clOrdId
                + ",\"side\":0,\"type\":0,\"market\":0,\"price\":70000,\"qty\":10}";
    }

    /** 로그인하지 않으면 거래 경로가 전부 막힌다. 시장을 보는 경로는 열려 있다. */
    @Test
    void anonymousCannotTrade() throws Exception {
        HttpClient c = browser();

        assertThat(post(c, "/api/orders", order(901)).statusCode()).isEqualTo(401);
        assertThat(get(c, "/api/orders").statusCode()).isEqualTo(401);
        assertThat(get(c, "/api/balance").statusCode()).isEqualTo(401);
        assertThat(get(c, "/api/auth/me").statusCode()).isEqualTo(401);

        /* 호가는 로그인 없이도 본다 — 시장은 누구의 것도 아니다 */
        assertThat(get(c, "/api/book?market=0").statusCode()).isEqualTo(200);
    }

    @Test
    void signupThenLoginThenLogout() throws Exception {
        HttpClient c = browser();

        HttpResponse<String> up = post(c, "/api/auth/signup", creds("carol"));
        assertThat(up.statusCode()).isEqualTo(200);
        assertThat(up.body()).contains("\"id\":\"carol\"").contains("\"account\":\"u");
        /* 비밀번호와 관련된 것은 무엇도 내보내지 않는다 */
        assertThat(up.body()).doesNotContain("password").doesNotContain("hash");

        assertThat(get(c, "/api/auth/me").statusCode()).isEqualTo(200);

        /* 같은 아이디로 또 가입하면 409 */
        assertThat(post(c, "/api/auth/signup", creds("carol")).statusCode()).isEqualTo(409);

        /* 비밀번호가 틀리면 401 */
        assertThat(post(c, "/api/auth/login",
                        "{\"id\":\"carol\",\"password\":\"wrongpassword\"}")
                        .statusCode())
                .isEqualTo(401);

        assertThat(post(c, "/api/auth/logout", "").statusCode()).isEqualTo(204);
        assertThat(get(c, "/api/auth/me").statusCode()).isEqualTo(401);
    }

    /** 두 사람이 각자 주문을 낸다. <b>서로의 주문이 보이면 안 된다.</b> */
    @Test
    void ordersAreIsolatedPerUser() throws Exception {
        HttpClient dan = browser();
        HttpClient erin = browser();
        assertThat(post(dan, "/api/auth/signup", creds("dan")).statusCode()).isEqualTo(200);
        assertThat(post(erin, "/api/auth/signup", creds("erin")).statusCode()).isEqualTo(200);

        assertThat(post(dan, "/api/orders", order(911)).statusCode()).isEqualTo(200);
        assertThat(post(erin, "/api/orders", order(912)).statusCode()).isEqualTo(200);

        String danList = get(dan, "/api/orders").body();
        String erinList = get(erin, "/api/orders").body();

        assertThat(danList).contains("\"clOrdId\":911").doesNotContain("\"clOrdId\":912");
        assertThat(erinList).contains("\"clOrdId\":912").doesNotContain("\"clOrdId\":911");
    }
}
