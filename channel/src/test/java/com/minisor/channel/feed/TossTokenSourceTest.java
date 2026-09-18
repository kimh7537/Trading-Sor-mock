package com.minisor.channel.feed;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.zip.GZIPOutputStream;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * T8-04 — 토큰 발급과 429.
 *
 * <p>진짜 토스에 붙지 않는다. 같은 규약으로 답하는 작은 HTTP 서버를 세운다 — 허용 IP와 장
 * 시간에 기대는 시험은 재현되지 않는다.
 */
class TossTokenSourceTest {

    private HttpServer server;
    private final AtomicInteger calls = new AtomicInteger();
    private final AtomicReference<String> lastBody = new AtomicReference<>("");
    private volatile int status = 200;
    private volatile String body = "{\"access_token\":\"tok-1\",\"expires_in\":3600}";
    private volatile String retryAfter;
    /** 참이면 본문을 gzip으로 돌려준다 — 실제 토스 앞단이 그렇게 했다. */
    private volatile boolean gzip;

    @BeforeEach
    void up() throws IOException {
        server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        server.createContext("/oauth2/token", this::handle);
        server.start();
    }

    private void handle(HttpExchange ex) throws IOException {
        calls.incrementAndGet();
        lastBody.set(new String(ex.getRequestBody().readAllBytes(), StandardCharsets.UTF_8));
        if (retryAfter != null) {
            ex.getResponseHeaders().add("Retry-After", retryAfter);
        }
        byte[] out = body.getBytes(StandardCharsets.UTF_8);
        if (gzip) {
            ByteArrayOutputStream buf = new ByteArrayOutputStream();
            try (GZIPOutputStream z = new GZIPOutputStream(buf)) {
                z.write(out);
            }
            out = buf.toByteArray();
            ex.getResponseHeaders().add("Content-Encoding", "gzip");
        }
        ex.sendResponseHeaders(status, out.length);
        ex.getResponseBody().write(out);
        ex.close();
    }

    @AfterEach
    void down() {
        server.stop(0);
    }

    private TossTokenSource source() {
        FeedProperties p =
                new FeedProperties(
                        true,
                        0,
                        "005930",
                        "wss://example.invalid/ws",
                        "http://127.0.0.1:" + server.getAddress().getPort(),
                        "id-1",
                        "secret-1",
                        "",
                        "",
                        1);
        return new TossTokenSource(p);
    }

    /** client credentials를 폼으로 보내고 토큰을 받는다. 두 번째 호출은 캐시를 쓴다. */
    @Test
    void issuesOnceAndCaches() throws Exception {
        TossTokenSource t = source();
        assertThat(t.token()).isEqualTo("tok-1");
        assertThat(lastBody.get())
                .contains("grant_type=client_credentials")
                .contains("client_id=id-1")
                .contains("client_secret=secret-1");

        assertThat(t.token()).isEqualTo("tok-1");
        /*
         * client 당 유효한 토큰이 하나다 — 재발급하면 이전 토큰이 즉시 무효가 된다.
         * 그래서 살아 있는 토큰을 두 번 받지 않는다.
         */
        assertThat(calls.get()).isEqualTo(1);
    }

    /** 429는 {@code Retry-After}를 그대로 들고 올라온다. 스스로 자지 않는다. */
    @Test
    void honoursRetryAfter() {
        status = 429;
        retryAfter = "7";
        body = "{}";

        assertThatThrownBy(() -> source().token())
                .isInstanceOf(TossTokenSource.FeedBackoff.class)
                .satisfies(e -> assertThat(((TossTokenSource.FeedBackoff) e).waitMs()).isEqualTo(7000));
    }

    /** {@code Retry-After}가 없거나 모르는 모양이면 30초로 둔다. */
    @Test
    void defaultsWhenRetryAfterIsMissing() {
        status = 429;
        retryAfter = null;
        body = "{}";

        assertThatThrownBy(() -> source().token())
                .isInstanceOf(TossTokenSource.FeedBackoff.class)
                .satisfies(
                        e -> assertThat(((TossTokenSource.FeedBackoff) e).waitMs()).isEqualTo(30_000));
    }

    /** 403(허용 IP 미등록이 첫 번째 의심)은 본문을 그대로 실어 올린다. */
    @Test
    void surfacesServerMessage() {
        status = 403;
        body = "{\"error\":\"ip_not_allowed\"}";

        assertThatThrownBy(() -> source().token())
                .isInstanceOf(IOException.class)
                .hasMessageContaining("403")
                .hasMessageContaining("ip_not_allowed");
    }

    /**
     * <b>gzip으로 온 응답을 푼다.</b>
     *
     * <p>JDK {@code HttpClient}는 {@code Content-Encoding}을 스스로 풀지 않는다. 실제로 붙어 보니
     * 앞단이 403 본문을 gzip으로 돌려줬고, 그대로 읽으니 로그에 깨진 바이트만 남았다. 성공
     * 응답도 같은 길로 오면 <b>토큰 파싱이 통째로 실패</b>하므로 둘 다 시험한다.
     */
    @Test
    void readsGzippedBodies() throws Exception {
        gzip = true;
        assertThat(source().token()).isEqualTo("tok-1");

        status = 403;
        body = "{\"error\":\"ip_not_allowed\"}";
        assertThatThrownBy(() -> source().token())
                .isInstanceOf(IOException.class)
                .hasMessageContaining("ip_not_allowed");
    }

    /** 만료가 짧으면 다음 요청 때 다시 받는다 — 만료된 뒤가 아니라 만료 전에. */
    @Test
    void reissuesBeforeExpiry() throws Exception {
        body = "{\"access_token\":\"tok-1\",\"expires_in\":30}";
        TossTokenSource t = source();
        assertThat(t.token()).isEqualTo("tok-1");

        body = "{\"access_token\":\"tok-2\",\"expires_in\":3600}";
        assertThat(t.token()).isEqualTo("tok-2");
        assertThat(calls.get()).isEqualTo(2);
    }
}
