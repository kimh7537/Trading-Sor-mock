package com.minisor.channel.feed;

import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import java.io.IOException;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.zip.GZIPInputStream;

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

    /**
     * 들고 있던 토큰을 버린다. 다음 {@link #token()}이 새로 받는다.
     *
     * <p><b>끊긴 뒤 다시 붙을 때 쓴다.</b> client 당 토큰이 하나라, 세션이 끊기면 서버 쪽에서
     * 그 토큰이 더는 통하지 않을 수 있다. 그런데 이쪽 캐시는 "만료 전"이라 같은 토큰을 계속
     * 내주고, 재연결이 영영 거부된다 — 실제로 20번 연속 실패했다.
     */
    public synchronized void invalidate() {
        token = null;
        expiresAtMs = 0;
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
                        .header("Accept", "application/json")
                        .timeout(Duration.ofSeconds(10))
                        .POST(HttpRequest.BodyPublishers.ofString(form, StandardCharsets.UTF_8))
                        .build();

        HttpResponse<byte[]> res = http.send(req, HttpResponse.BodyHandlers.ofByteArray());
        if (res.statusCode() == 429) {
            throw new FeedBackoff(retryAfterMs(res), "토큰 발급 429");
        }
        String body = text(res);
        if (res.statusCode() / 100 != 2) {
            /*
             * 403이면 허용 IP 미등록이 첫 번째 의심이다. 본문을 그대로 실어 올린다 —
             * 삼키면 "왜 안 붙지"를 밖에서 알 길이 없다.
             */
            throw new IOException("토큰 발급 실패 " + res.statusCode() + ": " + body);
        }

        JsonNode n = JSON.readTree(body);
        String got = n.path("access_token").asText(null);
        if (got == null || got.isBlank()) {
            throw new IOException("토큰 응답에 access_token이 없다: " + body);
        }
        long ttl = n.path("expires_in").asLong(3600);
        token = got;
        expiresAtMs = System.currentTimeMillis() + ttl * 1000;
        return token;
    }

    /**
     * 본문을 글자로 바꾼다. <b>gzip으로 오면 푼다.</b>
     *
     * <p>JDK의 {@code HttpClient}는 {@code Content-Encoding}을 스스로 풀지 않는다. 실제로 붙어 보니
     * 앞단이 403 본문을 gzip으로 돌려줬고, 그대로 읽으니 로그에 깨진 바이트만 남았다. 오류 본문을
     * 실어 올리는 이유가 "왜 안 붙는지 밖에서 보이게" 하려는 것이므로 그러면 뜻이 없다.
     *
     * <p>성공 응답도 같은 길로 올 수 있다 — 그때는 <b>토큰 파싱이 통째로 실패</b>한다. 그래서
     * 헤더만 믿지 않고 gzip 매직 바이트({@code 1f 8b})도 함께 본다.
     */
    static String text(HttpResponse<byte[]> res) {
        byte[] raw = res.body();
        if (raw == null || raw.length == 0) {
            return "";
        }
        boolean gzipHeader =
                res.headers().firstValue("Content-Encoding").orElse("").toLowerCase().contains("gzip");
        boolean gzipMagic =
                raw.length > 1 && (raw[0] & 0xff) == 0x1f && (raw[1] & 0xff) == 0x8b;
        if (!gzipHeader && !gzipMagic) {
            return new String(raw, StandardCharsets.UTF_8);
        }
        try (GZIPInputStream in = new GZIPInputStream(new ByteArrayInputStream(raw))) {
            return new String(in.readAllBytes(), StandardCharsets.UTF_8);
        } catch (IOException e) {
            /* gzip이라고 했는데 풀리지 않는다. 있는 그대로라도 보여 준다 */
            return new String(raw, StandardCharsets.UTF_8);
        }
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
