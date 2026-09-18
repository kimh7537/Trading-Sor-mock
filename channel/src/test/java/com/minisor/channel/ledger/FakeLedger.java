package com.minisor.channel.ledger;

import com.minisor.channel.wire.BalanceAck;
import com.minisor.channel.wire.BalanceReq;
import com.minisor.channel.wire.BookAck;
import com.minisor.channel.wire.BookFeed;
import com.minisor.channel.wire.BookReq;
import com.minisor.channel.wire.CancelAck;
import com.minisor.channel.wire.CancelReq;
import com.minisor.channel.wire.DetailAck;
import com.minisor.channel.wire.DetailReq;
import com.minisor.channel.wire.OrderAck;
import com.minisor.channel.wire.OrderReq;
import com.minisor.channel.wire.WireCodec;
import com.minisor.channel.wire.WireHeader;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * 시험용 원장 상대역. 진짜 전문 규격으로 답한다 — 지어내면 시험이 아니다.
 *
 * <p>접속마다 스레드 하나를 쓴다. 이쪽은 시험 상대역이지 운영 코드가 아니다.
 *
 * <p>T7-03부터 주문을 기억한다 — 상세·취소·잔고 조회에 일관되게 답하고, 시험이 나중 체결이나 호가
 * 변화를 일으킬 수 있게 한다. 매칭은 하지 않는다(그것은 C 원장 시험의 몫이다).
 */
public final class FakeLedger implements AutoCloseable {

    /** 기억하는 주문 하나. 시장 번호를 첨자로 쓴다(0 KRX, 1 NXT). */
    private static final class FakeOrder {
        long clOrdId;
        int side;
        /** 주문할 때 고른 시장(255 SOR 포함). 상세 응답에 그대로 싣는다 — 진짜 원장처럼 */
        int requested;
        /** 실제로 보낸 시장 */
        int market;
        int price;
        int qty;
        final int[] sent = new int[2];
        final int[] filled = new int[2];
        final int[] canceled = new int[2];
        final long[] notional = new long[2];

        int total(int[] a) {
            return a[0] + a[1];
        }

        int working() {
            return qty - total(filled) - total(canceled);
        }

        int status() {
            int f = total(filled);
            if (f >= qty) {
                return 2;
            }
            if (f > 0) {
                return 1;
            }
            return working() > 0 ? 0 : 3;
        }
    }

    private final ServerSocket server;
    private final Thread acceptor;
    private final List<Socket> accepted = new CopyOnWriteArrayList<>();
    private final AtomicInteger connections = new AtomicInteger();
    private final AtomicInteger requests = new AtomicInteger();
    private final Map<Long, FakeOrder> orders = new ConcurrentHashMap<>();

    /** 답하기 전에 이만큼 쉰다. 0이면 곧바로 답한다. */
    private volatile long delayMs;

    /** 참이면 요청을 받고 답하지 않는다 — 상대가 매달리는 상황을 만든다. */
    private volatile boolean silent;

    /** 주문마다 이만큼 체결됐다고 답한다. 0이면 체결 없음. */
    private volatile int fillQty;

    /** 잔고 조회에 돌려줄 값. */
    private volatile long cash = 100_000_000L;
    private volatile long reserved;

    /** 호가 매수 1단 수량에 더하는 값 — 호가 변화를 만든다. */
    private volatile int bookBump;

    /** 마지막으로 받은 호가 스냅샷(T8-02). 받으면 그것을 호가창으로 삼아 답한다. */
    private volatile BookFeed lastFeed;
    private final AtomicInteger feeds = new AtomicInteger();

    /** 지금까지 받은 스냅샷 주입 수. */
    public int feeds() {
        return feeds.get();
    }

    public BookFeed lastFeed() {
        return lastFeed;
    }

    public FakeLedger() throws IOException {
        server = new ServerSocket(0);
        acceptor = new Thread(this::acceptLoop, "fake-ledger");
        acceptor.setDaemon(true);
        acceptor.start();
    }

    public int port() {
        return server.getLocalPort();
    }

    public int connections() {
        return connections.get();
    }

    public int requests() {
        return requests.get();
    }

    public void setDelayMs(long ms) {
        delayMs = ms;
    }

    public void setSilent(boolean v) {
        silent = v;
    }

    public void setFillQty(int q) {
        fillQty = q;
    }

