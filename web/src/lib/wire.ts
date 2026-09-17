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

/** 원장이 받는 주문 유형만 둔다(ledger/src/order_validate.c: 지정가·IOC·FOK). */
export const ORDER_LIMIT = 0;
export const ORDER_IOC = 2;
export const ORDER_FOK = 3;
export type OrderType = typeof ORDER_LIMIT | typeof ORDER_IOC | typeof ORDER_FOK;

export const MARKET_KRX = 0;
export const MARKET_NXT = 1;
/** 시장을 원장이 SOR로 정한다. C `msg.h`의 `MSG_MARKET_AUTO`. */
export const MARKET_AUTO = 255;

export const STATUS_NEW = 0;
export const STATUS_PARTIAL = 1;
export const STATUS_FILLED = 2;
export const STATUS_CANCELED = 3;
export const STATUS_REJECTED = 4;

export type MarketName = "KRX" | "NXT" | "SOR";

export const marketName = (m: number): MarketName =>
  m === MARKET_KRX ? "KRX" : m === MARKET_NXT ? "NXT" : "SOR";

export const sideText = (s: number) => (s === SIDE_BUY ? "매수" : "매도");

export const orderTypeText = (t: number) =>
  t === ORDER_IOC ? "IOC" : t === ORDER_FOK ? "FOK" : "지정가";

const STATUS_TEXT = ["대기", "부분 체결", "전량 체결", "취소", "거부"];
export const statusText = (s: number) => STATUS_TEXT[s] ?? `상태 ${s}`;

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

/**
 * 호가 단위. C `core/src/tick_size.c`의 표를 옮겼다. 화면이 가격을 올리고 내릴 때 쓴다 —
 * 판정은 원장이 다시 한다(틀려도 "호가 단위에 맞지 않는 가격"으로 거절될 뿐이다).
 */
const TICKS: [below: number, tick: number][] = [
  [2000, 1],
  [5000, 5],
  [20000, 10],
  [50000, 50],
  [200000, 100],
  [500000, 500],
  [Number.MAX_SAFE_INTEGER, 1000],
];

export const tickSize = (price: number) => TICKS.find(([below]) => price < below)![1];

export const isValidTick = (price: number) => price > 0 && price % tickSize(price) === 0;

/** 한 호가 위·아래. 구간 경계를 넘을 때 새 구간의 단위를 쓴다. */
export const stepPrice = (price: number, dir: 1 | -1) => {
  if (!isValidTick(price)) {
    const t = tickSize(Math.max(1, price));
    return dir > 0 ? Math.ceil(price / t) * t : Math.max(t, Math.floor(price / t) * t);
  }
  if (dir > 0) return price + tickSize(price);
  const down = price - tickSize(price - 1);
  return down > 0 ? down : price;
};
