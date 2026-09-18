package com.minisor.channel.feed;

import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.time.Duration;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Autowired;
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

    private final FeedProperties props;
    private final LiveFeed live;
    private final TossTokenSource tokens;
    private final HttpClient http =
            HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();

    private final AtomicBoolean running = new AtomicBoolean();
    private volatile Thread loop;
    private volatile WebSocket socket;

    @Autowired
    public TossFeedClient(FeedProperties props, LiveFeed live) {
        this(props, live, new TossTokenSource(props));
    }

    TossFeedClient(FeedProperties props, LiveFeed live, TossTokenSource tokens) {
        this.props = props;
        this.live = live;
        this.tokens = tokens;
    }

    public boolean usable() {
        return props.usable();
    }

    /**
     * 뜨자마자 붙는다 — 설정이 켜져 있을 때만. 기본은 꺼짐이라 아무 일도 하지 않는다.
     *
     * <p>앱이 다 뜬 뒤에 시작한다. 생성자에서 시작하면 원장 게이트웨이가 아직 없을 수 있다.
     */
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
            }
            if (!running.get() || !sleep(backoff)) {
                return;
            }
            backoff = Math.min(backoff * 2, BACKOFF_MAX_MS);
        }
    }

    private void connectAndPump() throws Exception {
        String token = tokens.token();
        CountDownLatch closed = new CountDownLatch(1);

        WebSocket ws =
                http.newWebSocketBuilder()
                        .header("Authorization", "Bearer " + token)
                        .connectTimeout(Duration.ofSeconds(10))
                        .buildAsync(URI.create(props.wsUrl()), new Listener(closed))
                        .join();
        socket = ws;
        ws.sendText(subscribeJson(props.symbol()), true);
        live.enterLive("toss");
        log.info("실시세 구독: {} (시장 {})", props.symbol(), props.market());

        closed.await();
        socket = null;
        live.enterSim();
        throw new IllegalStateException("구독이 끊겼다");
    }

    /**
     * 화면에 보일 한 줄. 자바 예외 클래스 이름은 화면에서 뜻이 없다.
     *
     * <p>실제로 겪은 것: 허용 IP를 등록하지 않으면 토큰 발급이 403
     * {@code {"error":"access_denied","error_description":"IP address not allowed"}}이다.
     */
    static String reason(Throwable e) {
        String m = e.getMessage();
        return (m == null || m.isBlank()) ? e.toString() : m;
    }

    /** {@code [{"type":"orderbook:kr","codes":["005930"]}]} */
    static String subscribeJson(String symbol) {
        return "[{\"type\":\"orderbook:kr\",\"codes\":[\"" + symbol + "\"]}]";
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

        @Override
        public CompletionStage<?> onClose(WebSocket ws, int code, String reason) {
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
            if (!topic.startsWith("orderbook:")) {
                return; /* 체결(realtime-trade)은 호가에 이미 반영돼 온다 */
            }
            live.apply(Snapshot.fromToss(props.symbol(), n.path("data"), live.status().lastFeedTs()));
        } catch (Exception e) {
            log.warn("실시세 메시지를 읽지 못했다: {}", e.toString());
        }
    }
}
