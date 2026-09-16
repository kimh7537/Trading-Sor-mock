import { useCallback, useMemo, useState } from "react";
import { Panel } from "./components/Panel";
import { StatusBar } from "./components/StatusBar";
import { OrderBook } from "./components/OrderBook";
import { OrderTicket } from "./components/OrderTicket";
import { SorPanel } from "./components/SorPanel";
import { Working, type LogicalOrder } from "./components/Working";
import { Fills } from "./components/Fills";
import { Strategies } from "./components/Strategies";
import { Ops } from "./components/Ops";
import { useStream, type StreamEvent } from "./lib/useStream";
import { sampleBooks, type Fill } from "./lib/types";
import { time } from "./lib/format";

type Tab = "trade" | "orders" | "strategies" | "ops";

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
  const [orders] = useState<LogicalOrder[]>([]);

  const books = useMemo(() => sampleBooks(), []);

  const onEvent = useCallback((e: StreamEvent) => {
    setEvents((n) => n + 1);
    if (e.kind === "ledger-down") setLedgerDown(String(e.payload ?? "원인 미상"));
    if (e.kind === "ledger-up") setLedgerDown(null);
    if (e.kind === "fill") {
      const p = e.payload as Partial<Fill>;
      setFills((prev) =>
        [
          {
            at: time(),
            market: p.market ?? "KRX",
            side: p.side ?? 1,
            price: p.price ?? 0,
            qty: p.qty ?? 0,
            clOrdId: p.clOrdId ?? 0,
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
              <SorPanel books={books} side={1} />
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
            <Panel title="미체결 (논리 → 물리)" pad={false}>
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
