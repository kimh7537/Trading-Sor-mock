package com.minisor.channel.ledger;

import com.minisor.channel.wire.BookAck;
import com.minisor.channel.wire.BookReq;
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
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * 시험용 원장 상대역. 진짜 전문 규격으로 답한다 — 지어내면 시험이 아니다.
 *
 * <p>접속마다 스레드 하나를 쓴다. 이쪽은 시험 상대역이지 운영 코드가 아니다.
 */
public final class FakeLedger implements AutoCloseable {

    private final ServerSocket server;
    private final Thread acceptor;
    private final List<Socket> accepted = new CopyOnWriteArrayList<>();
    private final AtomicInteger connections = new AtomicInteger();
    private final AtomicInteger requests = new AtomicInteger();

    /** 답하기 전에 이만큼 쉰다. 0이면 곧바로 답한다. */
    private volatile long delayMs;

    /** 참이면 요청을 받고 답하지 않는다 — 상대가 매달리는 상황을 만든다. */
    private volatile boolean silent;

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

    /** 주문마다 이만큼 체결됐다고 답한다. 0이면 체결 없음. */
    private volatile int fillQty;

    public void setFillQty(int q) {
        fillQty = q;
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

                Object reply =
                        h.type() == WireCodec.typeCode(BookReq.class)
                                ? book(WireCodec.decodeBody(BookReq.class, body, 0, body.length))
                                : order(WireCodec.decodeBody(OrderReq.class, body, 0, body.length));

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

    private OrderAck order(OrderReq req) {
        OrderAck ack = new OrderAck();
        ack.clOrdId = req.clOrdId;
        /* 어느 요청의 답인지 알아볼 수 있게 우리 번호를 실어 보낸다. */
        ack.orderId = req.clOrdId + 100000;
        ack.status = fillQty > 0 ? 1 : 0;
        ack.reason = 0;
        ack.filledQty = fillQty;
        ack.price = req.price;
        return ack;
    }

    /** 매수 3단(70000부터 100원씩 아래), 매도 2단(70100부터 위). 매수 수량에 시장을 섞는다. */
    private static BookAck book(BookReq req) {
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
