/**
 * 전문에 실리는 열거값. C의 `core/include/types.h`와 같아야 한다.
 *
 * 처음에 화면은 매수=1, 지정가=1, KRX=1처럼 **1부터 센 날숫자**를 보냈고,
 * C는 0부터 세서 그것을 "NXT 시장가 매도"로 읽었다(T6-01). 날숫자를 없애고
 * 이 이름만 쓴다. 자바 쪽 같은 값은 `WireEnums`이고, 그쪽은 C 헤더와 테스트로
 * 대조된다.
 */
export const SIDE_BUY = 0;
export const SIDE_SELL = 1;
export type Side = typeof SIDE_BUY | typeof SIDE_SELL;

export const ORDER_LIMIT = 0;

export const MARKET_KRX = 0;
export const MARKET_NXT = 1;
