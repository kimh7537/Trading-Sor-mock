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

    /** 보유 수량 (T11-01). */
    @WireField(order = 5, type = I64)
    public long posQty;

    /** 매입 원가 합. 평균 단가 = posCost / posQty. */
    @WireField(order = 6, type = I64)
    public long posCost;

    /** 실현 손익 누계. 판 것에서만 생긴다. */
    @WireField(order = 7, type = I64)
    public long realized;
}
