package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 체결 통보 (46바이트). 요청 없이 원장이 밀어 보낸다.
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 9, name = "FILLNOTI")
public final class FillNoti {
    @WireField(order = 1, type = U64)
    public long orderId;

    @WireField(order = 2, type = U64)
    public long clOrdId;

    @WireField(order = 3, type = STR, length = 8)
    public String symbol;

    @WireField(order = 4, type = U8)
    public int market;

    @WireField(order = 5, type = U8)
    public int side;

    @WireField(order = 6, type = I32)
    public int price;

    @WireField(order = 7, type = I32)
    public int qty;

    @WireField(order = 8, type = I32)
    public int remainingQty;

    @WireField(order = 9, type = U64)
    public long execId;

}
