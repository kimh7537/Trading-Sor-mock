package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/** 종목 전환 응답 (18바이트, T8-10·T10-01·T12-05). {@code code}가 0이면 바뀐 것이다. */
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

    /**
     * 매칭 엔진이 <b>어디에 있는가</b>(T12-05). 0이면 원장 프로세스 안,
     * 1이면 별도 거래소 프로세스이고 그 사이를 FEP가 잇는다({@code ledgerd --exchange}).
     *
     * <p>화면의 통신 흐름 구조도가 이 값을 보고 FEP 홉을 그린다. 원장이 알려 주지
     * 않으면 화면은 알 방법이 없어 <b>지나는 길을 지나지 않는다고</b> 그리게 된다.
     */
    @WireField(order = 5, type = U8)
    public int venue;
}
