import { useMemo, useState } from "react";
import { SIDE_BUY } from "./lib/wire";
import { Panel } from "./components/Panel";
import { StatusBar } from "./components/StatusBar";
import { OrderBook } from "./components/OrderBook";
import { OrderTicket } from "./components/OrderTicket";
import { SorPanel } from "./components/SorPanel";
import { Working } from "./components/Working";
import { Fills } from "./components/Fills";
import { Strategies } from "./components/Strategies";
import { Ops } from "./components/Ops";
import { useTrading } from "./lib/useTrading";
import type { Book } from "./lib/types";

type Tab = "trade" | "orders" | "strategies" | "ops";

const TABS: { id: Tab; label: string }[] = [
  { id: "trade", label: "거래" },
  { id: "orders", label: "주문·체결" },
  { id: "strategies", label: "전략 비교" },
  { id: "ops", label: "관제" },
];

export default function App() {
  const t = useTrading();
  const [tab, setTab] = useState<Tab>("trade");
  const [price, setPrice] = useState(70000);

  const books = useMemo(
    () => [t.books.KRX, t.books.NXT].filter((b): b is Book => !!b),
    [t.books],
  );

  const bestOverall = useMemo(() => {
    const asks = books.map((b) => b.asks[0]?.price).filter(Boolean) as number[];
    const bids = books.map((b) => b.bids[0]?.price).filter(Boolean) as number[];
    return { ask: Math.min(...asks), bid: Math.max(...bids) };
  }, [books]);

  return (
    <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
      <StatusBar state={t.ws.state} attempt={t.ws.attempt} ledgerDown={t.ledgerDown} balance={t.balance} />

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
        {TABS.map((x) => (
          <button
            key={x.id}
            onClick={() => setTab(x.id)}
            style={{
              border: "none",
              borderRadius: 0,
              background: "transparent",
              padding: "10px var(--s-4)",
              fontSize: 13,
              fontWeight: tab === x.id ? 700 : 500,
              color: tab === x.id ? "var(--text)" : "var(--text-faint)",
              borderBottom: `2px solid ${tab === x.id ? "var(--buy)" : "transparent"}`,
            }}
          >
            {x.label}
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
              {t.bookError && (
                <div style={{ padding: "8px var(--s-3)", fontSize: 12, color: "var(--danger)" }}>
                  원장 호가를 읽지 못했다 — 원장(ledgerd)과 채널계가 떠 있는지 확인 ({t.bookError})
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
              <OrderTicket
                price={price}
                onPriceChange={setPrice}
                submit={t.submit}
                available={t.balance?.available ?? null}
              />
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
            <Panel title="주문 내역 (원장 상태)" pad={false}>
              <Working orders={t.orders} rejects={t.rejects} onCancel={t.cancel} />
            </Panel>
            <Panel title="체결 내역" pad={false}>
              <Fills fills={t.fills} />
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
            <Ops wsState={t.ws.state} ledgerDown={t.ledgerDown} events={t.events} />
          </Panel>
        )}
      </main>
    </div>
  );
}
