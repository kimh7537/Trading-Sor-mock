package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 종목 전환 요청 (13바이트, T8-10·T10-01).
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

    /** 그 종목의 현재가. 국내는 원, 미국은 센트. 호가창이 펼칠 가격대의 중심이다. */
    @WireField(order = 2, type = I32)
    public int refPrice;

    /**
     * 종목 종류(T10-01). 0=국내, 1=미국.
     *
     * <p><b>호가 단위와 통화가 여기서 갈린다.</b> 미국 종목을 국내로 보내면 원장이
     * 국내 호가 단위 표로 호가창을 열고, $191.23(19,123센트)이 "10원 단위"에 걸려
     * 정상 가격이 거절된다.
     */
    @WireField(order = 3, type = U8)
    public int kind;
}
