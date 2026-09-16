import { useCallback, useEffect, useMemo, useState } from "react";
import {
  MARKET_KRX,
  MARKET_NXT,
  SIDE_BUY,
  STATUS_FILLED,
  marketName,
  reasonText,
  type Side,
} from "./lib/wire";
import { Panel } from "./components/Panel";
import { StatusBar } from "./components/StatusBar";
import { OrderBook } from "./components/OrderBook";
import { OrderTicket } from "./components/OrderTicket";
import { SorPanel } from "./components/SorPanel";
import { Working, type Leg, type LogicalOrder } from "./components/Working";
import { Fills } from "./components/Fills";
import { Strategies } from "./components/Strategies";
import { Ops } from "./components/Ops";
import { useStream, type StreamEvent } from "./lib/useStream";
import type { Book, Fill } from "./lib/types";
import { fetchBook, type OrderRequest, type OrderResponse } from "./lib/api";
import { time } from "./lib/format";

type Tab = "trade" | "orders" | "strategies" | "ops";

/** 호가를 다시 읽는 간격. 원장은 호가 변화를 밀어 보내지 않는다(T6-04). */
const BOOK_POLL_MS = 1000;

function legOf(req: OrderRequest, res: OrderResponse): Leg {
  const state: Leg["state"] =
    res.outcome === "IN_DOUBT"
      ? "IN_DOUBT"
      : res.outcome === "REJECTED"
        ? "REJECTED"
        : res.status === STATUS_FILLED
          ? "DONE"
          : "LIVE";
  return {
    market: marketName(req.market),
    exchOrderId: res.orderId,
    price: res.filledQty > 0 ? res.avgPrice : req.price,
    qty: req.qty,
    filled: res.filledQty,
    state,
    note: res.outcome === "REJECTED" ? reasonText(res.reason) : undefined,
  };
}

const TABS: { id: Tab; label: string }[] = [
  { id: "trade", label: "거래" },
  { id: "orders", label: "주문·체결" },
  { id: "strategies", label: "전략 비교" },
  { id: "ops", label: "관제" },
];

