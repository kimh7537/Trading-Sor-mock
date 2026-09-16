const BASE = import.meta.env.VITE_API_BASE ?? "http://localhost:8080";

export type Outcome = "ACCEPTED" | "REJECTED" | "IN_DOUBT";

export interface OrderRequest {
  account: string;
  symbol: string;
  clOrdId: number;
  side: number;   // wire.ts — SIDE_BUY(0) / SIDE_SELL(1)
  type: number;   // wire.ts — ORDER_LIMIT(0)
  market: number; // wire.ts — MARKET_KRX(0) / MARKET_NXT(1)
  price: number;
  qty: number;
}

export interface OrderResponse {
  outcome: Outcome;
  clOrdId: number;
  orderId: number;
  reason: number;
  message: string;
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
    };
  }
  return (await res.json()) as OrderResponse;
}
