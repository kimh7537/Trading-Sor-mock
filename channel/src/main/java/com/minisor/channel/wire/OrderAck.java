package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 주문 응답 (29바이트).
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 2, name = "ORDERACK")
public final class OrderAck {
    @WireField(order = 1, type = U64)
    public long clOrdId;

    @WireField(order = 2, type = U64)
    public long orderId;

    @WireField(order = 3, type = U8)
    public int status;

    @WireField(order = 4, type = I32)
    public int reason;

    @WireField(order = 5, type = I32)
    public int filledQty;

    @WireField(order = 6, type = I32)
    public int price;

}
