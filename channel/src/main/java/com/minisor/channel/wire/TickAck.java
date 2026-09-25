package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/** 가상 참가자 스위치 응답 (5바이트, 점검). */
@WireMessage(type = 27, name = "TICK_ACK")
public final class TickAck {

    /** 지금 상태. 0이면 멈춰 있다. */
    @WireField(order = 1, type = U8)
    public int on;

    /** 0이면 반영됐다. */
    @WireField(order = 2, type = I32)
    public int code;
}
