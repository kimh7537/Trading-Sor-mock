package com.minisor.channel.feed;

import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.net.http.WebSocket;
import java.nio.ByteBuffer;
import java.time.Duration;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.boot.context.event.ApplicationReadyEvent;
import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Component;

/**
 * 토스증권 실시간 호가를 구독해 원장에 밀어 넣는다 (T8-04).
 *
 * <h2>읽기만 한다</h2>
 *
 * 구독하는 채널은 호가뿐이고 <b>주문 API는 부르지 않는다.</b> 토스에는 모의투자 샌드박스가
 * 없어 같은 키로 실주문이 나간다 — 주문 경로를 아예 만들지 않는 것이 유일하게 확실한 방어다.
 *
 * <h2>다시 붙기</h2>
 *
 * 끊기면 지수 백오프(1초에서 시작해 두 배씩, 60초 상한)로 다시 붙는다. 붙는 데 성공하면
 * 간격을 되돌린다 — 되돌리지 않으면 짧게 여러 번 끊긴 뒤 영영 1분에 한 번만 시도한다.
 * 429는 백오프 대신 {@code Retry-After}를 따른다({@link TossTokenSource.FeedBackoff}).
 *
 * <h2>기본은 꺼짐</h2>
 *
 * {@code minisor.feed.enabled}가 참이고 {@code .env}에 키가 있어야 돈다. 시험은 이 클래스를
 * 거치지 않고 {@link LiveFeed#apply}에 가짜 스냅샷을 넣는다 — 바깥에 붙는 시험은 장 시간과
 * 허용 IP에 기대므로 재현되지 않는다.
 */
@Component
public class TossFeedClient implements AutoCloseable {

    private static final Logger log = LoggerFactory.getLogger(TossFeedClient.class);
    private static final JsonMapper JSON = JsonMapper.builder().build();

    private static final long BACKOFF_MIN_MS = 1000;
    private static final long BACKOFF_MAX_MS = 60_000;

    /*
     * 이 간격으로 ping을 보낸다.
     *
     * **아무것도 보내지 않으면 서버가 끊는다.** 장이 닫히면 호가 변화가 없어 수신도 송신도
     * 없는 유휴 상태가 되는데, 실제로 그 상태로 2분 48초 만에 끊겼다
     * (`HTTP/1.1 header parser received no bytes`). 그 뒤 재연결은 한동안 거부됐다 —
     * 동시 연결 수를 서버가 아직 붙들고 있기 때문으로 보인다.
     *
     * 텍스트로 `{"type":"ping"}`을 보내지 않는다. 서버는 모르는 `type`에 `invalid-type`
     * 오류를 돌려준다(구독 토픽을 고를 때 확인했다). **표준 WebSocket Ping 프레임**이
     * 프로토콜이 정한 방법이고 상대가 무엇을 기대하든 안전하다.
     */
    private static final long PING_SEC = 30;

    private final FeedProperties props;
    private final LiveFeed live;
    private final TossTokenSource tokens;
    private final SymbolState symbols;
    private final HttpClient http =
            HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();

    /*
     * 붙은 뒤 처음 받는 몇 건은 **원문 그대로** 로그에 남긴다.
     *
     * 붙었는데 아무것도 오지 않을 때 원인이 둘이다 — 장이 닫혔거나, 구독 선언을 서버가
     * 받아들이지 않았거나. 조용히 버리면 둘을 구분할 수 없다. 구독 확인이든 오류 응답이든
     * 무엇이라도 오는지가 그 갈림길이다.
     */
    private static final int LOG_FIRST_MESSAGES = 5;

    private final AtomicInteger received = new AtomicInteger();
    private final AtomicBoolean running = new AtomicBoolean();
    private volatile Thread loop;
    private volatile WebSocket socket;

    public TossFeedClient(
            FeedProperties props, LiveFeed live, TossTokenSource tokens, SymbolState symbols) {
        this.props = props;
        this.live = live;
        this.tokens = tokens;
        this.symbols = symbols;
    }

