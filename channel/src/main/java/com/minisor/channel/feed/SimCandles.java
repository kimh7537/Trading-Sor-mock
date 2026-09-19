package com.minisor.channel.feed;

import com.minisor.channel.wire.BookAck;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import org.springframework.stereotype.Service;

/**
 * 시뮬 모드의 봉(OHLCV)을 만든다 (T8-09).
 *
 * <h2>왜 필요한가</h2>
 *
 * 토스의 봉은 <b>바깥 시장</b>의 체결이라 시뮬 모드의 호가창과 아무 상관이 없다. 그 둘을 한
 * 화면에 두면 위 차트와 아래 호가가 따로 놀아, 보는 사람은 같은 시장의 두 모습이라고 읽는다.
 * 시뮬에는 시뮬의 봉이 있어야 한다 — 가상 참가자끼리 실제로 체결이 일어나므로 <b>진짜
 * OHLCV</b>를 만들 수 있다.
 *
 * <h2>어디서 오나</h2>
 *
 * 원장이 호가 응답에 마지막 체결가와 누적 체결 수량을 실어 준다. 주기 조회
 * ({@code LedgerPoller})가 1초마다 읽으므로 1분봉 하나에 표본이 예순 개다. 그 사이의 체결은
 * 가격이 뭉개지지만(마지막 값만 남는다) <b>거래량은 누적값의 차이라 한 건도 빠지지 않는다.</b>
 *
 * <p>두 시장을 합쳐 하나의 봉으로 묶는다. 이 프로젝트에서 "시장 전체"는 KRX + NXT다.
 */
@Service
public class SimCandles {

    /** 봉 하나. 토스 쪽과 같은 모양이라 화면이 구분 없이 그린다. */
    public record Candle(long t, int open, int high, int low, int close, long volume) {}

    private static final long BUCKET_MS = 60_000; // 1분봉
    private static final int MAX = 180; // 세 시간치

    private static final class Bar {
        final long t;
        int open;
        int high;
        int low;
        int close;
        long volume;

        Bar(long t, int price) {
            this.t = t;
            this.open = price;
            this.high = price;
            this.low = price;
            this.close = price;
        }
    }

    private final Deque<Bar> bars = new ArrayDeque<>();
    private long lastTotal = -1;

    /**
     * 호가 응답 한 쌍(KRX·NXT)을 표본으로 받는다.
     *
     * <p>가격은 <b>더 최근에 체결된 쪽</b>을 쓴다 — 두 시장 중 방금 거래가 난 곳이 지금 값이다.
     * 없으면 표본을 버린다(아직 한 건도 체결되지 않았다).
     */
    public synchronized void sample(BookAck krx, BookAck nxt) {
        long total = (krx != null ? krx.tradedQty : 0) + (nxt != null ? nxt.tradedQty : 0);
        int price = pickPrice(krx, nxt);
        if (price <= 0) {
            lastTotal = total;
            return;
        }

        /* 첫 표본은 기준만 잡는다. 그전의 체결을 이번 봉에 몰아 넣지 않는다 */
        long delta = lastTotal < 0 ? 0 : Math.max(0, total - lastTotal);
        lastTotal = total;

        long now = System.currentTimeMillis();
        long bucket = now - (now % BUCKET_MS);

        Bar tail = bars.peekLast();
        if (tail == null || tail.t != bucket) {
            bars.addLast(new Bar(bucket, price));
            tail = bars.peekLast();
            while (bars.size() > MAX) {
                bars.removeFirst();
            }
        }
        tail.close = price;
        tail.high = Math.max(tail.high, price);
        tail.low = Math.min(tail.low, price);
        tail.volume += delta;
    }

    private static int pickPrice(BookAck krx, BookAck nxt) {
        int k = krx != null ? krx.lastPrice : 0;
        int n = nxt != null ? nxt.lastPrice : 0;
        if (k > 0 && n > 0) {
            /* 둘 다 있으면 거래가 더 많이 난 쪽을 지금 값으로 본다 */
            return (krx.tradedQty >= nxt.tradedQty) ? k : n;
        }
        return k > 0 ? k : n;
    }

    /** 오래된 것부터. 화면은 왼쪽이 과거다. */
    public synchronized List<Candle> candles(int count) {
        List<Candle> out = new ArrayList<>(bars.size());
        for (Bar b : bars) {
            out.add(new Candle(b.t, b.open, b.high, b.low, b.close, b.volume));
        }
        int from = Math.max(0, out.size() - count);
        return List.copyOf(out.subList(from, out.size()));
    }

    /** 원장을 다시 띄우면 누적값이 0으로 돌아간다. 그때 이어 붙이면 거래량이 튄다. */
    public synchronized void reset() {
        bars.clear();
        lastTotal = -1;
    }
}
