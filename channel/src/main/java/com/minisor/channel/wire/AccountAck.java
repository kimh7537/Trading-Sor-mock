package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/** 계좌 개설 응답 (32바이트, T9-01). {@code code}가 0이면 쓸 수 있는 계좌다. */
@WireMessage(type = 25, name = "ACCT_ACK")
public final class AccountAck {

    @WireField(order = 1, type = STR, length = 12)
    public String account;

    /** 0이면 쓸 수 있다(새로 열었든 이미 있었든). 음수면 원장의 에러코드. */
    @WireField(order = 2, type = I32)
    public int code;

    @WireField(order = 3, type = I64)
    public long cash;

    @WireField(order = 4, type = I64)
    public long reserved;
}
