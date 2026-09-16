package com.minisor.channel.wire;

/**
 * 전문에 실리는 열거값. <b>C의 {@code core/include/types.h}와 같아야 한다.</b>
 *
 * <h2>왜 이 파일이 생겼나</h2>
 *
 * 처음에는 채널계와 화면이 <b>각자 1부터 센 숫자</b>(매수=1, 지정가=1, KRX=1)를
 * 그대로 전문에 실었다. C는 0부터 센다(매수=0, 지정가=0, KRX=0). 변환이 어디에도
 * 없었으므로 화면의 "KRX 지정가 매수"가 C에서는 <b>"NXT 시장가 매도"</b>로
 * 읽혔다.
 *
 * <p>아무 테스트도 이것을 잡지 못했다. {@code WireLayoutTest}는 필드의
 * <b>길이</b>만 대조했고, 원장 데몬은 방향·유형·시장을 보지 않고 무조건 성공을
 * 돌려줬기 때문이다. 바이트 배치가 맞는 것과 값의 뜻이 맞는 것은 다른 일이다.
 *
 * <p>그래서 숫자를 쓰는 곳은 전부 이 상수를 거치게 하고,
 * {@code WireLayoutTest}가 C 헤더를 읽어 <b>값까지</b> 대조한다.
 */
public final class WireEnums {

    private WireEnums() {}

    /* side_t */
    public static final int SIDE_BUY = 0;
    public static final int SIDE_SELL = 1;

    /* order_type_t */
    public static final int ORDER_LIMIT = 0;
    public static final int ORDER_MARKET = 1;
    public static final int ORDER_IOC = 2;
    public static final int ORDER_FOK = 3;
    public static final int ORDER_MIDPOINT = 4;

    /* order_status_t */
    public static final int STATUS_NEW = 0;
    public static final int STATUS_PARTIAL = 1;
    public static final int STATUS_FILLED = 2;
    public static final int STATUS_CANCELED = 3;
    public static final int STATUS_REJECTED = 4;

    /* market_t */
    public static final int MARKET_KRX = 0;
    public static final int MARKET_NXT = 1;
}