    /**
     * 종목이 바뀌었다 — <b>연결을 놓는다</b>(T8-10).
     *
     * <p>구독을 따로 갈아끼우지 않는다. 끊기면 루프가 알아서 다시 붙고, 그때 새 종목으로
     * 구독한다. 구독 해제 전문을 따로 다루는 것보다 경로가 하나 적다.
     */
    public void resubscribe() {
        WebSocket ws = socket;
        if (ws != null) {
            ws.abort();
        }
    }

    public boolean usable() {
        return props.usable();
    }

    /**
     * 뜨자마자 붙는다 — 설정이 켜져 있을 때만. 기본은 꺼짐이라 아무 일도 하지 않는다.
     *
     * <p>앱이 다 뜬 뒤에 시작한다. 생성자에서 시작하면 원장 게이트웨이가 아직 없을 수 있다.
     *
     * <p><b>맨 나중이다.</b> 원장이 들고 있는 종목을 맞추기 전에 구독하면 옛 종목을 구독한다
     * ({@code SymbolService#syncFromLedger}).
     */
    @org.springframework.core.annotation.Order(org.springframework.core.Ordered.LOWEST_PRECEDENCE)
    @EventListener(ApplicationReadyEvent.class)
    void autoStart() {
        start();
    }

    public boolean isRunning() {
        return running.get();
    }

    /** 이미 돌고 있으면 아무 일도 하지 않는다. 설정이 없으면 거짓을 돌려준다. */
    public synchronized boolean start() {
        if (!props.usable()) {
            return false;
        }
        if (!running.compareAndSet(false, true)) {
            return true;
        }
        Thread t = new Thread(this::run, "toss-feed");
        t.setDaemon(true);
        loop = t;
        t.start();
        return true;
    }

    public synchronized void stop() {
        running.set(false);
        WebSocket ws = socket;
        if (ws != null) {
            ws.abort();
        }
        Thread t = loop;
        if (t != null) {
            t.interrupt();
        }
        live.enterSim();
    }

    @Override
    public void close() {
        stop();
    }

    private void run() {
        long backoff = BACKOFF_MIN_MS;
        while (running.get()) {
            try {
                connectAndPump();
                backoff = BACKOFF_MIN_MS; /* 한 번 붙었으면 간격을 되돌린다 */
            } catch (TossTokenSource.FeedBackoff b) {
                log.warn("실시세: {}", b.getMessage());
                live.noteError(b.getMessage());
                if (!sleep(b.waitMs())) {
                    return;
                }
                continue;
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return;
            } catch (Exception e) {
                log.warn("실시세 끊김: {}", e.toString());
                live.noteError(reason(e));
                /*
                 * **다음 시도는 새 토큰으로 한다.** client 당 토큰이 하나라 세션이 끊기면
                 * 서버 쪽에서 그 토큰이 더는 통하지 않을 수 있는데, 이쪽 캐시는 "만료 전"이라
                 * 같은 토큰을 계속 내준다. 그러면 재연결이 영영 거부된다.
                 */
                tokens.invalidate();
            }
            if (!running.get() || !sleep(backoff)) {
                return;
            }
            backoff = Math.min(backoff * 2, BACKOFF_MAX_MS);
        }
    }

    /** 이번 연결이 어떻게 끝났는지. 서버가 닫았으면 그 코드와 사유가 들어온다. */
    private volatile String closeNote = "까닭 모름";

    private void connectAndPump() throws Exception {
        String token = tokens.token();
        CountDownLatch closed = new CountDownLatch(1);
        closeNote = "까닭 모름";

        WebSocket ws =
                http.newWebSocketBuilder()
                        .header("Authorization", "Bearer " + token)
                        .connectTimeout(Duration.ofSeconds(10))
                        .buildAsync(URI.create(props.wsUrl()), new Listener(closed))
                        .join();
        socket = ws;
        String sub = subscribeJson(symbols.code());
        received.set(0);
        ws.sendText(sub, true);
        live.enterLive("toss");
        log.info("실시세 구독 보냄: {} (시장 {})", sub, props.market());

        /*
         * 구독만으로는 **다음 호가 변화가 올 때까지** 화면이 빈 채로 있다. 장이 닫혀 있으면
         * 영영 오지 않는다. REST로 지금 호가를 한 번 받아 채운다 — 붙자마자 보인다.
         */
        primeFromRest(token);

        /* 닫힐 때까지 기다리되, 조용한 동안 ping으로 연결을 살려 둔다 */
        while (running.get() && !closed.await(PING_SEC, TimeUnit.SECONDS)) {
            ws.sendPing(ByteBuffer.allocate(0));
        }

        log.info("실시세 구독이 닫혔다. 받은 메시지 {}건 — {}", received.get(), closeNote);
        /*
         * **확실히 놓아 준다.** 그냥 버리면 서버가 그 연결을 한동안 붙들고, 동시 연결 수에
         * 걸려 다시 붙지 못한다(재연결이 `WebSocketHandshakeException`으로 거부됐다).
         */
        ws.abort();
        socket = null;
        live.enterSim();
        throw new IllegalStateException("구독이 끊겼다 — " + closeNote);
    }

