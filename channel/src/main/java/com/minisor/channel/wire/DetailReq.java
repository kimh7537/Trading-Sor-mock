package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 주문 상세 요청 (20바이트, T7-02). 이 계좌의 주문이 아니면 원장은 "없음"으로 답한다.
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 17, name = "DETAILREQ")
public final class DetailReq {
    @WireField(order = 1, type = STR, length = 12)
    public String account;

    @WireField(order = 2, type = U64)
    public long orderId;
}
