import { useState } from "react";
import {
  SIDE_BUY,
  marketName,
  orderTypeText,
  sideText,
  statusText,
  STATUS_REJECTED,
} from "../lib/wire";
import { won, qty as fq } from "../lib/format";
import type { LocalReject, OrderView } from "../lib/types";

const MARKET_COLOR: Record<string, string> = {
  KRX: "var(--krx)",
  NXT: "var(--nxt)",
  SOR: "var(--ok)",
};

/**
 * 원장은 일부 체결 뒤 취소한 주문을 "부분 체결"로 둔다(T7-01). 끝난 주문인데 그대로 보이면
 * 아직 체결을 기다리는 것처럼 읽힌다.
 */
const stateLabel = (o: OrderView) =>
  o.done && o.canceled > 0
    ? o.filled > 0
      ? "부분 체결 후 취소"
      : "취소"
    : statusText(o.status);

/**
 * 주문 내역(T7-04). 원장이 알려 준 상태를 그대로 보인다 — 나중 체결·취소가 방송으로 들어오면 바뀐다.
 * 펼치면 논리 주문 하나가 시장별로 나간 물리 주문(다리)이 나온다.
 */
export function Working({
  orders,
  rejects,
  onCancel,
}: {
  orders: OrderView[];
  rejects: LocalReject[];
  onCancel: (orderId: number) => Promise<{ ok: boolean; message: string }>;
}) {
  const [open, setOpen] = useState<number | null>(null);
  const [onlyWorking, setOnlyWorking] = useState(false);
  const [busy, setBusy] = useState<number | null>(null);
  const [notes, setNotes] = useState<Record<number, { ok: boolean; message: string }>>({});

  const shown = onlyWorking ? orders.filter((o) => !o.done) : orders;

  const cancel = async (orderId: number) => {
    setBusy(orderId);
    const r = await onCancel(orderId);
    setNotes((n) => ({ ...n, [orderId]: r }));
    setBusy(null);
  };

  return (
    <div>
      <label
        style={{
          display: "flex",
          alignItems: "center",
          gap: 6,
          padding: "8px var(--s-3)",
          fontSize: 11,
          color: "var(--text-dim)",
          borderBottom: "1px solid var(--line-soft)",
        }}
      >
        <input
          type="checkbox"
          checked={onlyWorking}
          onChange={(e) => setOnlyWorking(e.target.checked)}
          style={{ width: "auto" }}
        />
        미체결만 보기
      </label>

      {shown.length === 0 && rejects.length === 0 && (
        <div style={{ padding: "var(--s-5)", textAlign: "center", color: "var(--text-faint)", fontSize: 12 }}>
          주문이 없다
        </div>
      )}

      {shown.map((o) => {
        const ratio = o.qty > 0 ? o.filled / o.qty : 0;
        const isOpen = open === o.orderId;
        const note = notes[o.orderId];
        const req = marketName(o.market);

        return (
          <div key={o.orderId} style={{ borderBottom: "1px solid var(--line-soft)" }}>
            <div style={{ display: "flex", alignItems: "center", gap: 8, padding: "10px var(--s-3) 6px" }}>
              <button
                onClick={() => setOpen(isOpen ? null : o.orderId)}
                aria-expanded={isOpen}
                style={{
                  flex: 1,
                  border: "none",
                  background: "transparent",
                  padding: 0,
                  display: "flex",
                  alignItems: "center",
                  gap: 8,
                  textAlign: "left",
                  minWidth: 0,
                }}
              >
                <span style={{ color: "var(--text-faint)", fontSize: 11 }}>{isOpen ? "▾" : "▸"}</span>
                <span style={{ color: o.side === SIDE_BUY ? "var(--buy)" : "var(--sell)", fontWeight: 700, fontSize: 12 }}>
                  {sideText(o.side)}
                </span>
                <span style={{ fontSize: 11, color: MARKET_COLOR[req], fontWeight: 700 }}>{req}</span>
                <span style={{ fontSize: 11, color: "var(--text-faint)" }}>{orderTypeText(o.type)}</span>
                <span className="num" style={{ fontSize: 12, color: "var(--text-dim)" }}>
                  {won(o.price)} × {fq(o.qty)}
                </span>
                <span style={{ flex: 1 }} />
                <span className="num" style={{ fontSize: 11, color: "var(--text-dim)" }}>
                  {fq(o.filled)}/{fq(o.qty)}
                  {o.filled > 0 && ` · 평균 ${won(o.avgPrice)}`}
                </span>
                <span style={{ fontSize: 11, color: o.done ? "var(--text-faint)" : "var(--ok)" }}>
                  {stateLabel(o)}
                </span>
              </button>
              {!o.done && (
                <button
                  onClick={() => cancel(o.orderId)}
                  disabled={busy === o.orderId}
                  style={{ padding: "3px 10px", fontSize: 11 }}
                >
                  {busy === o.orderId ? "취소 중…" : "취소"}
                </button>
              )}
            </div>
            <div style={{ height: 3, margin: "0 var(--s-3) 8px", background: "var(--line)", borderRadius: 2 }}>
              <div
                style={{
                  width: `${ratio * 100}%`,
                  height: "100%",
                  borderRadius: 2,
                  background: o.side === SIDE_BUY ? "var(--buy)" : "var(--sell)",
                }}
              />
            </div>
            {note && (
              <div style={{ padding: "0 var(--s-3) 8px", fontSize: 11, color: note.ok ? "var(--ok)" : "var(--danger)" }}>
                {note.message}
              </div>
            )}

            {isOpen && (
              <div style={{ padding: "0 var(--s-3) 10px calc(var(--s-3) + 12px)", fontSize: 11 }}>
                <div style={{ color: "var(--text-faint)", marginBottom: 4 }} className="num">
                  주문번호 {o.orderId} · 체결 {fq(o.filled)} · 취소 {fq(o.canceled)} · 대기 {fq(o.working)}
                </div>
                {o.legs.length === 0 && <div style={{ color: "var(--text-faint)" }}>시장으로 나간 몫이 없다</div>}
                {o.legs.map((l) => (
                  <div
                    key={l.market}
                    style={{
                      display: "flex",
                      alignItems: "center",
                      gap: 8,
                      padding: "6px var(--s-3)",
                      background: "var(--bg)",
                      border: "1px solid var(--line-soft)",
                      borderRadius: "var(--r-sm)",
                      marginTop: 4,
                    }}
                  >
                    <span style={{ color: MARKET_COLOR[marketName(l.market)], fontWeight: 700 }}>
                      {marketName(l.market)}
                    </span>
                    <span className="num" style={{ color: "var(--text-dim)" }}>보냄 {fq(l.sent)}</span>
                    <span style={{ flex: 1 }} />
                    <span className="num">체결 {fq(l.filled)}{l.filled > 0 && ` @ ${won(l.avgPrice)}`}</span>
                    <span className="num" style={{ color: "var(--text-faint)" }}>취소 {fq(l.canceled)}</span>
                  </div>
                ))}
              </div>
            )}
          </div>
        );
      })}

      {rejects.map((r) => (
        <div
          key={`r${r.clOrdId}`}
          style={{
            display: "flex",
            alignItems: "center",
            gap: 8,
            padding: "10px var(--s-3)",
            borderBottom: "1px solid var(--line-soft)",
            fontSize: 12,
          }}
        >
          <span style={{ color: r.side === SIDE_BUY ? "var(--buy)" : "var(--sell)", fontWeight: 700 }}>
            {sideText(r.side)}
          </span>
          <span className="num" style={{ color: "var(--text-dim)" }}>
            {won(r.price)} × {fq(r.qty)}
          </span>
          <span style={{ flex: 1 }} />
          <span style={{ fontSize: 11, color: r.outcome === "IN_DOUBT" ? "var(--warn)" : "var(--danger)" }}>
            {r.outcome === "IN_DOUBT" ? "확인 필요" : statusText(STATUS_REJECTED)} · {r.reason}
          </span>
        </div>
      ))}
    </div>
  );
}
