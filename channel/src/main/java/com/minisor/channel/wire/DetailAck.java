package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 주문 상세 응답 (91바이트, T7-02). 시장별 값은 시장 번호를 첨자로 쓰는 배열이다(0=KRX, 1=NXT).
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 18, name = "DETAILACK")
public final class DetailAck {
    /** C의 {@code MSG_LEG_SLOTS}. 시장 수와 같다. */
    public static final int LEGS = 2;

    @WireField(order = 1, type = U64)
    public long orderId;

    @WireField(order = 2, type = U64)
    public long clOrdId;

    @WireField(order = 3, type = I32)
    public int reason;

    @WireField(order = 4, type = U8)
    public int side;

    @WireField(order = 5, type = U8)
    public int status;

    /** 주문할 때 고른 시장. 255면 SOR 자동. */
    @WireField(order = 6, type = U8)
    public int market;

    @WireField(order = 7, type = I32)
    public int price;

    @WireField(order = 8, type = I32)
    public int qty;

    @WireField(order = 9, type = I32)
    public int filled;

    @WireField(order = 10, type = I32)
    public int canceled;

    @WireField(order = 11, type = I32)
    public int working;

    @WireField(order = 12, type = I64)
    public long notional;

    @WireField(order = 13, type = I32, count = LEGS)
    public int[] legSent;

    @WireField(order = 14, type = I32, count = LEGS)
    public int[] legFilled;

    @WireField(order = 15, type = I32, count = LEGS)
    public int[] legCanceled;

    @WireField(order = 16, type = I64, count = LEGS)
    public long[] legNotional;
}
