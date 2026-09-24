package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/** 종목 전환 응답 (17바이트, T8-10·T10-01). {@code code}가 0이면 바뀐 것이다. */
@WireMessage(type = 23, name = "SYMBOLACK")
public final class SymbolAck {

    /** 바뀐 뒤의 종목. 실패했으면 그대로인 종목이 온다. */
    @WireField(order = 1, type = STR, length = 8)
    public String symbol;

    @WireField(order = 2, type = I32)
    public int refPrice;

    /** 0이면 바뀌었다. 음수면 원장의 에러코드. */
    @WireField(order = 3, type = I32)
    public int code;

    /** 지금 다루는 종목의 종류. 0=국내(원), 1=미국(센트). */
    @WireField(order = 4, type = U8)
    public int kind;
}
