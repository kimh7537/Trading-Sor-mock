import { useState } from "react";
import { SIDE_BUY, type MarketName, type Side } from "../lib/wire";
import { won, qty as fq } from "../lib/format";

export interface Leg {
  market: MarketName;
  exchOrderId: number;
  price: number;
  qty: number;
  filled: number;
  state: "PENDING" | "LIVE" | "DONE" | "REJECTED" | "IN_DOUBT";
  /** 거절 사유 등 덧붙일 말 */
  note?: string;
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
  DONE: { c: "var(--text-dim)", t: "전량 체결" },
  REJECTED: { c: "var(--danger)", t: "거절" },
  IN_DOUBT: { c: "var(--warn)", t: "확인 필요" },
};

/**
 * 주문 내역. 펼치면 원장의 답(시장·주문번호·체결 수량·상태)이 나온다.
 *
 * ponytail: 원장 응답에는 시장별로 나뉜 물리 주문이 없어 한 줄만 보인다. SOR이 여러
 * 시장으로 나눈 내역을 보이려면 응답 전문에 다리 목록을 실어야 한다.
 */
export function Working({ orders }: { orders: LogicalOrder[] }) {
  const [open, setOpen] = useState<number | null>(orders[0]?.clOrdId ?? null);

  if (orders.length === 0) {
    return (
      <div style={{ padding: "var(--s-5)", textAlign: "center", color: "var(--text-faint)", fontSize: 12 }}>
        주문이 없다
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
                    <span style={{ color: l.market === "KRX" ? "var(--krx)" : l.market === "NXT" ? "var(--nxt)" : "var(--ok)", fontWeight: 700 }}>
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
                    <span style={{ color: STATE_STYLE[l.state].c }} title={l.note}>
                      {STATE_STYLE[l.state].t}
                      {l.note && ` · ${l.note}`}
                    </span>
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
