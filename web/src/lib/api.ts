import type { Balance, Book, CancelResult, FeedStatus, OrderView } from "./types";
import { marketName } from "./wire";

// 기본은 같은 출처. 개발 서버가 /api를 채널계로 넘긴다(vite.config.ts).
const BASE = import.meta.env.VITE_API_BASE ?? "";

/** 원장 데몬의 데모 계좌·종목(ledger/src/ledger_core.c의 기본값) */
export const ACCOUNT = "123456789012";
export const SYMBOL = "005930";

export type Outcome = "ACCEPTED" | "REJECTED" | "IN_DOUBT";

export interface OrderRequest {
  account: string;
  symbol: string;
  clOrdId: number;
  side: number;   // wire.ts — SIDE_BUY(0) / SIDE_SELL(1)
  type: number;   // wire.ts — ORDER_LIMIT(0) / ORDER_IOC(2) / ORDER_FOK(3)
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

const rejected = (req: OrderRequest, reason: number, message: string): OrderResponse => ({
  outcome: "REJECTED",
  clOrdId: req.clOrdId,
  orderId: 0,
  reason,
  message,
  status: 0,
  filledQty: 0,
  avgPrice: 0,
});

export async function submitOrder(req: OrderRequest): Promise<OrderResponse> {
  let res: Response;
  try {
    res = await fetch(`${BASE}/api/orders`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(req),
    });
  } catch {
    return rejected(req, -16, "채널계에 붙지 못했다");
  }

  // 400은 본문이 스프링 기본 오류라 형태가 다르다.
  if (res.status === 400) {
    return rejected(req, -1, "입력이 올바르지 않습니다");
  }
  // 채널계가 정한 모양(200·202·422·503)이 아니면 — 예: 예상 못 한 500 — outcome이 없다.
  // 그대로 넘기면 화면이 없는 스타일을 읽다 통째로 멈춘다.
  const body = (await res.json().catch(() => null)) as Partial<OrderResponse> | null;
  if (body?.outcome !== "ACCEPTED" && body?.outcome !== "REJECTED" && body?.outcome !== "IN_DOUBT") {
    return rejected(req, 0, `채널계 오류 (HTTP ${res.status})`);
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

/** 이 채널계가 낸 주문과 최신 상태, 최근 것부터(T7-03) */
export async function fetchOrders(): Promise<OrderView[]> {
  const res = await fetch(`${BASE}/api/orders`);
  if (!res.ok) throw new Error(`주문 목록 조회 실패 ${res.status}`);
  return (await res.json()) as OrderView[];
}

export async function fetchOrder(orderId: number): Promise<OrderView | null> {
  const res = await fetch(`${BASE}/api/orders/${orderId}`);
  if (res.status === 404) return null;
  if (!res.ok) throw new Error(`주문 조회 실패 ${res.status}`);
  return (await res.json()) as OrderView;
}

/**
 * 취소. 채널계의 상태 코드: 200 취소됨, 409 이미 끝난 주문, 404 모르는 주문, 503 원장 없음.
 * 취소는 다시 보내도 안전하다.
 */
export async function cancelOrder(
  orderId: number,
): Promise<{ status: number; result: CancelResult | null }> {
  try {
    const res = await fetch(`${BASE}/api/orders/${orderId}`, { method: "DELETE" });
    const result = (await res.json().catch(() => null)) as CancelResult | null;
    return { status: res.status, result };
  } catch {
    return { status: 0, result: null };
  }
}

/** 데모 계좌의 예수금·묶인 금액·주문 가능 금액(T7-03) */
export async function fetchBalance(): Promise<Balance> {
  const res = await fetch(`${BASE}/api/balance`);
  if (!res.ok) throw new Error(`잔고 조회 실패 ${res.status}`);
  return (await res.json()) as Balance;
}

/** 지금 시뮬 모드인가 실시세 모드인가(T8-05) */
export async function fetchFeed(): Promise<FeedStatus> {
  const res = await fetch(`${BASE}/api/feed`);
  if (!res.ok) throw new Error(`모드 조회 실패 ${res.status}`);
  return (await res.json()) as FeedStatus;
}

/**
 * 모드를 바꾼다. 상태 코드가 셋이다.
 *
 * - 200 바뀌었다 (녹화 파일 재생은 시작한 순간 이미 실시세다)
 * - 202 붙는 중이다 (토스는 붙어 봐야 안다. 붙으면 `feed-mode` 방송이 화면을 바꾼다)
 * - 409 실시세 설정이 없어 바꿀 수 없다
 *
 * "켜졌다"고 표시해 놓고 아무 일도 일어나지 않는 것이 제일 나쁘다.
 */
export async function setFeedMode(
  mode: "sim" | "live",
): Promise<{ status: number; feed: FeedStatus | null }> {
  try {
    const res = await fetch(`${BASE}/api/feed/mode?mode=${mode}`, { method: "POST" });
    const feed = (await res.json().catch(() => null)) as FeedStatus | null;
    return { status: res.status, feed };
  } catch {
    return { status: 0, feed: null };
  }
}