    /**
     * 체결 한 건. <b>실물 메시지를 본 적이 없다</b> — 장중이 아니면 오지 않는다.
     *
     * <p>{@code trade:kr} 토픽이 유효하다는 것은 서버의 구독 응답으로 확인했다
     * ({@code subscribed:["trade:kr:005930"]}). 필드 이름은 호가와 같은 규약
     * ({@code price} / {@code volume})을 먼저 보고, 흔한 대안({@code quantity})도 본다.
     * 읽지 못하면 <b>버린다</b> — 추측한 값을 화면에 올리지 않는다. 앞의 몇 건은 원문을
     * 로그에 남기므로 장중에 한 번 보면 확정된다.
     */
    private void onTrade(JsonNode data) {
        int price = intOf(data, "price");
        int qty = intOf(data, "volume");
        if (qty <= 0) {
            qty = intOf(data, "quantity");
        }
        live.onTrade(price, qty);
    }

    private static int intOf(JsonNode data, String field) {
        JsonNode v = data.path(field);
        if (v.isMissingNode() || v.isNull()) {
            return 0;
        }
        try {
            return new java.math.BigDecimal(v.asString()).intValue();
        } catch (RuntimeException e) {
            return 0;
        }
    }

    /**
     * 붙자마자 REST로 호가를 한 번 받아 원장에 심는다.
     *
     * <p>{@code GET /api/v1/orderbook}의 파라미터는 {@code symbol} 하나뿐이다 — 시장을 고르는
     * 인자가 없다. "국내는 통합 시세(KRX+NXT)만"이 스펙 수준에서 확인되는 자리다.
     *
     * <p>실패해도 구독은 계속한다. 첫 화면을 채우려는 것이지 이것이 피드는 아니다.
     */
    private void primeFromRest(String token) {
        try {
            HttpRequest req =
                    HttpRequest.newBuilder(
                                    URI.create(
                                            props.baseUrl()
                                                    + "/api/v1/orderbook?symbol="
                                                    + symbols.code()))
                            .header("Authorization", "Bearer " + token)
                            .header("Accept", "application/json")
                            .timeout(Duration.ofSeconds(10))
                            .GET()
                            .build();

            HttpResponse<byte[]> res = http.send(req, HttpResponse.BodyHandlers.ofByteArray());
            String body = TossTokenSource.text(res);
            if (res.statusCode() / 100 != 2) {
                log.warn("첫 호가 조회 실패 {}: {}", res.statusCode(), body);
                return;
            }
            log.info(
                    "첫 호가: {}",
                    body.length() > 400 ? body.substring(0, 400) + "…" : body);

            JsonNode n = JSON.readTree(body);
            live.apply(
                    Snapshot.fromToss(
                            symbols.code(), n.path("result"), live.status().lastFeedTs()));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        } catch (Exception e) {
            log.warn("첫 호가를 받지 못했다: {}", e.toString());
        }
    }

    /**
     * 화면에 보일 한 줄. 자바 예외 클래스 이름은 화면에서 뜻이 없다.
     *
     * <p>실제로 겪은 것: 허용 IP를 등록하지 않으면 토큰 발급이 403
     * {@code {"error":"access_denied","error_description":"IP address not allowed"}}이다.
     */
    static String reason(Throwable e) {
        /*
         * 껍질을 벗긴다. `CompletionException: IOException: ...`처럼 겹쳐 오면 화면에 자바
         * 클래스 이름만 길게 남고 정작 무슨 일인지가 묻힌다.
         */
        Throwable root = e;
        while (root.getCause() != null && root.getCause() != root) {
            root = root.getCause();
        }
        String m = root.getMessage();
        return (m == null || m.isBlank()) ? root.getClass().getSimpleName() : m;
    }

