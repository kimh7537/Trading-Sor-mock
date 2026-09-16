package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 호가창 조회 요청 (9바이트, T6-04).
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 15, name = "BOOKREQ")
public final class BookReq {
    @WireField(order = 1, type = STR, length = 8)
    public String symbol;

    @WireField(order = 2, type = U8)
    public int market;
}
