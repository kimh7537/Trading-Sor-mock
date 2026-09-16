import { useCallback, useState } from "react";
import { Panel } from "./components/Panel";
import { StatusBar } from "./components/StatusBar";
import { useStream, type StreamEvent } from "./lib/useStream";
import { time } from "./lib/format";

interface LogLine {
  at: string;
  kind: string;
  text: string;
}

export default function App() {
  const [log, setLog] = useState<LogLine[]>([]);
  const [ledgerDown, setLedgerDown] = useState<string | null>(null);

  const onEvent = useCallback((e: StreamEvent) => {
    if (e.kind === "ledger-down") {
      setLedgerDown(String(e.payload ?? "원인 미상"));
    } else if (e.kind === "ledger-up") {
      setLedgerDown(null);
    }
    setLog((prev) =>
      [{ at: time(), kind: e.kind, text: JSON.stringify(e.payload) }, ...prev].slice(0, 200),
    );
  }, []);

  const { state, attempt } = useStream(onEvent);

  return (
    <div style={{ display: "flex", flexDirection: "column", height: "100%" }}>
      <StatusBar state={state} attempt={attempt} ledgerDown={ledgerDown} />

      <main
        style={{
          flex: 1,
          minHeight: 0,
          display: "grid",
          gridTemplateColumns: "minmax(0, 1fr) 360px",
          gap: "var(--s-4)",
          padding: "var(--s-4)",
        }}
      >
        <Panel title="호가 · SOR">
          <div style={{ color: "var(--text-faint)", fontSize: 13 }}>
            호가창은 T4-07에서 붙는다.
          </div>
        </Panel>

        <Panel title="실시간" right={<span className="num">{log.length}</span>} pad={false}>
          {log.length === 0 ? (
            <div
              style={{
                padding: "var(--s-5)",
                color: "var(--text-faint)",
                fontSize: 13,
                textAlign: "center",
              }}
            >
              아직 들어온 것이 없다
            </div>
          ) : (
            <ul style={{ listStyle: "none", margin: 0, padding: 0 }}>
              {log.map((l, i) => (
                <li
                  key={i}
                  style={{
                    display: "flex",
                    gap: "var(--s-3)",
                    padding: "8px var(--s-4)",
                    borderBottom: "1px solid var(--line-soft)",
                    fontSize: 12,
                  }}
                >
                  <span className="num" style={{ color: "var(--text-faint)" }}>
                    {l.at}
                  </span>
                  <span style={{ color: "var(--text-dim)", minWidth: 72 }}>{l.kind}</span>
                  <span
                    style={{
                      color: "var(--text)",
                      overflow: "hidden",
                      textOverflow: "ellipsis",
                      whiteSpace: "nowrap",
                    }}
                  >
                    {l.text}
                  </span>
                </li>
              ))}
            </ul>
          )}
        </Panel>
      </main>
    </div>
  );
}