    /**
     * {@code [{"type":"orderbook:kr","codes":["005930"]}, …]}
     *
     * <p>체결 채널의 토픽 이름은 공개 스펙에 없다. 서버가 구독 응답에
     * {@code subscribed}/{@code rejected}를 돌려주므로 <b>후보를 같이 보내고 어느 것이
     * 받아들여지는지 본다</b> — 추측으로 하나만 적어 두는 것보다 확실하다.
     */
    static String subscribeJson(String symbol) {
        StringBuilder b = new StringBuilder("[");
        String[] types = {"orderbook:kr", "trade:kr"};
        for (int i = 0; i < types.length; i++) {
            b.append(i > 0 ? "," : "")
                    .append("{\"type\":\"")
                    .append(types[i])
                    .append("\",\"codes\":[\"")
                    .append(symbol)
                    .append("\"]}");
        }
        return b.append("]").toString();
    }

    /** 참이면 계속 간다. 거짓이면 종료 중이다. */
    private boolean sleep(long ms) {
        try {
            TimeUnit.MILLISECONDS.sleep(ms);
            return running.get();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    /**
     * 받은 텍스트를 스냅샷으로 옮긴다.
     *
     * <p>조각으로 오는 메시지를 모은다 — {@code onText}는 {@code last}가 참일 때까지 이어진다.
     */
    private final class Listener implements WebSocket.Listener {
        private final CountDownLatch closed;
        private final StringBuilder buf = new StringBuilder();

        Listener(CountDownLatch closed) {
            this.closed = closed;
        }

        @Override
        public CompletionStage<?> onText(WebSocket ws, CharSequence data, boolean last) {
            buf.append(data);
            if (last) {
                String text = buf.toString();
                buf.setLength(0);
                handle(ws, text);
            }
            ws.request(1);
            return null;
        }

        /*
         * **누가 왜 끊었는지를 적는다.** 서버가 끊으면 코드와 사유가 여기로만 온다. 버리면
         * 화면에는 "시뮬로 돌아왔다"만 남고 이유가 사라진다 — 실제로 그래서 되풀이되는
         * 끊김의 원인을 한참 못 찾았다.
         */
        @Override
        public CompletionStage<?> onClose(WebSocket ws, int code, String reason) {
            closeNote = "서버가 닫았다 " + code + (reason == null || reason.isBlank() ? "" : " " + reason);
            closed.countDown();
            return null;
        }

        @Override
        public void onError(WebSocket ws, Throwable error) {
            log.warn("실시세 소켓 오류: {}", error.toString());
            closed.countDown();
        }
    }

    /** 패키지 밖에서 부르지 않는다. 시험이 메시지 한 건을 그대로 넣어 볼 수 있게 열어 둔다. */
    void handle(WebSocket ws, String text) {
        int seq = received.incrementAndGet();
        if (seq <= LOG_FIRST_MESSAGES) {
            log.info(
                    "실시세 수신 {}: {}",
                    seq,
                    text.length() > 400 ? text.substring(0, 400) + "…" : text);
        }
        try {
            JsonNode n = JSON.readTree(text);
            String type = n.path("type").asText("");
            if ("ping".equals(type)) {
                if (ws != null) {
                    ws.sendText("{\"type\":\"pong\"}", true);
                }
                return;
            }
            if (!"message".equals(type)) {
                return;
            }
            String topic = n.path("topic").asText("");
            if (topic.startsWith("trade:")) {
                onTrade(n.path("data"));
                return;
            }
            if (!topic.startsWith("orderbook:")) {
                return;
            }
            live.apply(
                    Snapshot.fromToss(symbols.code(), n.path("data"), live.status().lastFeedTs()));
        } catch (Exception e) {
            log.warn("실시세 메시지를 읽지 못했다: {}", e.toString());
        }
    }
}
