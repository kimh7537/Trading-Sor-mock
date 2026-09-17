import { won, qty as fq } from "../lib/format";
import { SIDE_BUY, SIDE_SELL, sideText } from "../lib/wire";
import type { Fill } from "../lib/types";

const MARKET_COLOR = { KRX: "var(--krx)", NXT: "var(--nxt)", SOR: "var(--ok)" };

export function Fills({ fills }: { fills: Fill[] }) {
  if (fills.length === 0) {
    return (
      <div style={{ padding: "var(--s-5)", textAlign: "center", color: "var(--text-faint)", fontSize: 12 }}>
        체결 내역이 없다
      </div>
    );
  }

  // 매수와 매도를 섞은 평균은 뜻이 없다 — 방향별로 나눈다
  const summary = ([SIDE_BUY, SIDE_SELL] as const).map((side) => {
    const mine = fills.filter((f) => f.side === side);
    const q = mine.reduce((s, f) => s + f.qty, 0);
    const amt = mine.reduce((s, f) => s + f.price * f.qty, 0);
    return { side, q, avg: q > 0 ? Math.round(amt / q) : 0 };
  });

  return (
    <div>
      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          padding: "8px var(--s-3)",
          background: "var(--bg)",
          borderBottom: "1px solid var(--line-soft)",
          fontSize: 11,
        }}
      >
        {summary.map((s) => (
          <span key={s.side}>
            <span style={{ color: s.side === SIDE_BUY ? "var(--buy)" : "var(--sell)" }}>
              {sideText(s.side)} 평균
            </span>{" "}
            <span className="num" style={{ fontWeight: 700 }}>
              {s.q > 0 ? `${won(s.avg)}원 · ${fq(s.q)}주` : "—"}
            </span>
          </span>
        ))}
      </div>
      <ul style={{ listStyle: "none", margin: 0, padding: 0 }}>
        {fills.map((f) => (
          <li
            key={f.id}
            style={{
              display: "flex",
              alignItems: "center",
              gap: 8,
              padding: "7px var(--s-3)",
              borderBottom: "1px solid var(--line-soft)",
              fontSize: 11,
            }}
          >
            <span className="num" style={{ color: "var(--text-faint)" }}>{f.at}</span>
            <span style={{ color: MARKET_COLOR[f.market], fontWeight: 700 }}>
              {f.market}
            </span>
            <span style={{ color: f.side === SIDE_BUY ? "var(--buy)" : "var(--sell)" }}>
              {f.side === SIDE_BUY ? "매수" : "매도"}
            </span>
            <span style={{ flex: 1 }} />
            <span className="num">{won(f.price)}</span>
            <span className="num" style={{ color: "var(--text-dim)", minWidth: 44, textAlign: "right" }}>
              {fq(f.qty)}
            </span>
          </li>
        ))}
      </ul>
    </div>
  );
}
