package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 주문 요청 (39바이트).
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 *
 * <p>{@code side}·{@code type}·{@code market}의 <b>값</b>은 {@link WireEnums}를 따른다
 * (매수=0, 지정가=0, KRX=0). 길이 대조만으로는 값의 뜻이 틀린 것을 못 잡는다 — 실제로
 * 1부터 센 값이 한동안 그대로 실렸다(T6-01).
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
