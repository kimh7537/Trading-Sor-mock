package com.minisor.channel.feed;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.ledger.FakeLedger;
import com.minisor.channel.stream.StreamHub;
import com.minisor.channel.wire.BookAck;
import com.minisor.channel.wire.BookFeed;
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
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

/**
 * T8-04 — 받은 스냅샷이 원장 호가창까지 간다.
 *
 * <p><b>가짜 피드로 시험한다.</b> 토스에 실제로 붙는 시험은 장 시간과 허용 IP에 기대므로
 * 재현되지 않는다. 그래서 {@link LiveFeed#apply}에 스냅샷을 직접 넣고, 원장 상대역이 받은
 * 전문을 바이트 단위로 확인한다 — 소켓과 코덱을 포함한 전체 경로가 돈다.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class LiveFeedTest {

    private static FakeLedger ledger;

    @DynamicPropertySource
    static void props(DynamicPropertyRegistry reg) throws Exception {
        ledger = new FakeLedger();
        reg.add("minisor.ledger.port", ledger::port);
        reg.add("minisor.poller.enabled", () -> false);
        reg.add("minisor.feed.enabled", () -> false);
    }

    @LocalServerPort private int port;
    @Autowired private LiveFeed live;
    @Autowired private StreamHub hub;

    private static final class Sink implements WebSocket.Listener {
        final List<String> got = new CopyOnWriteArrayList<>();

        @Override
        public CompletionStage<?> onText(WebSocket ws, CharSequence data, boolean last) {
            got.add(data.toString());
            ws.request(1);
            return null;
        }
    }

    private static Snapshot snap(long ts, int[][] bids, int[][] asks) {
        return new Snapshot("005930", ts, List.of(bids), List.of(asks));
    }

    /** 스냅샷이 {@code MSG_BOOK_FEED}가 되어 원장에 닿는다. 모자란 단은 0으로 채운다. */
    @Test
    void snapshotReachesLedgerAsBookFeed() {
        int before = ledger.feeds();
        BookAck ack =
                live.apply(
                        snap(
                                32_400_000_000_000L,
                                new int[][] {{69900, 12}, {69800, 5}},
                                new int[][] {{70000, 7}}));

        assertThat(ledger.feeds()).isEqualTo(before + 1);
        BookFeed sent = ledger.lastFeed();
        assertThat(sent.symbol).isEqualTo("005930");
        assertThat(sent.market).isZero();
        assertThat(sent.feedTs).isEqualTo(32_400_000_000_000L);
        assertThat(sent.bidPrice[0]).isEqualTo(69900);
        assertThat(sent.bidQty[0]).isEqualTo(12);
        assertThat(sent.bidPrice[1]).isEqualTo(69800);
        assertThat(sent.bidPrice[2]).isZero(); /* 없는 단 */
        assertThat(sent.askPrice[0]).isEqualTo(70000);

        /* 응답은 심은 뒤의 호가창이다 */
        assertThat(ack.bidPrice[0]).isEqualTo(69900);
    }

    /** 심은 결과를 곧바로 화면에 밀어 보낸다 — 주기 조회를 1초 기다리지 않는다. */
    @Test
    void broadcastsResultingBook() throws Exception {
        Sink sink = new Sink();
        HttpClient.newHttpClient()
                .newWebSocketBuilder()
                .buildAsync(URI.create("ws://127.0.0.1:" + port + "/ws/stream"), sink)
                .get(5, TimeUnit.SECONDS);
        for (int i = 0; i < 100 && hub.subscriberCount() == 0; i++) {
            Thread.sleep(20);
        }
        sink.got.clear();

        live.apply(snap(1000, new int[][] {{69500, 3}}, new int[][] {{69600, 4}}));
        Thread.sleep(250);

        List<String> books = sink.got.stream().filter(m -> m.contains("\"kind\":\"book\"")).toList();
        assertThat(books).hasSize(1);
        assertThat(books.get(0)).contains("69500").contains("69600");
    }

    /**
     * 실시세 설정이 없으면 <b>실시세로 바꿀 수 없다.</b> 화면이 "켜졌다"고 표시해 놓고 아무 일도
     * 일어나지 않는 것이 제일 나쁘다.
     */
    @Test
    void modeToggleRefusesWithoutASource() throws Exception {
        HttpClient http = HttpClient.newHttpClient();
        HttpResponse<String> before =
                http.send(
                        HttpRequest.newBuilder(
                                        URI.create("http://127.0.0.1:" + port + "/api/feed"))
                                .timeout(Duration.ofSeconds(5))
                                .build(),
                        HttpResponse.BodyHandlers.ofString());
        assertThat(before.statusCode()).isEqualTo(200);
        assertThat(before.body()).contains("\"mode\":\"sim\"").contains("\"available\":false");

        HttpResponse<String> res =
                http.send(
                        HttpRequest.newBuilder(
                                        URI.create(
                                                "http://127.0.0.1:"
                                                        + port
                                                        + "/api/feed/mode?mode=live"))
                                .timeout(Duration.ofSeconds(5))
                                .POST(HttpRequest.BodyPublishers.noBody())
                                .build(),
                        HttpResponse.BodyHandlers.ofString());
        assertThat(res.statusCode()).isEqualTo(409);
        assertThat(res.body()).contains("\"mode\":\"sim\"");
    }

    /** 모드가 바뀌면 화면에 알린다. "시세는 실제, 주문은 모의"라는 문구를 함께 싣는다. */
    @Test
    void modeChangeIsAnnounced() {
        live.enterLive("replay");
        assertThat(live.status().mode()).isEqualTo("live");
        assertThat(live.status().source()).isEqualTo("replay");
        live.enterSim();
        assertThat(live.status().mode()).isEqualTo("sim");
    }
}
