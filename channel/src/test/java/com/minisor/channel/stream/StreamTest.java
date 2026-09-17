package com.minisor.channel.stream;

import static org.assertj.core.api.Assertions.assertThat;

import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.WebSocket;
import java.util.List;
import java.util.concurrent.CompletionStage;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.web.server.LocalServerPort;

/** T4-05: 구독·전파, 이탈, 느린 구독자가 전체를 막지 않는 것. */
@SpringBootTest(
        webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT,
        properties = "minisor.poller.enabled=false")
class StreamTest {

    @LocalServerPort private int port;

    @Autowired private StreamHub hub;

    /** 받은 것을 모으는 구독자. */
    private static final class Sink implements WebSocket.Listener {
        final List<String> got = new CopyOnWriteArrayList<>();
        final CountDownLatch latch;

        Sink(int expect) {
            latch = new CountDownLatch(expect);
        }

        @Override
        public CompletionStage<?> onText(WebSocket ws, CharSequence data, boolean last) {
            got.add(data.toString());
            latch.countDown();
            ws.request(1);
            return null;
        }
    }

    private WebSocket connect(Sink sink) throws Exception {
        return HttpClient.newHttpClient()
                .newWebSocketBuilder()
                .buildAsync(URI.create("ws://127.0.0.1:" + port + "/ws/stream"), sink)
                .get(5, TimeUnit.SECONDS);
    }

    private void waitForSubscribers(int n) throws Exception {
        for (int i = 0; i < 100 && hub.subscriberCount() != n; i++) {
            Thread.sleep(20);
        }
        assertThat(hub.subscriberCount()).isEqualTo(n);
    }

    @Test
    void broadcastsToAllSubscribers() throws Exception {
        Sink a = new Sink(1);
        Sink b = new Sink(1);
        WebSocket wa = connect(a);
        WebSocket wb = connect(b);
        waitForSubscribers(2);

        hub.broadcast(StreamEvent.fill("체결"));

        assertThat(a.latch.await(5, TimeUnit.SECONDS)).isTrue();
        assertThat(b.latch.await(5, TimeUnit.SECONDS)).isTrue();
        assertThat(a.got.get(0)).contains("fill").contains("체결");
        assertThat(b.got.get(0)).contains("fill");

        wa.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
        wb.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }

    /** 원장이 끊기면 화면이 그 사실을 안다 — 조용히 멈추지 않는다. */
    @Test
    void ledgerDownIsVisible() throws Exception {
        Sink a = new Sink(1);
        WebSocket wa = connect(a);
        waitForSubscribers(1);

        hub.broadcast(StreamEvent.ledgerDown("접속 끊김"));

        assertThat(a.latch.await(5, TimeUnit.SECONDS)).isTrue();
        assertThat(a.got.get(0)).contains("ledger-down");

        wa.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }

    /** 나간 구독자는 목록에서 빠진다. 남은 사람은 계속 받는다. */
    @Test
    void goneSubscriberIsDropped() throws Exception {
        Sink a = new Sink(1);
        Sink b = new Sink(1);
        WebSocket wa = connect(a);
        WebSocket wb = connect(b);
        waitForSubscribers(2);

        wa.sendClose(WebSocket.NORMAL_CLOSURE, "먼저 나간다").get(5, TimeUnit.SECONDS);
        waitForSubscribers(1);

        hub.broadcast(StreamEvent.fill("남은 사람만"));

        assertThat(b.latch.await(5, TimeUnit.SECONDS)).isTrue();
        assertThat(hub.subscriberCount()).isEqualTo(1);

        wb.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }

    /** 많이 보내도 전파가 막히지 않는다. */
    @Test
    void manyEventsGetThrough() throws Exception {
        Sink a = new Sink(50);
        WebSocket wa = connect(a);
        waitForSubscribers(1);

        for (int i = 0; i < 50; i++) {
            hub.broadcast(StreamEvent.fill(i));
        }

        assertThat(a.latch.await(10, TimeUnit.SECONDS)).isTrue();
        assertThat(a.got).hasSizeGreaterThanOrEqualTo(50);

        wa.sendClose(WebSocket.NORMAL_CLOSURE, "끝");
    }
}
