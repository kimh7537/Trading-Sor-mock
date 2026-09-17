package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 잔고 조회 요청 (12바이트, T7-02).
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 */
@WireMessage(type = 19, name = "BALANCEREQ")
public final class BalanceReq {
    @WireField(order = 1, type = STR, length = 12)
    public String account;
}
