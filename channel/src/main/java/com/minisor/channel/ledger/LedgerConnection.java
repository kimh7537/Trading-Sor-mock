package com.minisor.channel.ledger;

import com.minisor.channel.wire.WireCodec;
import com.minisor.channel.wire.WireException;
import com.minisor.channel.wire.WireHeader;
import java.io.DataInputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;

/**
 * 원장(C)에 붙은 접속 하나.
 *
 * <p><b>요청 하나에 응답 하나.</b> 블로킹으로 단순하게 짠다 — 이 계층에서
 * 이벤트 루프가 벌 것이 없다. 동시성은 접속을 여럿 두는 것으로 얻는다
 * (FEP가 한 접속으로 여러 주문을 다루느라 논블로킹이어야 했던 것과 다르다).
 *
 * <p><b>이 객체는 한 번에 한 스레드만 쓴다.</b> 풀이 그것을 보장한다 — 빌려 간
 * 사람만 쓰고, 돌려주기 전까지 다른 사람에게 가지 않는다.
 */
public final class LedgerConnection implements AutoCloseable {

    private final Socket socket;
    private final DataInputStream in;
    private final OutputStream out;

    /**
     * 이 접속으로 보낸 전문의 차례. <b>접속마다 따로 센다</b> — 풀의 접속들은
     * 서로 바꿔 쓸 수 있어야 하고, 번호를 공유하면 그 자유가 사라진다.
     */
    private long outSeq = 1;

    /** 한 번이라도 어긋났으면 다시 쓰지 않는다. */
    private boolean broken;

    LedgerConnection(String host, int port, int connectTimeoutMs, int readTimeoutMs)
            throws IOException {
        socket = new Socket();
        socket.connect(new InetSocketAddress(host, port), connectTimeoutMs);
        socket.setSoTimeout(readTimeoutMs);
        /*
         * 네이글을 끈다. 이 구간은 작은 전문을 주고받는 요청-응답이라,
         * 묶어 보내려고 기다리는 것이 그대로 주문 지연이 된다.
         */
        socket.setTcpNoDelay(true);

        in = new DataInputStream(socket.getInputStream());
        out = socket.getOutputStream();
    }

    public boolean isBroken() {
        return broken || socket.isClosed() || !socket.isConnected();
    }

    /**
     * 요청을 보내고 응답 하나를 받는다.
     *
     * <p>응답이 제때 오지 않거나 스트림이 어긋나면 <b>이 접속을 버린다.</b>
     * 원장이 처리했는지 안 했는지 모르는 상태이고, 같은 접속을 계속 쓰면
     * <b>응답이 한 칸씩 밀려</b> 다음 요청이 앞 요청의 답을 받는다.
     */
    public <T> T call(Object request, Class<T> responseType, long ts) {
        if (isBroken()) {
            throw new LedgerException("이미 버려진 접속이다");
        }
        try {
            byte[] body = WireCodec.encodeBody(request);
            WireHeader h =
                    new WireHeader(
                            WireHeader.VERSION,
                            WireCodec.typeCode(request.getClass()),
                            body.length,
                            outSeq++,
                            ts);

            /*
             * 헤더와 바디를 한 번에 쓴다. 나눠 쓰면 그 사이에 실패했을 때
             * 반쪽 전문이 상대에게 남는다.
             */
            byte[] frame = new byte[WireHeader.LENGTH + body.length];
            System.arraycopy(h.encode(), 0, frame, 0, WireHeader.LENGTH);
            System.arraycopy(body, 0, frame, WireHeader.LENGTH, body.length);
            out.write(frame);
            out.flush();

            byte[] hbuf = new byte[WireHeader.LENGTH];
            in.readFully(hbuf);
            WireHeader rh = WireHeader.decode(hbuf, 0);

            byte[] rbody = new byte[rh.bodyLen()];
            if (rh.bodyLen() > 0) {
                in.readFully(rbody);
            }

            int want = WireCodec.typeCode(responseType);
            if (rh.type() != want) {
                /*
                 * 기다린 종별이 아니다. 요청과 응답이 어긋났다는 뜻이라
                 * 이 접속은 더 믿을 수 없다.
                 */
                broken = true;
                throw new LedgerException(
                        "응답 종별이 다르다: " + rh.type() + " (기다린 것은 " + want + ")");
            }
            return WireCodec.decodeBody(responseType, rbody, 0, rh.bodyLen());

        } catch (IOException | WireException e) {
            broken = true;
            throw new LedgerException("원장 요청 실패: " + e, e);
        }
    }

    @Override
    public void close() {
        try {
            socket.close();
        } catch (IOException ignored) {
            // 닫다 나는 오류로 할 수 있는 일이 없다
        }
    }
}
