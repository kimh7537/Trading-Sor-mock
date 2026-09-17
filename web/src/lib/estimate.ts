import type { Book, Market } from "./types";

/** 전문의 시장 번호(0 KRX, 1 NXT, 255 SOR)가 쓰는 호가창들 */
export const marketsOf = (market: number): Market[] =>
  market === 0 ? ["KRX"] : market === 1 ? ["NXT"] : ["KRX", "NXT"];

export interface Estimate {
  /** 지금 보이는 호가로 바로 체결될 수량 */
  fill: number;
  notional: number;
  /** 평균 체결가(버림). 체결이 없으면 0 */
  avg: number;
  /** 바로 체결되지 않는 수량 — 지정가면 호가창에 남고, IOC·FOK면 취소된다 */
  rest: number;
  byMarket: Record<Market, number>;
}

/**
 * 지금 화면에 보이는 호가만으로 계산한 예상 체결(T7-05). **주문 전 참고용이다.**
 * 실제 배분·체결은 원장이 정하고, 그 사이 다른 주문이 먼저 들어오면 달라진다.
 * 채널계가 주는 호가는 시장마다 10단이라 그보다 깊이 쓸어 담는 주문은 적게 잡힌다.
 *
 * `markets`가 둘이면 두 시장 호가를 가격순으로 합쳐 채운다. 같은 가격이면 `markets` 순서대로.
 * `allOrNone`(FOK)이면 전량이 안 되면 아무것도 체결되지 않는다.
 */
export function estimate(
  books: Partial<Record<Market, Book>>,
  markets: Market[],
  buy: boolean,
  limit: number,
  qty: number,
  allOrNone: boolean,
): Estimate {
  const levels = markets.flatMap(
    (m) =>
      (buy ? books[m]?.asks : books[m]?.bids)
        ?.filter((l) => (buy ? l.price <= limit : l.price >= limit))
        .map((l) => ({ m, ...l })) ?? [],
  );
  levels.sort((a, b) => (buy ? a.price - b.price : b.price - a.price)); // 안정 정렬 — 같은 가격은 시장 순서 유지

  const byMarket: Record<Market, number> = { KRX: 0, NXT: 0 };
  let fill = 0;
  let notional = 0;
  for (const l of levels) {
    if (fill >= qty) break;
    const take = Math.min(qty - fill, l.qty);
    fill += take;
    notional += take * l.price;
    byMarket[l.m] += take;
  }

  if (allOrNone && fill < qty) {
    return { fill: 0, notional: 0, avg: 0, rest: qty, byMarket: { KRX: 0, NXT: 0 } };
  }
  return { fill, notional, avg: fill > 0 ? Math.floor(notional / fill) : 0, rest: qty - fill, byMarket };
}
