import { useState } from "react";
import { SIDE_BUY, type Side } from "../lib/wire";
import { won, qty as fq } from "../lib/format";

export interface Leg {
  market: "KRX" | "NXT";
  exchOrderId: number;
  price: number;
  qty: number;
  filled: number;
  state: "PENDING" | "LIVE" | "DONE" | "IN_DOUBT";
}

export interface LogicalOrder {
  clOrdId: number;
  symbol: string;
  side: Side;
  price: number;
  qty: number;
  legs: Leg[];
}

const STATE_STYLE: Record<Leg["state"], { c: string; t: string }> = {
  PENDING: { c: "var(--text-faint)", t: "응답 대기" },
  LIVE: { c: "var(--ok)", t: "접수" },
  DONE: { c: "var(--text-dim)", t: "종료" },
  IN_DOUBT: { c: "var(--warn)", t: "확인 필요" },
};

/**
 * 미체결 목록. 논리 주문 하나를 펼치면 시장별 물리 주문이 나온다 —
 * 사용자가 낸 것은 하나인데 실제로 나간 것은 여럿이라는 사실이 보여야 한다.
 */
export function Working({ orders }: { orders: LogicalOrder[] }) {
  const [open, setOpen] = useState<number | null>(orders[0]?.clOrdId ?? null);

  if (orders.length === 0) {
    return (
      <div style={{ padding: "var(--s-5)", textAlign: "center", color: "var(--text-faint)", fontSize: 12 }}>
        미체결 주문이 없다
      </div>
    );
  }

  return (
    <div>
      {orders.map((o) => {
        const filled = o.legs.reduce((s, l) => s + l.filled, 0);
        const ratio = o.qty > 0 ? filled / o.qty : 0;
        const isOpen = open === o.clOrdId;
        const indoubt = o.legs.some((l) => l.state === "IN_DOUBT");

        return (
          <div key={o.clOrdId} style={{ borderBottom: "1px solid var(--line-soft)" }}>
            <button
              onClick={() => setOpen(isOpen ? null : o.clOrdId)}
              style={{
                width: "100%",
                border: "none",
                borderRadius: 0,
                background: "transparent",
                padding: "10px var(--s-3)",
                display: "grid",
                gap: 6,
                textAlign: "left",
              }}
            >
              <div style={{ display: "flex", alignItems: "center", gap: 8 }}>
                <span style={{ color: o.side === SIDE_BUY ? "var(--buy)" : "var(--sell)", fontWeight: 700, fontSize: 12 }}>
                  {o.side === SIDE_BUY ? "매수" : "매도"}
                </span>
                <span style={{ fontSize: 12 }}>{o.symbol}</span>
                <span className="num" style={{ fontSize: 12, color: "var(--text-dim)" }}>
                  {won(o.price)} × {fq(o.qty)}
                </span>
                {indoubt && (
                  <span style={{ fontSize: 10, color: "var(--warn)" }}>확인 필요</span>
                )}
                <span style={{ flex: 1 }} />
                <span className="num" style={{ fontSize: 11, color: "var(--text-dim)" }}>
                  {fq(filled)}/{fq(o.qty)}
                </span>
                <span style={{ color: "var(--text-faint)", fontSize: 11 }}>
                  {isOpen ? "▾" : "▸"}
                </span>
              </div>
              <div style={{ height: 3, background: "var(--line)", borderRadius: 2 }}>
                <div
                  style={{
                    width: `${ratio * 100}%`,
                    height: "100%",
                    borderRadius: 2,
                    background: o.side === SIDE_BUY ? "var(--buy)" : "var(--sell)",
                  }}
                />
              </div>
            </button>

            {isOpen && (
              <div style={{ padding: "0 var(--s-3) 10px calc(var(--s-3) + 12px)" }}>
                {o.legs.map((l) => (
                  <div
                    key={`${l.market}-${l.exchOrderId}`}
                    style={{
                      display: "flex",
                      alignItems: "center",
                      gap: 8,
                      padding: "6px var(--s-3)",
                      background: "var(--bg)",
                      border: "1px solid var(--line-soft)",
                      borderRadius: "var(--r-sm)",
                      marginTop: 4,
                      fontSize: 11,
                    }}
                  >
                    <span style={{ color: l.market === "KRX" ? "var(--krx)" : "var(--nxt)", fontWeight: 700 }}>
                      {l.market}
                    </span>
                    <span className="num" style={{ color: "var(--text-faint)" }}>
                      #{l.exchOrderId || "—"}
                    </span>
                    <span className="num" style={{ color: "var(--text-dim)" }}>
                      {won(l.price)} × {fq(l.qty)}
                    </span>
                    <span style={{ flex: 1 }} />
                    <span className="num">{fq(l.filled)}</span>
                    <span style={{ color: STATE_STYLE[l.state].c }}>{STATE_STYLE[l.state].t}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        );
      })}
    </div>
  );
}
