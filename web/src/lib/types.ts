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
  applied: number;
  lastFeedTs: number;
  note: string;
}

/** 차트 한 점. 호가가 바뀔 때마다 하나씩 쌓인다. */
export interface Tick {
  t: number;
  /** 두 시장을 합친 최우선호가의 중간값 */
  mid: number;
  /** 이 점까지 사이에 체결된 내 주문 수량 */
  vol: number;
}
