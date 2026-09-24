package com.minisor.channel.feed;

import com.minisor.channel.api.LedgerGateway;
import com.minisor.channel.stream.StreamEvent;
import com.minisor.channel.stream.StreamHub;
import com.minisor.channel.wire.SymbolAck;
import com.minisor.channel.wire.SymbolSet;
import java.io.IOException;
import java.util.List;
import java.util.regex.Pattern;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.context.event.ApplicationReadyEvent;
import org.springframework.context.event.EventListener;
import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.stereotype.Service;

/**
 * 종목을 바꾼다 (T8-10).
 *
 * <h2>왜 기준가를 같이 보내나</h2>
 *
 * 원장 호가창은 만들 때 정한 <b>기준가 ±30%</b>(가격 제한폭, {@code docs/SPEC.md} 3.1)만
 * 펼쳐 둔다. 89,000원짜리 호가창에 260,000원 호가를 심으면 전 단이 범위 밖이라 통째로
 * 버려진다 — 스냅샷은 계속 들어가는데 화면은 그대로다. 실제로 그렇게 됐다. 그래서
 * 종목을 바꿀 때 <b>그 종목의 현재가</b>를 함께 보내고, 원장은 그 가격대로 호가창을 새로 연다.
 *
 * <h2>바꾸면 원장이 초기화된다</h2>
 *
 * 미체결 주문과 잔고가 처음으로 돌아간다. 이 원장은 한 종목짜리이고, 앞 종목의 주문을
 * 다른 종목의 호가창에 남겨 둘 자리가 없다. 화면에도 그렇게 적는다.
 *
 * <h2>국내 보통주만</h2>
 *
 * 호가 단위·가격 제한폭이 한국 시장 규칙이고 가격이 원 단위 정수다. 달러로 매겨진 해외
 * 종목은 {@link TossStocks}의 목록에 아예 들어 있지 않다.
 */
@Service
public class SymbolService {

    private final UsStocks us = new UsStocks();

    private static final Logger log = LoggerFactory.getLogger(SymbolService.class);

    /** 원장 전문의 종목 칸은 8바이트다. 그 밖의 글자는 보내기 전에 막는다. */
    private static final Pattern CODE = Pattern.compile("[0-9A-Za-z._-]{1,8}");

    private final LedgerGateway gateway;
    private final TossStocks stocks;
    private final TossCandles candles;
    private final SimCandles sim;
    private final SymbolState state;
    private final LiveFeed live;
    private final TossFeedClient client;
    private final StreamHub hub;
    private final boolean syncOnStart;

    public SymbolService(
            LedgerGateway gateway,
            TossStocks stocks,
            TossCandles candles,
            SimCandles sim,
            SymbolState state,
            LiveFeed live,
            TossFeedClient client,
            StreamHub hub,
            @Value("${minisor.symbol.sync-on-start:true}") boolean syncOnStart) {
        this.gateway = gateway;
        this.stocks = stocks;
        this.candles = candles;
        this.sim = sim;
        this.state = state;
        this.live = live;
        this.client = client;
        this.hub = hub;
        this.syncOnStart = syncOnStart;
    }

    /**
     * 뜨자마자 <b>원장이 무엇을 다루고 있는지</b> 물어 맞춘다.
     *
     * <p>채널계가 다시 뜨면 설정에 적힌 종목(보통 005930)을 들고 시작한다. 그사이 원장은
     * 다른 종목으로 바뀌어 있을 수 있고, 그러면 모든 호가 조회가 빈 호가창을 돌려준다 —
     * 원장은 자기 종목에만 답한다. 기준가 0으로 보내면 바꾸지 않고 답만 온다.
     *
     * <p><b>이름보다 코드가 먼저다.</b> 이름은 바깥에서 받아 오는 것이라 몇 초가 걸리는데,
     * 그사이 화면이 읽으면 옛 종목과 빈 호가창을 본다. 코드를 먼저 세우고 이름은 나중에 채운다.
     *
     * <p>실시세 클라이언트보다 먼저 돌아야 한다({@link Order}) — 뒤면 옛 종목을 구독한다.
     *
     * <p>테스트는 끈다({@code minisor.symbol.sync-on-start=false}) — 원장이 없는 테스트에서
     * 이 물음이 실패하면 게이트웨이가 "원장이 끊겼다"를 방송해, 방송을 세는 테스트가 깨진다.
     */
    @Order(Ordered.HIGHEST_PRECEDENCE)
    @EventListener(ApplicationReadyEvent.class)
    void syncFromLedger() {
        if (!syncOnStart) {
            return;
        }
        try {
            SymbolSet ask = new SymbolSet();
            ask.symbol = state.code();
            ask.refPrice = 0;
            SymbolAck now = gateway.call(ask, SymbolAck.class);
            if (now.symbol == null || now.symbol.isBlank() || now.symbol.equals(state.code())) {
                state.set(state.code(), state.current().name(), now.refPrice);
                return;
            }
            /* 코드를 먼저 세운다 — 이름을 받는 동안에도 호가 조회가 맞는 종목을 본다 */
            state.set(now.symbol, now.symbol, now.refPrice);
            String name = nameOf(now.symbol);
            state.set(now.symbol, name, now.refPrice);
            log.info("원장이 들고 있던 종목에 맞춘다: {} {} 기준가 {}원", now.symbol, name, now.refPrice);
        } catch (RuntimeException e) {
            log.warn("원장의 종목을 묻지 못했다: {}", e.toString());
        }
    }