    public void setBalance(long cash, long reserved) {
        this.cash = cash;
        this.reserved = reserved;
    }

    private final java.util.concurrent.atomic.AtomicInteger detailCalls =
            new java.util.concurrent.atomic.AtomicInteger();

    /** 지금까지 받은 주문 상세 요청 수 */
    public int detailCalls() {
        return detailCalls.get();
    }

    public void bumpBook(int delta) {
        bookBump += delta;
    }

    /** 걸어 둔 주문이 나중에 체결된 것처럼 만든다. 그 주문이 나간 시장에 붙는다. */
    public void fillLater(long orderId, int qty, int price) {
        FakeOrder o = orders.get(orderId);
        synchronized (o) {
            o.filled[o.market] += qty;
            o.notional[o.market] += (long) qty * price;
        }
    }

    private void acceptLoop() {
        while (!server.isClosed()) {
            try {
                Socket s = server.accept();
                accepted.add(s);
                connections.incrementAndGet();
                Thread t = new Thread(() -> serve(s), "fake-ledger-conn");
                t.setDaemon(true);
                t.start();
            } catch (IOException e) {
                return; // 닫혔다
            }
        }
    }

    private void serve(Socket s) {
        try (Socket sock = s) {
            DataInputStream in = new DataInputStream(sock.getInputStream());
            OutputStream out = sock.getOutputStream();

            while (true) {
                byte[] hbuf = new byte[WireHeader.LENGTH];
                in.readFully(hbuf);
                WireHeader h = WireHeader.decode(hbuf, 0);

                byte[] body = new byte[h.bodyLen()];
                if (h.bodyLen() > 0) {
                    in.readFully(body);
                }
                requests.incrementAndGet();

                if (delayMs > 0) {
                    Thread.sleep(delayMs);
                }
                if (silent) {
                    continue; // 받고 답하지 않는다
                }

                Object reply = answer(h.type(), body);

                byte[] ab = WireCodec.encodeBody(reply);
                WireHeader rh =
                        new WireHeader(
                                WireHeader.VERSION,
                                WireCodec.typeCode(reply.getClass()),
                                ab.length,
                                h.seq(),
                                h.ts());
                out.write(rh.encode());
                out.write(ab);
                out.flush();
            }
        } catch (IOException | InterruptedException e) {
            // 상대가 끊었거나 시험이 끝났다
        }
    }

    private Object answer(int type, byte[] body) {
        if (type == WireCodec.typeCode(BookFeed.class)) {
            return feed(WireCodec.decodeBody(BookFeed.class, body, 0, body.length));
        }
        if (type == WireCodec.typeCode(BookReq.class)) {
            return book(WireCodec.decodeBody(BookReq.class, body, 0, body.length));
        }
        if (type == WireCodec.typeCode(DetailReq.class)) {
            detailCalls.incrementAndGet();
            return detail(WireCodec.decodeBody(DetailReq.class, body, 0, body.length));
        }
        if (type == WireCodec.typeCode(CancelReq.class)) {
            return cancel(WireCodec.decodeBody(CancelReq.class, body, 0, body.length));
        }
        if (type == WireCodec.typeCode(BalanceReq.class)) {
            return balance(WireCodec.decodeBody(BalanceReq.class, body, 0, body.length));
        }
        return order(WireCodec.decodeBody(OrderReq.class, body, 0, body.length));
    }

    private OrderAck order(OrderReq req) {
        OrderAck ack = new OrderAck();
        ack.clOrdId = req.clOrdId;
        /* 어느 요청의 답인지 알아볼 수 있게 우리 번호를 실어 보낸다. */
        ack.orderId = req.clOrdId + 100000;
        ack.status = fillQty > 0 ? 1 : 0;
        ack.reason = 0;
        ack.filledQty = fillQty;
        ack.price = req.price;

        FakeOrder o = new FakeOrder();
        o.clOrdId = req.clOrdId;
        o.side = req.side;
        o.requested = req.market;
        o.market = req.market == 255 ? 1 : req.market; // SOR 자동이면 NXT로 간 것으로 둔다
        o.price = req.price;
        o.qty = req.qty;
        o.sent[o.market] = req.qty;
        o.filled[o.market] = fillQty;
        o.notional[o.market] = (long) fillQty * req.price;
        orders.put(ack.orderId, o);
        return ack;
    }

