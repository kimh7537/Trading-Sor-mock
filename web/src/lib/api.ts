import type { Book } from "./types";
import { marketName } from "./wire";

// 기본은 같은 출처. 개발 서버가 /api를 채널계로 넘긴다(vite.config.ts).
const BASE = import.meta.env.VITE_API_BASE ?? "";

export type Outcome = "ACCEPTED" | "REJECTED" | "IN_DOUBT";

export interface OrderRequest {
  account: string;
  symbol: string;
  clOrdId: number;
  side: number;   // wire.ts — SIDE_BUY(0) / SIDE_SELL(1)
  type: number;   // wire.ts — ORDER_LIMIT(0)
  market: number; // wire.ts — MARKET_KRX(0) / MARKET_NXT(1) / MARKET_AUTO(255)
  price: number;
  qty: number;
}

export interface OrderResponse {
  outcome: Outcome;
  clOrdId: number;
  orderId: number;
  reason: number;
  message: string;
  status: number;    // wire.ts — STATUS_*
  filledQty: number;
  avgPrice: number;  // 체결이 있으면 평균 체결가, 없으면 주문 가격
}

export async function submitOrder(req: OrderRequest): Promise<OrderResponse> {
  const res = await fetch(`${BASE}/api/orders`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(req),
  });

  // 400은 본문이 스프링 기본 오류라 형태가 다르다.
  if (res.status === 400) {
    return {
      outcome: "REJECTED",
      clOrdId: req.clOrdId,
      orderId: 0,
      reason: -1,
      message: "입력이 올바르지 않습니다",
      status: 0,
      filledQty: 0,
      avgPrice: 0,
    };
  }
  // 채널계가 정한 모양(200·202·422·503)이 아니면 — 예: 예상 못 한 500 — outcome이 없다.
  // 그대로 넘기면 화면이 없는 스타일을 읽다 통째로 멈춘다.
  const body = (await res.json().catch(() => null)) as Partial<OrderResponse> | null;
  if (body?.outcome !== "ACCEPTED" && body?.outcome !== "REJECTED" && body?.outcome !== "IN_DOUBT") {
    return {
      outcome: "REJECTED",
      clOrdId: req.clOrdId,
      orderId: 0,
      reason: 0,
      message: `채널계 오류 (HTTP ${res.status})`,
      status: 0,
      filledQty: 0,
      avgPrice: 0,
    };
  }
  return body as OrderResponse;
}

/** 원장 안의 실제 호가창. 원장이 없으면 예외. */
export async function fetchBook(market: number): Promise<Book> {
  const res = await fetch(`${BASE}/api/book?market=${market}`);
  if (!res.ok) throw new Error(`호가 조회 실패 ${res.status}`);
  const b = (await res.json()) as Omit<Book, "market"> & { market: number };
  return { ...b, market: marketName(b.market) as Book["market"] };
}
