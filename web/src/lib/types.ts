import type { Side } from "./wire";

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
  market: Market;
  side: Side;
  price: number;
  qty: number;
  clOrdId: number;
}

/** 화면이 붙기 전까지 쓰는 표본. 결정적이라 볼 때마다 같다. */
export function sampleBooks(): Book[] {
  const mk = (base: number, spread: number, seed: number): Book["bids"] =>
    Array.from({ length: 8 }, (_, i) => ({
      price: base - i * spread,
      qty: ((seed * (i + 3)) % 47) * 10 + 20,
    }));

  return [
    {
      market: "KRX",
      bids: mk(70000, 100, 7),
      asks: mk(70100, -100, 5).map((l) => ({ ...l })),
    },
    {
      market: "NXT",
      bids: mk(69900, 100, 11),
      asks: mk(70000, -100, 13).map((l) => ({ ...l })),
    },
  ];
}
