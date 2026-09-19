package com.minisor.channel.feed;

import com.minisor.channel.api.BookController;
import com.minisor.channel.api.LedgerGateway;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.stream.StreamEvent;
import com.minisor.channel.stream.StreamHub;
import com.minisor.channel.wire.BookAck;
import com.minisor.channel.wire.BookFeed;
import com.minisor.channel.wire.SymbolAck;
import com.minisor.channel.wire.SymbolSet;
import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Path;
import java.util.Map;
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

    private static final org.slf4j.Logger log = org.slf4j.LoggerFactory.getLogger(LiveFeed.class);

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
            String symbolName,
            long applied,
            long lastFeedTs,
            String note,
            /** 마지막으로 붙지 못한 이유. 붙어 있으면 null */
            String error,
            /**
             * 마지막 스냅샷을 받은 <b>벽시계</b> 시각(ms). 0이면 아직 없다.
             *
             * <p>원장의 논리 시각과 달리 "얼마나 오래 조용한가"를 재는 데 쓴다 — 장이 닫히면
             * 피드가 멈추고 호가창이 얼어붙는데, 그것을 화면이 말해 줘야 한다.
             */
            long lastFeedAt) {}

    private final LedgerGateway gateway;
    private final StreamHub hub;
    private final FeedProperties props;
    private final SymbolState symbols;
    private final SimCandles sim;

    private final AtomicLong applied = new AtomicLong();
    private final AtomicLong lastFeedTs = new AtomicLong();
    private final AtomicLong lastFeedAt = new AtomicLong();
    /* 바깥에서 받은 체결. 보여 주기만 한다 — 원장에 넣지 않는다 */
    private final AtomicLong tradeQty = new AtomicLong();
    private final AtomicLong lastTradePrice = new AtomicLong();
    private volatile Mode mode = Mode.SIM;
    private volatile String source = "sim";
    /*
     * 화면이 "켜졌다"고 표시해 놓고 아무 일도 일어나지 않는 것이 제일 나쁘다. 붙지 못한
     * 이유를 여기 담아 상태에 실어 보낸다 — 실제로 허용 IP 미등록(403)으로 조용히 멈추는
     * 상황을 겪었다.
     */
    private volatile String lastError;
    private volatile FeedFile recorder;

    public LiveFeed(
            LedgerGateway gateway,
            StreamHub hub,
            FeedProperties props,
            SymbolState symbols,
            SimCandles sim) {
        this.gateway = gateway;
        this.hub = hub;
        this.props = props;
        this.symbols = symbols;
        this.sim = sim;
    }

    /**
     * 스냅샷을 원장에 심고 그 결과를 화면에 밀어 보낸다.
     *
     * <p>원장이 답하지 않으면 {@link LedgerException}을 그대로 올린다 — 부르는 쪽(피드 루프)이
     * 다시 붙을지 그만둘지를 정한다. 여기서 삼키면 피드가 조용히 멈춘 것을 아무도 모른다.
     */
    public BookAck apply(Snapshot s) {
        BookFeed f = new BookFeed();
        f.symbol = symbols.code();
        f.market = props.market();
        f.feedTs = s.tsNanos();
        BookFeed.fill(f.bidPrice, f.bidQty, s.bids());
        BookFeed.fill(f.askPrice, f.askQty, s.asks());

        BookAck ack = gateway.call(f, BookAck.class);
        checkPlanted(f, ack);

        applied.incrementAndGet();
        lastFeedTs.set(s.tsNanos());
        lastFeedAt.set(System.currentTimeMillis());
        record(s);
        hub.broadcast(StreamEvent.book(BookController.BookDto.from(ack)));
        return ack;
    }

    /**
     * 보낸 호가가 정말 심겼는지 본다.
     *
     * <p>원장 호가창은 <b>기준가 ±30%</b>(가격 제한폭)만 펼쳐 둔다. 종목의 실제 가격이
     * 원장의 기준가와 멀면 모든 단이 그 범위 밖이라 <b>통째로 버려진다</b> — 스냅샷은
     * 계속 들어가는데 호가창은 그대로다. 실제로 그렇게 됐다(실호가 260,000원대, 원장
     * 기준가 70,000원).
     *
     * <p>응답은 심은 뒤의 호가창이므로, 보낸 최우선 가격이 그 안에 없으면 버려진 것이다.
     * 조용히 넘기지 않고 화면까지 올린다.
     */
    /** 가격대를 다시 여는 일을 이 간격 안에 한 번만 한다. 되풀이하면 원장이 계속 초기화된다. */
    private static final long REBASE_QUIET_MS = 30_000;

    private volatile long lastRebaseAt;

    private void checkPlanted(BookFeed sent, BookAck ack) {
        int want = sent.bidPrice[0] > 0 ? sent.bidPrice[0] : sent.askPrice[0];
        if (want <= 0) {
            return; /* 빈 스냅샷 — 걷어내는 것이 목적이다 */
        }
        if (has(ack.bidPrice, want) || has(ack.askPrice, want)) {
            lastError = null;
            return;
        }
        /*
         * **버려졌다면 원장을 그 가격대로 다시 연다**(T8-10).
         *
         * 예전에는 "ledgerd를 --ref-price 로 다시 띄워라"라고만 적었다. 그런데 그 상태의
         * 화면은 실시세라고 적힌 채 호가창이 텅 비어 있고, 사용자가 할 수 있는 일은 서버를
         * 손으로 다시 띄우는 것뿐이었다. 가격대를 아는 쪽이 여기이므로 여기서 고친다.
         *
         * 미체결 주문과 잔고가 초기화되지만, 애초에 이 상태에서는 아무것도 체결되지 않는다.
         * 되풀이를 막으려고 {@link #REBASE_QUIET_MS} 안에는 한 번만 시도한다.
         */
        long now = System.currentTimeMillis();
        if (now - lastRebaseAt > REBASE_QUIET_MS) {
            lastRebaseAt = now;
            try {
                SymbolSet req = new SymbolSet();
                req.symbol = symbols.code();
                req.refPrice = want;
                SymbolAck done = gateway.call(req, SymbolAck.class);
                if (done.code == 0) {
                    symbols.set(symbols.code(), symbols.current().name(), done.refPrice);
                    /*
                     * **앞의 봉을 버린다.** 가격대가 통째로 달라졌다 — 7만원대에서 만든 봉과
                     * 26만원대 봉을 한 그림에 이어 붙이면 축이 눌려 아무것도 읽을 수 없다.
                     */
                    sim.reset();
                    log.info("원장을 {}원 가격대로 다시 열었다", done.refPrice);
                    /*
                     * **다시 연 호가창에 이 스냅샷을 곧바로 심는다.**
                     *
                     * 원장을 새로 열면 "이 시장은 피드가 맡는다"는 표시가 지워져 가상 참가자가
                     * 그 시장에서 다시 주문을 낸다. 장이 닫혀 다음 스냅샷이 오지 않으면 아무도
                     * 덮어쓰지 않으므로, 화면에는 "실시세"라고 적힌 채 움직이는 것은 내 가짜
                     * 주문뿐이다 — 실제로 토요일에 그렇게 됐다. 심으면 표시가 다시 서고,
                     * 호가창은 바깥 시장이 멈춘 자리에서 함께 멈춘다.
                     */
                    BookAck replanted = gateway.call(sent, BookAck.class);
                    if (!has(replanted.bidPrice, want) && !has(replanted.askPrice, want)) {
                        noteError("가격대를 다시 열었는데도 실호가가 심기지 않는다 — " + want + "원");
                        return;
                    }
                    hub.broadcast(StreamEvent.book(BookController.BookDto.from(replanted)));
                    lastError = null;
                    return;
                }
            } catch (RuntimeException e) {
                log.warn("가격대를 맞추지 못했다: {}", e.toString());
            }
        }

        noteError("원장이 이 가격대를 받지 못한다 — 실호가 " + want + "원, 원장 기준가가 다르다");
    }

    private static boolean has(int[] prices, int want) {
        for (int p : prices) {
            if (p == want) {
                return true;
            }
        }
        return false;
    }

    /**
     * 바깥에서 받은 <b>체결</b> 하나. 호가와 달리 원장에 넣지 않는다 — 내 호가창은 내
     * 매칭 엔진이 채우고, 바깥 체결은 <b>보여 주기만</b> 한다.
     *
     * <p>넣으려 들면 "그 가격에 거래가 있었으니 내 주문도 체결"이라는 순진한 판정이 되어
     * 체결률을 부풀린다. 그것이 이 프로젝트가 피하려는 바로 그 지점이다.
     */
    public void onTrade(int price, int qty) {
        if (price <= 0 || qty <= 0) {
            return;
        }
        tradeQty.addAndGet(qty);
        lastTradePrice.set(price);
        hub.broadcast(StreamEvent.trade(Map.of("price", price, "qty", qty)));
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
            /* 더 적을 수 없다. 열어 둔 채 놓지 않는다 — 다시 켜면 새로 연다 */
            stopRecording();
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
            stopRecording();
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
        this.lastError = null;
        hub.broadcast(new StreamEvent("feed-mode", status()));
    }

    /** 붙지 못했다. 이유를 화면까지 올린다. */
    public void noteError(String why) {
        this.lastError = why;
        hub.broadcast(new StreamEvent("feed-mode", status()));
    }

    /**
     * 시뮬로 돌아간다. <b>원장에 "피드가 끝났다"를 알린다.</b>
     *
     * <p>알리지 않으면 원장은 그 시장에서 손을 뗀 채로 남아 호가창이 영영 얼어붙는다.
     * 마지막 실호가는 지우지 않는다 — 가상 참가자가 그 위에서 이어 간다.
     */
    public void enterSim() {
        boolean wasLive = mode == Mode.LIVE;
        this.source = "sim";
        this.mode = Mode.SIM;
        if (wasLive) {
            try {
                BookFeed end = new BookFeed();
                end.symbol = symbols.code();
                end.market = props.market();
                end.flags = BookFeed.FEED_END;
                gateway.call(end, BookAck.class);
            } catch (RuntimeException e) {
                /* 원장이 답하지 않아도 화면의 모드는 바꾼다. 다음 전환에서 다시 알린다 */
                lastError = "원장에 피드 종료를 알리지 못했다: " + e.getMessage();
            }
        }
        hub.broadcast(new StreamEvent("feed-mode", status()));
    }

    public Mode mode() {
        return mode;
    }

    /**
     * 실시세로 바뀌기를 잠깐 기다린다. 바뀌었으면 참, 실패했거나 시간이 다 됐으면 거짓.
     *
     * <p>토스는 <b>시작한 것과 붙은 것이 다르다.</b> 켜 달라는 요청에 시작만 하고 답하면,
     * 붙지 못하는 설정(허용 IP 미등록 등)에서는 버튼을 눌러도 아무 일이 일어나지 않는다.
     * 그래서 부르는 쪽이 짧게 기다렸다가 다른 수(녹화 재생)를 쓸 수 있게 한다.
     *
     * <p>실패가 먼저 오면 기다리지 않고 바로 돌아온다 — 대개 1초 안에 판가름 난다.
     */
    public boolean awaitLive(long ms) {
        long end = System.currentTimeMillis() + ms;
        while (System.currentTimeMillis() < end) {
            if (mode == Mode.LIVE) {
                return true;
            }
            if (lastError != null) {
                return false;
            }
            try {
                Thread.sleep(50);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                return false;
            }
        }
        return mode == Mode.LIVE;
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
                symbols.code(),
                symbols.current().name(),
                applied.get(),
                lastFeedTs.get(),
                note,
                lastError,
                lastFeedAt.get());
    }
}
