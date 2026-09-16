package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 조회 응답 (38바이트). last가 참이면 이것이 마지막이다(T3-14).
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 8, name = "QUERYACK")
public final class QueryAck {
    @WireField(order = 1, type = U64)
    public long orderId;

    @WireField(order = 2, type = U64)
    public long clOrdId;

    @WireField(order = 3, type = STR, length = 8)
    public String symbol;

    @WireField(order = 4, type = U8)
    public int status;

    @WireField(order = 5, type = I32)
    public int price;

    @WireField(order = 6, type = I32)
    public int qty;

    @WireField(order = 7, type = I32)
    public int filledQty;

    @WireField(order = 8, type = U8)
    public int last;

}
