package com.minisor.channel.feed;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.ledger.FakeLedger;
import com.minisor.channel.wire.BookFeed;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

/**
 * T8-06 — 녹화한 장을 다시 재생한다.
 *
 * <p>완료 조건이 둘이다. <b>같은 파일은 같은 결과를 만들고</b>, <b>배속을 줄 수 있다.</b>
 * 배속은 부르는 간격만 바꾼다 — 순서와 내용은 그대로여야 한다. 그렇지 않으면 "재생"이
 * 아니라 다른 장이다.
 */
@SpringBootTest
class FeedReplayTest {

    private static FakeLedger ledger;

    @DynamicPropertySource
    static void props(DynamicPropertyRegistry reg) throws Exception {
        ledger = new FakeLedger();
        reg.add("minisor.ledger.port", ledger::port);
        reg.add("minisor.poller.enabled", () -> false);
        reg.add("minisor.feed.enabled", () -> false);
    }

    @Autowired private LiveFeed live;

    private static Snapshot snap(long ts, int bid, int qty) {
        return new Snapshot(
                "005930", ts, List.of(new int[] {bid, qty}), List.of(new int[] {bid + 100, qty}));
    }

    /** 적은 대로 다시 읽힌다. 가격·잔량이 정수 쌍으로 남는다. */
    @Test
    void roundTripsThroughTheFile(@TempDir Path dir) throws IOException {
        Path f = dir.resolve("tape.jsonl");
        try (FeedFile out = new FeedFile(f)) {
            out.write(snap(1_000_000_000L, 69900, 12));
            out.write(snap(2_000_000_000L, 69800, 5));
        }

        List<Snapshot> back = FeedFile.read(f);
        assertThat(back).hasSize(2);
        assertThat(back.get(0).tsNanos()).isEqualTo(1_000_000_000L);
        assertThat(back.get(0).bids().get(0)).containsExactly(69900, 12);
        assertThat(back.get(1).asks().get(0)).containsExactly(69900, 5);
    }

    /** 녹화가 중간에 끊겨 마지막 줄이 반쪽이어도 앞의 장은 읽힌다. */
    @Test
    void skipsBrokenLines(@TempDir Path dir) throws IOException {
        Path f = dir.resolve("torn.jsonl");
        Files.writeString(f, FeedFile.toJson(snap(1, 69900, 1)) + "\n{\"ts\":2,\"bid");
        assertThat(FeedFile.read(f)).hasSize(1);
    }

    /** 같은 파일을 두 번 재생하면 원장이 받는 전문이 같다 — 결정성. */
    @Test
    void sameFileSameFeeds(@TempDir Path dir) throws Exception {
        Path f = dir.resolve("tape.jsonl");
        try (FeedFile out = new FeedFile(f)) {
            for (int i = 0; i < 5; i++) {
                out.write(snap(1_000_000L * i, 69900 - i * 100, 10 + i));
            }
        }

        List<String> first = replay(f);
        List<String> second = replay(f);
        assertThat(first).hasSize(5).isEqualTo(second);
        assertThat(first.get(0)).contains("69900");
    }

    /** 배속은 간격만 바꾼다. 시각 차이가 커도 상한을 둬서 재생이 몇 분씩 멈추지 않는다. */
    @Test
    void speedOnlyChangesTheGap() {
        long oneSecond = 1_000_000_000L;
        assertThat(FeedReplayer.gapMs(0, oneSecond, 1)).isEqualTo(1000);
        assertThat(FeedReplayer.gapMs(0, oneSecond, 10)).isEqualTo(100);
        /* 거꾸로 가는 시각은 기다리지 않는다 */
        assertThat(FeedReplayer.gapMs(oneSecond, 0, 1)).isZero();
        /* 장 시작 전 공백 같은 큰 구멍은 상한으로 접는다 */
        assertThat(FeedReplayer.gapMs(0, 600 * oneSecond, 1)).isEqualTo(2000);
    }

    /** 파일을 그대로 원장에 흘려 넣고, 원장이 받은 매수 1단 가격을 순서대로 모은다. */
    private List<String> replay(Path f) throws IOException {
        List<String> got = new ArrayList<>();
        for (Snapshot s : FeedFile.read(f)) {
            live.apply(s);
            BookFeed sent = ledger.lastFeed();
            got.add(sent.feedTs + ":" + sent.bidPrice[0] + "x" + sent.bidQty[0]);
        }
        return got;
    }
}
