package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 호가 스냅샷 주입 (177바이트, T8-02). 바깥에서 받은 실호가를 원장 호가창에 심는다.
 *
 * <p>배치는 {@link BookAck}에 <b>피드 시각(i64)</b>을 더한 것이다. 시각을 싣는 이유는
 * 원장이 시스템 시각을 읽지 않기 때문이다 — 스냅샷이 자기 시각을 들고 와야
 * 리플레이(T8-06)가 같은 파일에서 같은 결과를 낸다.
 *
 * <p>응답은 {@link BookAck}이다. 심은 뒤의 호가창을 그대로 돌려준다.
 *
 * <p><b>단수가 모자라면 0으로 채우고 넘치면 자른다.</b> 토스 오픈 API의 호가 스키마에는
 * {@code maxItems}가 없고 예시가 3단·1단이라, 10단이 오리라 기대할 수 없다.
 */
@WireMessage(type = 21, name = "BOOKFEED")
public final class BookFeed {
    /** C의 {@code MSG_BOOK_DEPTH}. */
    public static final int DEPTH = BookAck.DEPTH;

    @WireField(order = 1, type = STR, length = 8)
    public String symbol;

    @WireField(order = 2, type = U8)
    public int market;

    /**
     * C의 {@code MSG_FEED_END}. 바깥 시세가 <b>끝났다</b>는 신호.
     *
     * <p>스냅샷이 잠시 안 오는 것(장 마감)과 피드가 끝난 것은 원장이 보기에 같다. 시간으로
     * 어림해 풀면 장 마감에 가상 참가자가 슬그머니 돌아와 "실시세인 척하는 시뮬"이 된다.
     * 그래서 <b>보내는 쪽이 끝을 알린다.</b>
     */
    public static final int FEED_END = 0x01;

    @WireField(order = 3, type = U8)
    public int flags;

    @WireField(order = 4, type = I64)
    public long feedTs;

    @WireField(order = 5, type = I32, count = DEPTH)
    public int[] bidPrice = new int[DEPTH];

    @WireField(order = 6, type = I32, count = DEPTH)
    public int[] bidQty = new int[DEPTH];

    @WireField(order = 7, type = I32, count = DEPTH)
    public int[] askPrice = new int[DEPTH];

    @WireField(order = 8, type = I32, count = DEPTH)
    public int[] askQty = new int[DEPTH];

    /** 10단을 넘으면 자르고 모자라면 0으로 남긴다. {@code levels}는 {가격, 잔량} 쌍의 목록. */
    public static void fill(int[] price, int[] qty, java.util.List<int[]> levels) {
        for (int i = 0; i < DEPTH && i < levels.size(); i++) {
            price[i] = levels.get(i)[0];
            qty[i] = levels.get(i)[1];
        }
    }
}
