package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 주문 요청 (39바이트).
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 1, name = "ORDERREQ")
public final class OrderReq {
    @WireField(order = 1, type = STR, length = 12)
    public String account;

    @WireField(order = 2, type = STR, length = 8)
    public String symbol;

    @WireField(order = 3, type = U64)
    public long clOrdId;

    @WireField(order = 4, type = U8)
    public int side;

    @WireField(order = 5, type = U8)
    public int type;

    @WireField(order = 6, type = U8)
    public int market;

    @WireField(order = 7, type = I32)
    public int price;

    @WireField(order = 8, type = I32)
    public int qty;

}
