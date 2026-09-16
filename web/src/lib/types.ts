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

export interface Fill {
  at: string;
  /** SOR이면 원장이 시장을 골랐다. 어느 시장에서 체결됐는지는 응답에 없다 */
  market: MarketName;
  side: Side;
  price: number;
  qty: number;
  clOrdId: number;
}