    /** 이름은 바깥에서만 온다. 못 받으면 코드를 그대로 쓴다 — 화면이 비지 않게. */
    private String nameOf(String code) {
        if (!stocks.usable()) {
            return code;
        }
        try {
            TossStocks.Stock s = stocks.find(code);
            return s != null ? s.name() : code;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return code;
        } catch (IOException | RuntimeException e) {
            return code;
        }
    }

    /** 바꾸지 못하는 이유. 화면에 그대로 보여 준다. */
    public static class SwitchFailed extends RuntimeException {
        public SwitchFailed(String why) {
            super(why);
        }
    }

    public boolean usable() {
        return stocks.usable();
    }

    /**
     * 이름·코드로 찾는다. 국내(토스)와 미국(내장 목록)을 함께 돌려준다.
     *
     * <p>국내 쪽은 실시세 설정이 없으면 빈 목록이다. 그때도 미국 종목은 고를 수 있다 —
     * 미국은 애초에 시세를 받아 오지 않기 때문이다.
     */
    public List<TossStocks.Stock> search(String q) throws IOException, InterruptedException {
        List<TossStocks.Stock> out = new java.util.ArrayList<>();
        for (UsStocks.Stock s : us.search(q)) {
            out.add(new TossStocks.Stock(s.symbol(), s.name(), "US"));
        }
        if (stocks.usable()) {
            out.addAll(stocks.search(q));
        }
        return out;
    }

    /**
     * 종목을 바꾼다. 성공하면 바뀐 상태를 돌려준다.
     *
     * <p>순서가 중요하다 — <b>원장이 받아들인 뒤에</b> 이쪽 상태를 바꾼다. 먼저 바꾸면
     * 원장은 옛 종목인데 채널계는 새 종목이라, 모든 호가 조회가 빈 호가창을 돌려준다.
     */
    public SymbolState.Current switchTo(String code) throws IOException, InterruptedException {
        if (code == null) {
            throw new SwitchFailed("종목 코드가 올바르지 않다");
        }
        if (code.equalsIgnoreCase(state.code())) {
            return state.current();
        }

        /*
         * 미국 종목은 실시세 원천이 없다(T10-02). 토스 Open API의 구독 토픽이
         * orderbook:kr·trade:kr로 국내만 주기 때문이다. 그래서 내장 목록의
         * **시작 가격**으로 호가창을 열고 그다음은 가상 참가자가 움직인다.
         */
        UsStocks.Stock usHit = UsStocks.looksUs(code) ? us.find(code) : null;
        if (UsStocks.looksUs(code) && usHit == null) {
            throw new SwitchFailed("미국 종목 목록에 없다: " + code);
        }

        String name;
        int ref;
        int kind;
        if (usHit != null) {
            name = usHit.name();
            ref = usHit.seedCents();
            kind = SymbolState.Current.US;
            code = usHit.symbol();
        } else {
            if (!CODE.matcher(code).matches()) {
                throw new SwitchFailed("종목 코드가 올바르지 않다");
            }
            if (!stocks.usable()) {
                throw new SwitchFailed(
                        "실시세 설정이 없으면 국내 종목을 바꿀 수 없다 — 그 종목의 가격대를 알 길이 없다");
            }
            TossStocks.Stock found = stocks.find(code);
            if (found == null) {
                throw new SwitchFailed("국내 보통주 목록에 없는 종목이다: " + code);
            }
            name = found.name();
            ref = refPrice(code);
            kind = SymbolState.Current.KR;
            if (ref <= 0) {
                throw new SwitchFailed("현재가를 받지 못해 호가창을 열 수 없다: " + code);
            }
        }

        SymbolSet req = new SymbolSet();
        req.symbol = code;
        req.refPrice = ref;
        req.kind = kind;
        SymbolAck ack = gateway.call(req, SymbolAck.class);
        if (ack.code != 0) {
            throw new SwitchFailed("원장이 종목 전환을 거절했다 (코드 " + ack.code + ")");
        }

        state.set(code, name, ack.refPrice, ack.kind);
        /* 앞 종목의 봉을 새 종목의 차트에 섞지 않는다 */
        candles.clear();
        sim.reset();
        /* 실시세라면 새 종목으로 다시 구독한다. 시뮬이면 가상 참가자가 새 가격대에서 돈다 */
        if (live.mode() == LiveFeed.Mode.LIVE && !state.us()) {
            client.resubscribe();
        }

        log.info("종목 전환: {} {} 기준가 {}{}", code, name, ack.refPrice,
                state.us() ? "센트" : "원");
        hub.broadcast(new StreamEvent("symbol", state.current()));
        return state.current();
    }

    /**
     * 호가창을 열 가격대의 중심.
     *
     * <p>장이 열리기 전에는 현재가가 없을 수 있다. 그때는 <b>일봉의 종가</b>로 물러선다 —
     * 어제 종가는 오늘 가격 제한폭의 기준이기도 하다.
     */
    private int refPrice(String code) throws IOException, InterruptedException {
        int now = stocks.price(code);
        if (now > 0) {
            return now;
        }
        try {
            List<TossCandles.Candle> bars = candles.fetch(code, "1d", 1).candles();
            return bars.isEmpty() ? 0 : bars.get(bars.size() - 1).close();
        } catch (IOException | RuntimeException e) {
            log.warn("일봉으로 기준가를 찾지 못했다 {}: {}", code, e.toString());
            return 0;
        }
    }
}
