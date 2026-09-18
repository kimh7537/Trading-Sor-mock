package com.minisor.channel.feed;

import com.minisor.channel.api.BookController;
import com.minisor.channel.api.LedgerGateway;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.stream.StreamEvent;
import com.minisor.channel.stream.StreamHub;
import com.minisor.channel.wire.BookAck;
import com.minisor.channel.wire.BookFeed;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Path;
import java.util.concurrent.atomic.AtomicLong;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.context.event.ApplicationReadyEvent;
import org.springframework.context.event.EventListener;
import org.springframework.stereotype.Service;

/**
 * 스냅샷 하나를 원장 호가창에 심는다 (T8-04).
 *
 * <p><b>피드가 어디서 오는지 모른다.</b> 토스 WebSocket({@link TossFeedClient})이든 녹화 파일
 * 재생({@link FeedReplayer})이든 시험의 가짜 피드든, 여기로 {@link Snapshot} 하나를 주면 된다.
 * 그 덕에 시험이 소켓 없이 전체 경로를 돈다.
 *
 * <p><b>주문 API는 부르지 않는다.</b> 이 계층이 바깥에 하는 일은 읽기뿐이고, 주문은 전부 내
 * 원장에서 끝난다. 화면에도 그렇게 적는다(T8-05).
 *
 * <p>심은 결과({@code MSG_BOOK_ACK})를 그대로 화면에 밀어 보낸다. 주기 조회
 * ({@code LedgerPoller})를 기다리면 실시세의 즉각성이 1초 늦는다.
 */
@Service
public class LiveFeed {

    /** 지금 호가창을 무엇이 움직이고 있는가. */
    public enum Mode {
        /** 가상 참가자(시드 유동성 + {@code ledgerd --live} 틱). */
        SIM,
        /** 바깥에서 받은 실호가. */
        LIVE
    }

    /** 화면이 보는 상태. {@code available}이 거짓이면 실시세로 바꿀 수 없다. */
    public record Status(
            String mode,
            String source,
            boolean available,
            int market,
            String symbol,
            long applied,
            long lastFeedTs,
            String note) {}

    private final LedgerGateway gateway;
    private final StreamHub hub;
    private final FeedProperties props;
    private final String symbol;

    private final AtomicLong applied = new AtomicLong();
    private final AtomicLong lastFeedTs = new AtomicLong();
    private volatile Mode mode = Mode.SIM;
    private volatile String source = "sim";
    private volatile FeedFile recorder;

    public LiveFeed(
            LedgerGateway gateway,
            StreamHub hub,
            FeedProperties props,
            @Value("${minisor.symbol:005930}") String symbol) {
        this.gateway = gateway;
        this.hub = hub;
        this.props = props;
        this.symbol = symbol;
    }

    /**
     * 스냅샷을 원장에 심고 그 결과를 화면에 밀어 보낸다.
     *
     * <p>원장이 답하지 않으면 {@link LedgerException}을 그대로 올린다 — 부르는 쪽(피드 루프)이
     * 다시 붙을지 그만둘지를 정한다. 여기서 삼키면 피드가 조용히 멈춘 것을 아무도 모른다.
     */
    public BookAck apply(Snapshot s) {
        BookFeed f = new BookFeed();
        f.symbol = symbol;
        f.market = props.market();
        f.feedTs = s.tsNanos();
        BookFeed.fill(f.bidPrice, f.bidQty, s.bids());
        BookFeed.fill(f.askPrice, f.askQty, s.asks());

        BookAck ack = gateway.call(f, BookAck.class);

        applied.incrementAndGet();
        lastFeedTs.set(s.tsNanos());
        record(s);
        hub.broadcast(StreamEvent.book(BookController.BookDto.from(ack)));
        return ack;
    }

    /** 녹화 중이면 적는다. 적다 실패해도 피드를 멈추지 않는다 — 녹화는 곁다리다. */
    private void record(Snapshot s) {
        FeedFile f = recorder;
        if (f == null) {
            return;
        }
        try {
            f.write(s);
        } catch (UncheckedIOException e) {
            recorder = null;
        }
    }

    /** 설정에 녹화 파일이 있으면 뜨자마자 연다. 없으면 아무 일도 하지 않는다(T8-06). */
    @EventListener(ApplicationReadyEvent.class)
    void autoRecord() {
        if (!props.recording()) {
            return;
        }
        try {
            startRecording(Path.of(props.recordFile()));
        } catch (IOException | RuntimeException e) {
            /* 녹화는 곁다리다. 못 열어도 시세 수신은 돈다 */
            recorder = null;
        }
    }

    /** 녹화를 시작한다. 이미 하고 있으면 그대로 둔다. */
    public synchronized void startRecording(Path path) throws IOException {
        if (recorder == null) {
            recorder = new FeedFile(path);
        }
    }

    public synchronized void stopRecording() {
        FeedFile f = recorder;
        recorder = null;
        if (f != null) {
            try {
                f.close();
            } catch (IOException ignored) {
                // 닫다 나는 오류로 할 수 있는 일이 없다
            }
        }
    }

    /** 실시세를 밀어 넣는 쪽이 자기 이름과 함께 켠다("toss", "replay"). */
    public void enterLive(String source) {
        this.source = source;
        this.mode = Mode.LIVE;
        hub.broadcast(new StreamEvent("feed-mode", status()));
    }

    public void enterSim() {
        this.source = "sim";
        this.mode = Mode.SIM;
        hub.broadcast(new StreamEvent("feed-mode", status()));
    }

    public Mode mode() {
        return mode;
    }

    public Status status() {
        boolean canToss = props.usable();
        boolean canReplay = props.replaying();
        String note =
                canToss || canReplay
                        ? "시세는 실제, 주문은 모의"
                        : "실시세 설정이 없다(.env의 TOSS_* 또는 minisor.feed.replay-file)";
        return new Status(
                mode == Mode.LIVE ? "live" : "sim",
                source,
                canToss || canReplay,
                props.market(),
                symbol,
                applied.get(),
                lastFeedTs.get(),
                note);
    }
}
