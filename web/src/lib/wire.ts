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
/** 시장을 원장이 SOR로 정한다. C `msg.h`의 `MSG_MARKET_AUTO`. */
export const MARKET_AUTO = 255;

export const STATUS_NEW = 0;
export const STATUS_PARTIAL = 1;
export const STATUS_FILLED = 2;

export type MarketName = "KRX" | "NXT" | "SOR";

export const marketName = (m: number): MarketName =>
  m === MARKET_KRX ? "KRX" : m === MARKET_NXT ? "NXT" : "SOR";

/**
 * 거절 사유. C `core/include/errors.h`의 문구를 옮겼다 — 화면에 보일 글자일 뿐이라
 * 틀려도 주문이 잘못 나가지는 않는다. 모르는 코드는 숫자를 그대로 보인다.
 */
const REASON: Record<number, string> = {
  [-1]: "잘못된 인자",
  [-3]: "가격이 유효 범위를 벗어남",
  [-4]: "수량이 유효 범위를 벗어남",
  [-5]: "호가 단위에 맞지 않는 가격",
  [-6]: "가격 제한폭 초과",
  [-7]: "해당 시장이 열려 있지 않음",
  [-8]: "지원하지 않는 요청",
  [-9]: "계좌·주문을 찾을 수 없음",
  [-11]: "원장의 주문 자리가 다 찼음",
  [-13]: "체결할 반대 호가가 없음",
  [-14]: "증거금이 모자람",
  [-15]: "주문 한도 초과",
  [-16]: "원장에 연결하지 못함",
};

export const reasonText = (code: number) => REASON[code] ?? `사유 코드 ${code}`;