    private DetailAck detail(DetailReq req) {
        DetailAck d = new DetailAck();
        d.orderId = req.orderId;
        d.legSent = new int[2];
        d.legFilled = new int[2];
        d.legCanceled = new int[2];
        d.legNotional = new long[2];
        FakeOrder o = orders.get(req.orderId);
        if (o == null) {
            d.reason = -9;
            d.status = 4;
            return d;
        }
        synchronized (o) {
            d.clOrdId = o.clOrdId;
            d.side = o.side;
            d.status = o.status();
            d.market = o.requested;
            d.price = o.price;
            d.qty = o.qty;
            d.filled = o.total(o.filled);
            d.canceled = o.total(o.canceled);
            d.working = o.working();
            d.notional = o.notional[0] + o.notional[1];
            for (int m = 0; m < 2; m++) {
                d.legSent[m] = o.sent[m];
                d.legFilled[m] = o.filled[m];
                d.legCanceled[m] = o.canceled[m];
                d.legNotional[m] = o.notional[m];
            }
        }
        return d;
    }

    private CancelAck cancel(CancelReq req) {
        CancelAck ack = new CancelAck();
        ack.orderId = req.orderId;
        ack.clOrdId = req.clOrdId;
        FakeOrder o = orders.get(req.orderId);
        if (o == null) {
            ack.reason = -9;
            ack.status = 4;
            return ack;
        }
        synchronized (o) {
            int w = o.working();
            if (w <= 0) {
                ack.reason = -9;
                ack.status = 4;
                return ack;
            }
            o.canceled[o.market] += w;
            ack.canceledQty = w;
            ack.status = o.status();
        }
        return ack;
    }

    /**
     * 스냅샷을 받아 두고 **심은 뒤의 호가창**으로 답한다 — 진짜 원장과 같은 규약(T8-03).
     * 매칭은 하지 않는다. 그것은 C 원장 시험의 몫이다.
     */
    private BookAck feed(BookFeed f) {
        lastFeed = f;
        feeds.incrementAndGet();

        if ((f.flags & BookFeed.FEED_END) != 0) {
            /* 끝 신호는 호가창을 바꾸지 않는다 — 진짜 원장과 같은 규약 */
            BookAck keep = new BookAck();
            keep.symbol = f.symbol;
            keep.market = f.market;
            keep.bidPrice = new int[BookAck.DEPTH];
            keep.bidQty = new int[BookAck.DEPTH];
            keep.askPrice = new int[BookAck.DEPTH];
            keep.askQty = new int[BookAck.DEPTH];
            return keep;
        }

        BookAck ack = new BookAck();
        ack.symbol = f.symbol;
        ack.market = f.market;
        ack.bidPrice = f.bidPrice.clone();
        ack.bidQty = f.bidQty.clone();
        ack.askPrice = f.askPrice.clone();
        ack.askQty = f.askQty.clone();
        return ack;
    }

    private BalanceAck balance(BalanceReq req) {
        BalanceAck ack = new BalanceAck();
        ack.account = req.account;
        ack.cash = cash;
        ack.reserved = reserved;
        return ack;
    }

    /** 매수 3단(70000부터 100원씩 아래), 매도 2단(70100부터 위). 매수 수량에 시장을 섞는다. */
    private BookAck book(BookReq req) {
        BookAck ack = new BookAck();
        ack.symbol = req.symbol;
        ack.market = req.market;
        ack.bidPrice = new int[BookAck.DEPTH];
        ack.bidQty = new int[BookAck.DEPTH];
        ack.askPrice = new int[BookAck.DEPTH];
        ack.askQty = new int[BookAck.DEPTH];
        for (int i = 0; i < 3; i++) {
            ack.bidPrice[i] = 70000 - 100 * i;
            ack.bidQty[i] = 10 + i + 100 * req.market;
        }
        ack.bidQty[0] += bookBump;
        for (int i = 0; i < 2; i++) {
            ack.askPrice[i] = 70100 + 100 * i;
            ack.askQty[i] = 20 + i;
        }
        return ack;
    }

    @Override
    public void close() throws IOException {
        server.close();
        for (Socket s : accepted) {
            try {
                s.close();
            } catch (IOException ignored) {
                // 닫다 나는 오류로 할 수 있는 일이 없다
            }
        }
    }
}
