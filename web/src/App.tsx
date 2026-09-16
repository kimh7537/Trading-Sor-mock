import { useCallback, useMemo, useState } from "react";
import { Panel } from "./components/Panel";
import { StatusBar } from "./components/StatusBar";
import { OrderBook } from "./components/OrderBook";
import { OrderTicket } from "./components/OrderTicket";
import { SorPanel } from "./components/SorPanel";
import { useStream, type StreamEvent } from "./lib/useStream";
import { sampleBooks } from "./lib/types";
import { time } from "./lib/format";

interface LogLine {
  at: string;
  kind: string;
  text: string;
}

export default function App() {
  const [log, setLog] = useState<LogLine[]>([]);
  const [ledgerDown, setLedgerDown] = useState<string | null>(null);
  const [price, setPrice] = useState(70000);

  const books = useMemo(() => sampleBooks(), []);

  const onEvent = useCallback((e: StreamEvent) => {
    if (e.kind === "ledger-down") setLedgerDown(String(e.payload ?? "원인 미상"));
    if (e.kind === "ledger-up") setLedgerDown(null);
    setLog((p) =>
      [{ at: time(), kind: e.kind, text: JSON.stringify(e.payload) }, ...p].slice(0, 200),
    );
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

      <main
        style={{
          flex: 1,
          minHeight: 0,
          display: "grid",
          gridTemplateColumns: "minmax(0, 1fr) 300px 320px",
          gap: "var(--s-3)",
          padding: "var(--s-3)",
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

        <div style={{ display: "grid", gridTemplateRows: "auto 1fr", gap: "var(--s-3)", minHeight: 0 }}>
          <Panel title="SOR 판단">
            <SorPanel books={books} side={1} />
          </Panel>
          <Panel title="실시간" right={<span className="num">{log.length}</span>} pad={false}>
            {log.length === 0 ? (
              <div style={{ padding: "var(--s-5)", color: "var(--text-faint)", fontSize: 12, textAlign: "center" }}>
                아직 들어온 것이 없다
              </div>
            ) : (
              <ul style={{ listStyle: "none", margin: 0, padding: 0 }}>
                {log.map((l, i) => (
                  <li
                    key={i}
                    style={{
                      display: "flex",
                      gap: "var(--s-2)",
                      padding: "6px var(--s-3)",
                      borderBottom: "1px solid var(--line-soft)",
                      fontSize: 11,
                    }}
                  >
                    <span className="num" style={{ color: "var(--text-faint)" }}>{l.at}</span>
                    <span style={{ color: "var(--text-dim)" }}>{l.kind}</span>
                  </li>
                ))}
              </ul>
            )}
          </Panel>
        </div>

        <Panel title="주문">
          <OrderTicket price={price} onPriceChange={setPrice} />
        </Panel>
      </main>
    </div>
  );
}
