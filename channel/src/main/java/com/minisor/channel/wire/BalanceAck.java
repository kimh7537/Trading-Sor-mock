package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 잔고 조회 응답 (32바이트, T7-02). 없는 계좌면 reason이 -9이고 금액은 0이다.
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 */
@WireMessage(type = 20, name = "BALANCEACK")
public final class BalanceAck {
    @WireField(order = 1, type = STR, length = 12)
    public String account;

    @WireField(order = 2, type = I32)
    public int reason;

    /** 예수금(원). */
    @WireField(order = 3, type = I64)
    public long cash;

    /** 주문에 묶인 금액(원). */
    @WireField(order = 4, type = I64)
    public long reserved;
}
