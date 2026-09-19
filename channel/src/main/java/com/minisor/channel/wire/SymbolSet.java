package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 종목 전환 요청 (12바이트, T8-10).
 *
 * <p><b>호가창은 기준가 ±30%(가격 제한폭)만 펼쳐 둔다.</b> 그래서 다루는 종목을 바꾸려면
 * 그 종목의 가격대를 같이 줘야 한다 — 89,000원짜리 호가창에 260,000원 호가를 심으면
 * 통째로 버려지고 화면에서는 아무 일도 일어나지 않는다. 실제로 그렇게 됐다.
 *
 * <p>원장은 이 전문을 받으면 <b>그 종목의 원장을 새로 연다.</b> 미체결 주문과 잔고는
 * 초기화된다 — 이 원장은 한 종목짜리다.
 */
@WireMessage(type = 22, name = "SYMBOLSET")
public final class SymbolSet {

    @WireField(order = 1, type = STR, length = 8)
    public String symbol;

    /** 그 종목의 현재가(원). 호가창이 펼칠 가격대의 중심이다. */
    @WireField(order = 2, type = I32)
    public int refPrice;
}
