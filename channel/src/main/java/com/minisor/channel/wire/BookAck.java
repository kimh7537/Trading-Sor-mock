package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 호가창 조회 응답 (181바이트, T6-04 · T8-09). 매수는 높은 가격부터, 매도는 낮은 가격부터.
 * 없는 단은 가격·수량 모두 0이다.
 *
 * <p>배치는 C의 {@code core/include/msg.h}와 같아야 한다.
 * 어긋남은 {@code WireLayoutTest}가 길이로 잡는다.
 */
@WireMessage(type = 16, name = "BOOKACK")
public final class BookAck {
    /** C의 {@code MSG_BOOK_DEPTH}. */
    public static final int DEPTH = 10;

    @WireField(order = 1, type = STR, length = 8)
    public String symbol;

    @WireField(order = 2, type = U8)
    public int market;

    @WireField(order = 3, type = I32, count = DEPTH)
    public int[] bidPrice;

    @WireField(order = 4, type = I32, count = DEPTH)
    public int[] bidQty;

    @WireField(order = 5, type = I32, count = DEPTH)
    public int[] askPrice;

    @WireField(order = 6, type = I32, count = DEPTH)
    public int[] askQty;

    /**
     * 마지막 체결가. 아직 한 건도 없으면 0.
     *
     * <p>봉(OHLCV)은 체결을 묶은 것이라 호가만으로는 만들 수 없다. 체결을 건건이 보내는
     * 전문을 따로 두는 대신, 이미 주기적으로 읽는 호가 응답에 얹었다(T8-09).
     */
    @WireField(order = 7, type = I32)
    public int lastPrice;

    /** 이 시장에서 지금까지 체결된 총 수량. 차이를 내면 구간 거래량이다. */
    @WireField(order = 8, type = I64)
    public long tradedQty;
}