export default function App() {
  const [tab, setTab] = useState<Tab>("trade");
  const [events, setEvents] = useState(0);
  const [ledgerDown, setLedgerDown] = useState<string | null>(null);
  const [price, setPrice] = useState(70000);
  const [fills, setFills] = useState<Fill[]>([]);
  const [orders, setOrders] = useState<LogicalOrder[]>([]);
  const [books, setBooks] = useState<Book[]>([]);
  const [bookError, setBookError] = useState<string | null>(null);
  const [bookTick, setBookTick] = useState(0);

  // 원장의 실제 호가창을 읽는다. 주문 결과가 오면 기다리지 않고 바로 다시 읽는다.
  useEffect(() => {
    let alive = true;
    const load = () =>
      Promise.all([fetchBook(MARKET_KRX), fetchBook(MARKET_NXT)])
        .then((b) => {
          if (!alive) return;
          setBooks(b);
          setBookError(null);
        })
        .catch((e: unknown) => alive && setBookError(String(e)));
    load();
    const t = window.setInterval(load, BOOK_POLL_MS);
    return () => {
      alive = false;
      window.clearInterval(t);
    };
  }, [bookTick]);

  const onEvent = useCallback((e: StreamEvent) => {
    setEvents((n) => n + 1);
    if (e.kind === "ledger-down") setLedgerDown(String(e.payload ?? "원인 미상"));
    if (e.kind === "ledger-up") setLedgerDown(null);
    if (e.kind === "order") {
      const { request: req, result: res } = e.payload as {
        request: OrderRequest;
        result: OrderResponse;
      };
      setOrders((prev) =>
        [
          {
            clOrdId: req.clOrdId,
            symbol: req.symbol,
            side: req.side as Side,
            price: req.price,
            qty: req.qty,
            legs: [legOf(req, res)],
          },
          ...prev.filter((o) => o.clOrdId !== req.clOrdId),
        ].slice(0, 200),
      );
      setBookTick((n) => n + 1);
    }
    if (e.kind === "fill") {
      const p = e.payload as {
        market: number;
        side: Side;
        price: number;
        qty: number;
        clOrdId: number;
      };
      setFills((prev) =>
        [
          {
            at: time(),
            market: marketName(p.market),
            side: p.side,
            price: p.price,
            qty: p.qty,
            clOrdId: p.clOrdId,
          },
          ...prev,
        ].slice(0, 200),
      );
    }
  }, []);

  const { state, attempt } = useStream(onEvent);

  const bestOverall = useMemo(() => {
    const asks = books.map((b) => b.asks[0]?.price).filter(Boolean) as number[];
    const bids = books.map((b) => b.bids[0]?.price).filter(Boolean) as number[];
    return { ask: Math.min(...asks), bid: Math.max(...bids) };
  }, [books]);

  return (
    <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
      <StatusBar state={state} attempt={attempt} ledgerDown={ledgerDown} />

      <nav
        style={{
          display: "flex",
          gap: 2,
          padding: "0 var(--s-4)",
          background: "var(--bg-panel)",
          borderBottom: "1px solid var(--line)",
          flex: "0 0 auto",
        }}
      >
        {TABS.map((t) => (
          <button
            key={t.id}
            onClick={() => setTab(t.id)}
            style={{
              border: "none",
              borderRadius: 0,
              background: "transparent",
              padding: "10px var(--s-4)",
              fontSize: 13,
              fontWeight: tab === t.id ? 700 : 500,
              color: tab === t.id ? "var(--text)" : "var(--text-faint)",
              borderBottom: `2px solid ${tab === t.id ? "var(--buy)" : "transparent"}`,
            }}
          >
            {t.label}
          </button>
        ))}
      </nav>

      <main style={{ flex: 1, minHeight: 0, padding: "var(--s-3)" }}>
        {tab === "trade" && (
          <div
            style={{
              display: "grid",
              gridTemplateColumns: "minmax(0, 1fr) 300px 320px",
              gap: "var(--s-3)",
              height: "100%",
            }}
          >
            <Panel title="호가창 · 005930 삼성전자" pad={false}>
              {bookError && (
                <div style={{ padding: "8px var(--s-3)", fontSize: 12, color: "var(--danger)" }}>
                  원장 호가를 읽지 못했다 — 원장(ledgerd)과 채널계가 떠 있는지 확인 ({bookError})
                </div>
              )}
              <div
                style={{
                  display: "grid",
                  gridTemplateColumns: "1fr 1fr",
                  gap: 1,
                  background: "var(--line-soft)",
                }}
              >
                {books.map((b) => (
                  <div key={b.market} style={{ background: "var(--bg-panel)" }}>
                    <OrderBook book={b} onPick={setPrice} bestOverall={bestOverall} />
                  </div>
                ))}
              </div>
            </Panel>
            <Panel title="SOR 판단">
              <SorPanel books={books} side={SIDE_BUY} />
            </Panel>
            <Panel title="주문">
              <OrderTicket price={price} onPriceChange={setPrice} />
            </Panel>
          </div>
        )}

        {tab === "orders" && (
          <div
            style={{
              display: "grid",
              gridTemplateColumns: "1fr 1fr",
              gap: "var(--s-3)",
              height: "100%",
            }}
          >
            <Panel title="주문 내역 (원장 응답)" pad={false}>
              <Working orders={orders} />
            </Panel>
            <Panel title="체결 내역" pad={false}>
              <Fills fills={fills} />
            </Panel>
          </div>
        )}

        {tab === "strategies" && (
          <Panel title="전략별 집행 품질 · Phase 2 측정 결과">
            <Strategies />
          </Panel>
        )}

        {tab === "ops" && (
          <Panel title="관제">
            <Ops wsState={state} ledgerDown={ledgerDown} events={events} />
          </Panel>
        )}
      </main>
    </div>
  );
}
