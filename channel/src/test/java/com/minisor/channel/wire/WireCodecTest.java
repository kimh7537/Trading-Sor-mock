package com.minisor.channel.wire;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.jupiter.api.Test;

/**
 * T4-02 완료 조건을 그대로 옮긴다.
 *
 * <ol>
 *   <li>선언만 보고 왕복한다
 *   <li>C와 같은 바이트를 만든다 (빅엔디언, 채움 없음, 고정 길이 문자열)
 *   <li>길이가 규격과 다르면 디코딩하지 않는다
 *   <li>헤더의 magic·version이 틀리면 해석하지 않는다
 * </ol>
 */
class WireCodecTest {

    @Test
    void orderReqRoundTrip() {
        OrderReq in = new OrderReq();
        in.account = "123456789012";
        in.symbol = "005930";
        in.clOrdId = Long.MAX_VALUE;
        in.side = 1;
        in.type = 1;
        in.market = 2;
        in.price = 70000;
        in.qty = 10;

        byte[] body = WireCodec.encodeBody(in);
        assertThat(body).hasSize(39); // C의 MSG_ORDER_REQ_LEN

        OrderReq out = WireCodec.decodeBody(OrderReq.class, body, 0, body.length);
        assertThat(out.account).isEqualTo("123456789012");
        assertThat(out.symbol).isEqualTo("005930");
        assertThat(out.clOrdId).isEqualTo(Long.MAX_VALUE);
        assertThat(out.side).isEqualTo(1);
        assertThat(out.market).isEqualTo(2);
        assertThat(out.price).isEqualTo(70000);
        assertThat(out.qty).isEqualTo(10);
    }

    /** 음수와 최소·최대가 그대로 돌아온다. i32를 u32로 읽으면 여기서 걸린다. */
    @Test
    void extremesSurvive() {
        OrderAck in = new OrderAck();
        in.clOrdId = 0;
        in.orderId = -1L; // u64의 최대값을 자바에서는 -1로 본다
        in.status = 255;
        in.reason = Integer.MIN_VALUE;
        in.filledQty = Integer.MAX_VALUE;
        in.price = -1;

        OrderAck out =
                WireCodec.decodeBody(
                        OrderAck.class, WireCodec.encodeBody(in), 0, 29);

        assertThat(out.orderId).isEqualTo(-1L);
        assertThat(out.status).isEqualTo(255); // u8은 부호 없이 읽는다
        assertThat(out.reason).isEqualTo(Integer.MIN_VALUE);
        assertThat(out.filledQty).isEqualTo(Integer.MAX_VALUE);
        assertThat(out.price).isEqualTo(-1);
    }

    /**
     * <b>빅엔디언이어야 한다.</b> 리틀엔디언으로 쓰면 C가 읽을 때 값이 뒤집힌다 —
     * 70000이 값이 뒤집힌 수가 되어도 전문은 멀쩡해 보인다.
     */
    @Test
    void bigEndianOnTheWire() {
        OrderAck in = new OrderAck();
        in.clOrdId = 0x0102030405060708L;

        byte[] body = WireCodec.encodeBody(in);

        assertThat(body[0]).isEqualTo((byte) 0x01);
        assertThat(body[7]).isEqualTo((byte) 0x08);
    }

    /** 고정 길이 문자열: 남는 자리는 0, 넘치면 자른다. */
    @Test
    void fixedWidthStrings() {
        OrderReq in = new OrderReq();
        in.account = "12";
        in.symbol = "ABCDEFGHIJK"; // 8바이트를 넘는다

        byte[] body = WireCodec.encodeBody(in);
        assertThat(body[2]).isZero(); // 계좌의 남는 자리

        OrderReq out = WireCodec.decodeBody(OrderReq.class, body, 0, body.length);
        assertThat(out.account).isEqualTo("12");
        assertThat(out.symbol).isEqualTo("ABCDEFGH"); // 잘렸다
    }

    /**
     * 호가 응답의 배열(T6-04). 배열 넷이 선언 순서대로 이어지고, 비어 있으면 0으로 채운다.
     * 첫 배열 첫 칸과 넷째 배열 마지막 칸 위치를 바이트로 본다.
     */
    @Test
    void intArraysInOrder() {
        BookAck in = new BookAck();
        in.symbol = "005930";
        in.market = 1;
        in.bidPrice = new int[BookAck.DEPTH];
        in.bidPrice[0] = 70000;
        in.askQty = new int[BookAck.DEPTH];
        in.askQty[BookAck.DEPTH - 1] = 0x01020304;
        // bidQty, askPrice는 비워 둔다 — 0으로 나가야 한다

        byte[] body = WireCodec.encodeBody(in);
        assertThat(body).hasSize(169); // C의 MSG_BOOK_ACK_LEN
        assertThat(body[8]).isEqualTo((byte) 1);
        assertThat(java.nio.ByteBuffer.wrap(body, 9, 4).getInt()).isEqualTo(70000);
        assertThat(body[165]).isEqualTo((byte) 0x01);
        assertThat(body[168]).isEqualTo((byte) 0x04);

        BookAck out = WireCodec.decodeBody(BookAck.class, body, 0, body.length);
        assertThat(out.bidPrice[0]).isEqualTo(70000);
        assertThat(out.bidQty).containsOnly(0);
        assertThat(out.askQty[BookAck.DEPTH - 1]).isEqualTo(0x01020304);

        in.bidQty = new int[3]; // 선언과 길이가 다르다
        assertThatThrownBy(() -> WireCodec.encodeBody(in)).isInstanceOf(WireException.class);
    }

    /** 길이가 규격과 다르면 해석하지 않는다(T3-02와 같은 판단). */
    @Test
    void rejectsWrongLength() {
        byte[] body = new byte[39];

        assertThatThrownBy(() -> WireCodec.decodeBody(OrderReq.class, body, 0, 38))
                .isInstanceOf(WireException.class);
        assertThatThrownBy(() -> WireCodec.decodeBody(OrderReq.class, body, 0, 40))
                .isInstanceOf(WireException.class);
    }

    @Test
    void headerRoundTrip() {
        WireHeader in = new WireHeader(1, 9, 46, 1234L, 5678L);
        WireHeader out = WireHeader.decode(in.encode(), 0);
        assertThat(out).isEqualTo(in);
    }

    /** magic·version이 틀리면 <b>해석하지 않는다.</b> */
    @Test
    void headerRejectsBadMagicAndVersion() {
        byte[] good = new WireHeader(1, 1, 0, 1, 1).encode();

        byte[] badMagic = good.clone();
        badMagic[0] = 'X';
        assertThatThrownBy(() -> WireHeader.decode(badMagic, 0))
                .isInstanceOf(WireException.class)
                .hasMessageContaining("magic");

        byte[] badVersion = good.clone();
        badVersion[2] = 99;
        assertThatThrownBy(() -> WireHeader.decode(badVersion, 0))
                .isInstanceOf(WireException.class)
                .hasMessageContaining("version");
    }

    /** body_len 한도를 넘으면 거절한다 — 받는 쪽이 4GB를 잡으면 안 된다. */
    @Test
    void headerRejectsHugeBody() {
        byte[] buf = new WireHeader(1, 1, 0, 1, 1).encode();
        buf[4] = 0x7F; // bodyLen을 아주 크게
        assertThatThrownBy(() -> WireHeader.decode(buf, 0))
                .isInstanceOf(WireException.class);
    }
}
