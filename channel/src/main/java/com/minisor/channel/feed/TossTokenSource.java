package com.minisor.channel.feed;

import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;

/**
 * OAuth 2.0 client credentials 토큰 (T8-04).
 *
 * <h2>client 당 토큰이 하나다</h2>
 *
 * 토스 스펙에 적혀 있다 — <b>재발급하면 이전 토큰이 즉시 무효가 된다.</b> 그래서
 * <ul>
 *   <li>여기서만 발급한다. 쓰는 쪽은 {@link #token()}으로 받아 간다
 *   <li><b>만료 전에 미리</b> 바꾼다. 만료된 뒤에 바꾸면 그사이 수신이 끊긴다
 *   <li>같은 키로 프로세스를 둘 띄우면 서로 죽인다. 막을 방법이 이쪽에 없으므로 문서에 적는다
 * </ul>
 *
 * <h2>429</h2>
 *
 * {@code Retry-After}를 따른다. 스스로 자지 않고 {@link FeedBackoff}에 담아 올린다 —
 * 얼마나 기다릴지는 알지만 <b>언제 다시 부를지는 부르는 쪽의 결정</b>이다(종료 중일 수도 있다).
 */
public class TossTokenSource {

    private static final JsonMapper JSON = JsonMapper.builder().build();

    /** 만료 얼마 전에 미리 바꾸나. */
    private static final Duration EARLY = Duration.ofSeconds(60);

    private final FeedProperties props;
    private final HttpClient http;

    private String token;
    private long expiresAtMs;

    public TossTokenSource(FeedProperties props) {
        this(props, HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build());
    }

    TossTokenSource(FeedProperties props, HttpClient http) {
        this.props = props;
        this.http = http;
    }

    /** 살아 있는 토큰. 없거나 곧 만료면 새로 받는다. */
    public synchronized String token() throws IOException, InterruptedException {
        if (token != null && System.currentTimeMillis() < expiresAtMs - EARLY.toMillis()) {
            return token;
        }
        return issue();
    }

    private String issue() throws IOException, InterruptedException {
        String form =
                "grant_type=client_credentials"
                        + "&client_id="
                        + enc(props.clientId())
                        + "&client_secret="
                        + enc(props.clientSecret());

        HttpRequest req =
                HttpRequest.newBuilder(URI.create(props.baseUrl() + "/oauth2/token"))
                        .header("Content-Type", "application/x-www-form-urlencoded")
                        .timeout(Duration.ofSeconds(10))
                        .POST(HttpRequest.BodyPublishers.ofString(form, StandardCharsets.UTF_8))
                        .build();

        HttpResponse<String> res = http.send(req, HttpResponse.BodyHandlers.ofString());
        if (res.statusCode() == 429) {
            throw new FeedBackoff(retryAfterMs(res), "토큰 발급 429");
        }
        if (res.statusCode() / 100 != 2) {
            /*
             * 403이면 허용 IP 미등록이 첫 번째 의심이다. 본문을 그대로 실어 올린다 —
             * 삼키면 "왜 안 붙지"를 밖에서 알 길이 없다.
             */
            throw new IOException("토큰 발급 실패 " + res.statusCode() + ": " + res.body());
        }

        JsonNode n = JSON.readTree(res.body());
        String got = n.path("access_token").asText(null);
        if (got == null || got.isBlank()) {
            throw new IOException("토큰 응답에 access_token이 없다: " + res.body());
        }
        long ttl = n.path("expires_in").asLong(3600);
        token = got;
        expiresAtMs = System.currentTimeMillis() + ttl * 1000;
        return token;
    }

    /** 헤더가 초 단위 숫자를 준다. 없거나 모르는 모양이면 30초. */
    static long retryAfterMs(HttpResponse<?> res) {
        return res.headers()
                .firstValue("Retry-After")
                .map(
                        v -> {
                            try {
                                return Long.parseLong(v.trim()) * 1000;
                            } catch (NumberFormatException e) {
                                return 30_000L;
                            }
                        })
                .orElse(30_000L);
    }

    private static String enc(String v) {
        return URLEncoder.encode(v == null ? "" : v, StandardCharsets.UTF_8);
    }

    /** 얼마나 기다렸다 다시 부를지를 들고 있는 예외. */
    public static final class FeedBackoff extends IOException {
        private final long waitMs;

        public FeedBackoff(long waitMs, String message) {
            super(message + " (" + waitMs + "ms 뒤 재시도)");
            this.waitMs = waitMs;
        }

        public long waitMs() {
            return waitMs;
        }
    }
}
