package com.minisor.channel.ledger;

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
final class FakeLedger implements AutoCloseable {

    private final ServerSocket server;
    private final Thread acceptor;
    private final List<Socket> accepted = new CopyOnWriteArrayList<>();
    private final AtomicInteger connections = new AtomicInteger();
    private final AtomicInteger requests = new AtomicInteger();

    /** 답하기 전에 이만큼 쉰다. 0이면 곧바로 답한다. */
    private volatile long delayMs;

    /** 참이면 요청을 받고 답하지 않는다 — 상대가 매달리는 상황을 만든다. */
    private volatile boolean silent;

    FakeLedger() throws IOException {
        server = new ServerSocket(0);
        acceptor = new Thread(this::acceptLoop, "fake-ledger");
        acceptor.setDaemon(true);
        acceptor.start();
    }

    int port() {
        return server.getLocalPort();
    }

    int connections() {
        return connections.get();
    }

    int requests() {
        return requests.get();
    }

    void setDelayMs(long ms) {
        delayMs = ms;
    }

    void setSilent(boolean v) {
        silent = v;
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

                OrderReq req =
                        WireCodec.decodeBody(OrderReq.class, body, 0, body.length);

                OrderAck ack = new OrderAck();
                ack.clOrdId = req.clOrdId;
                /* 어느 요청의 답인지 알아볼 수 있게 우리 번호를 실어 보낸다. */
                ack.orderId = req.clOrdId + 100000;
                ack.status = 0;
                ack.reason = 0;
                ack.filledQty = 0;
                ack.price = req.price;

                byte[] ab = WireCodec.encodeBody(ack);
                WireHeader rh =
                        new WireHeader(
                                WireHeader.VERSION,
                                WireCodec.typeCode(OrderAck.class),
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
