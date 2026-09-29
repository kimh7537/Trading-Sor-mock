import type { MarketName, Side } from "./wire";

export type Market = "KRX" | "NXT";

export interface Level {
  price: number;
  qty: number;
}

export interface Book {
  market: Market;
  bids: Level[]; // 높은 가격부터
  asks: Level[]; // 낮은 가격부터
}

/** 체결 한 건. 채널계가 방송한다 — 접수 즉시 체결과 나중 체결 모두. */
export interface Fill {
  id: string;
  at: string;
  /** 실제로 체결된 시장. 채널계가 원장 상세에서 시장별로 나눠 보낸다(T7-03) */
  market: MarketName;
  side: Side;
  price: number;
  qty: number;
  clOrdId: number;
  orderId: number;
}

/**
 * 채널계 ↔ 원장 **전문 한 왕복**에 실제로 오간 것(점검).
 *
 * 길이는 규격에서 나온 고정 길이고, `micros`는 채널계가 잰 벽시계 시간이다.
 * **원장 안에서 검증·SOR·매칭이 각각 얼마나 걸렸는지는 없다** — 재지 않는다.
 * 매칭 엔진이 시스템 시각을 읽으면 같은 입력이 같은 출력을 내지 않게 된다.
 */
export interface WireHop {
  sent: string;
  sentType: number;
  sentBytes: number;
  got: string;
  gotType: number;
  gotBytes: number;
  seq: number;
  micros: number;
}

/** 전문 필드 하나. `offset`은 바디 안 시작 위치(바이트), `size`는 고정 길이다 */
export interface WireFieldView {
  name: string;
  type: string;
  offset: number;
  size: number;
  value: string;
}

/**
 * 채널계 ↔ 원장을 오간 전문 한 왕복 전체(점검).
 *
 * `ok`가 거짓이면 **보냈는데 답을 못 받았다** — 받은 쪽 칸이 비어 있다.
 */
export interface WireHeaderView {
  version: number;
  type: number;
  bodyLen: number;
  seq: number;
  ts: number;
}

export interface WireFrame {
  id: number;
  at: string;
  sent: string;
  sentType: number;
  sentBytes: number;
  sentFields: WireFieldView[];
  got: string;
  gotType: number;
  gotBytes: number;
  gotFields: WireFieldView[];
  seq: number;
  micros: number;
  ok: boolean;
  /** 실제로 오간 바이트 그대로(헤더 + 바디)를 16진수로. 빈 문자열이면 없다 */
  sentHex: string;
  gotHex: string;
  /** 바디 앞에 붙은 머리를 푼 것 */
  sentHeader: WireHeaderView | null;
  gotHeader: WireHeaderView | null;
}

/** 매칭 엔진이 어디에 있는가(T12-05). 원장이 `SYMBOL_ACK`으로 알려 준 값이다 */
export const VENUE_UNKNOWN = -1;
export const VENUE_LOCAL = 0;
export const VENUE_REMOTE = 1;

export interface WireLog {
  total: number;
  capacity: number;
  /** -1 모름 · 0 원장 프로세스 안 · 1 별도 거래소 프로세스(FEP 경유) */
  venue: number;
  frames: WireFrame[];
}

/** 시장 하나로 나간 몫 — "논리 → 물리"의 물리 쪽 */
export interface LegView {
  market: number;
  sent: number;
  filled: number;
  canceled: number;
  notional: number;
  avgPrice: number;
}

/** 원장이 알고 있는 주문 하나(채널계 `OrderView`) */
export interface OrderView {
  orderId: number;
  clOrdId: number;
  side: Side;
  type: number;
  market: number;
  price: number;
  qty: number;
  filled: number;
  canceled: number;
  working: number;
  notional: number;
  avgPrice: number;
  status: number;
  done: boolean;
  legs: LegView[];
}

/** 원장까지 가지 못했거나 원장이 거절한 주문. 주문번호가 없어 화면만 기억한다 */
export interface LocalReject {
  clOrdId: number;
  at: string;
  side: Side;
  type: number;
  market: number;
  price: number;
  qty: number;
  outcome: "REJECTED" | "IN_DOUBT";
  reason: string;
}

export interface Balance {
  account: string;
  cash: number;
  reserved: number;
  available: number;
}

export interface CancelResult {
  orderId: number;
  reason: number;
  status: number;
  canceledQty: number;
  order: OrderView | null;
}

/**
 * 지금 호가창을 무엇이 움직이고 있는가(T8-05).
 *
 * `sim`이면 가상 참가자, `live`면 바깥에서 받은 실호가다. `available`이 거짓이면
 * 채널계에 실시세 설정(.env의 토스 키나 재생 파일)이 없어 바꿀 수 없다.
 */
export interface FeedStatus {
  mode: "sim" | "live";
  source: string;
  available: boolean;
  market: number;
  symbol: string;
  /** 종목 이름. 화면이 "삼성전자"라고 보여 준다 */
  symbolName: string;
  applied: number;
  lastFeedTs: number;
  note: string;
  /** 마지막으로 붙지 못한 이유. 붙어 있으면 null */
  error: string | null;
  /**
   * 지금 **가상 참가자**가 호가를 만들고 있는가.
   *
   * 시뮬 모드에서 호가창을 움직이는 것은 이것뿐이다. "시뮬"이라고만 적어 두면
   * 호가가 도는 것을 보고 실시세로 오해한다 — 실제로 그랬다.
   */
  simTicks: boolean;
}

/**
 * 차트 한 점. 호가가 바뀔 때마다 하나씩 쌓인다.
 *
 * **시장마다 따로 잰다.** 두 시장을 합친 중간가는 이 데모에서 거의 움직이지 않는다 —
 * 한 시장의 최우선 매도와 다른 시장의 최우선 매수가 기준가에서 맞물려 통합 호가창이
 * 늘 교차해 있기 때문이다(그 교차가 SOR이 존재하는 이유이기도 하다). 시장별로 그리면
 * 두 시장의 가격 차이가 그대로 보인다.
 *
 * 값이 0이면 그 시장의 호가가 아직 없다는 뜻이다.
 */
export interface Tick {
  t: number;
  krx: number;
  nxt: number;
  /** 이 점까지 사이에 체결된 내 주문 수량 */
  vol: number;
}

/** 봉 하나(OHLCV). 토스에서 받은 **바깥 시장**의 체결 집계다 — 내 원장과 별개다. */
export interface Candle {
  t: number;
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
}

export interface CandleChart {
  symbol: string;
  interval: "1m" | "1d";
  candles: Candle[];
  fetchedAt: number;
}
