package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 취소 요청 (28바이트). orderId가 0이면 clOrdId로 찾으라는 뜻이다(T3-13).
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 3, name = "CANCELREQ")
public final class CancelReq {
    @WireField(order = 1, type = STR, length = 12)
    public String account;

    @WireField(order = 2, type = U64)
    public long orderId;

    @WireField(order = 3, type = U64)
    public long clOrdId;

}
