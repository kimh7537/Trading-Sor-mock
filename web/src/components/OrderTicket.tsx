import { useState } from "react";
import { submitOrder, type OrderResponse } from "../lib/api";
import { won } from "../lib/format";

const OUTCOME_STYLE: Record<string, { color: string; label: string; hint: string }> = {
  ACCEPTED: { color: "var(--ok)", label: "접수됨", hint: "" },
  REJECTED: { color: "var(--danger)", label: "거절됨", hint: "" },
  IN_DOUBT: {
    color: "var(--warn)",
    label: "확인 필요",
    hint: "원장 응답을 못 받았다. 다시 보내면 중복 주문이 될 수 있으니 조회로 확인한다.",
  },
};

export function OrderTicket({
  price,
  onPriceChange,
}: {
  price: number;
  onPriceChange: (p: number) => void;
}) {
  const [side, setSide] = useState<1 | 2>(1);
  const [qty, setQty] = useState(10);
  const [market, setMarket] = useState(1);
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState<OrderResponse | null>(null);

  const isBuy = side === 1;
  const accent = isBuy ? "var(--buy)" : "var(--sell)";
  const notional = price * qty;

  const send = async () => {
    setBusy(true);
    setResult(null);
    try {
      const res = await submitOrder({
        account: "123456789012",
        symbol: "005930",
        clOrdId: Date.now() % 1_000_000_000,
        side,
        type: 1,
        market,
        price,
        qty,
      });
      setResult(res);
    } catch {
      setResult({
        outcome: "REJECTED",
        clOrdId: 0,
        orderId: 0,
        reason: -16,
        message: "채널계에 붙지 못했다",
      });
    } finally {
      setBusy(false);
    }
  };

  const label = (t: string) => (
    <span style={{ fontSize: 11, color: "var(--text-dim)" }}>{t}</span>
  );

  return (
    <div style={{ display: "grid", gap: "var(--s-4)" }}>
      {/* 매수/매도. 가장 크게, 색으로 구분한다 */}
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: "var(--s-2)" }}>
        {([1, 2] as const).map((s) => {
          const on = side === s;
          const c = s === 1 ? "var(--buy)" : "var(--sell)";
          return (
            <button
              key={s}
              onClick={() => setSide(s)}
              style={{
                padding: "10px 0",
                fontWeight: 700,
                color: on ? "#fff" : c,
                background: on ? c : "transparent",
                borderColor: on ? c : "var(--line)",
              }}
            >
              {s === 1 ? "매수" : "매도"}
            </button>
          );
        })}
      </div>

      <div style={{ display: "grid", gap: 6 }}>
        {label("시장")}
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: "var(--s-2)" }}>
          {[
            { v: 0, t: "SOR" },
            { v: 1, t: "KRX" },
            { v: 2, t: "NXT" },
          ].map((m) => (
            <button
              key={m.v}
              onClick={() => setMarket(m.v)}
              disabled={m.v === 0}
              title={m.v === 0 ? "자동 배분은 Phase 5에서 붙는다" : ""}
              style={{
                padding: "8px 0",
                fontSize: 12,
                fontWeight: 600,
                background: market === m.v ? "var(--bg-raised)" : "transparent",
                borderColor: market === m.v ? "#3d4a63" : "var(--line)",
              }}
            >
              {m.t}
            </button>
          ))}
        </div>
      </div>

      <div style={{ display: "grid", gap: 6 }}>
        {label("가격 (호가를 클릭해도 담긴다)")}
        <input
          className="num"
          type="number"
          step={100}
          value={price}
          onChange={(e) => onPriceChange(Number(e.target.value))}
        />
      </div>

      <div style={{ display: "grid", gap: 6 }}>
        {label("수량")}
        <input
          className="num"
          type="number"
          min={1}
          value={qty}
          onChange={(e) => setQty(Math.max(1, Number(e.target.value)))}
        />
        <div style={{ display: "flex", gap: 6 }}>
          {[10, 50, 100, 500].map((q) => (
            <button
              key={q}
              onClick={() => setQty(q)}
              style={{ flex: 1, padding: "4px 0", fontSize: 11 }}
            >
              {q}
            </button>
          ))}
        </div>
      </div>

      <div
        style={{
          display: "flex",
          justifyContent: "space-between",
          padding: "10px var(--s-3)",
          background: "var(--bg)",
          border: "1px solid var(--line-soft)",
          borderRadius: "var(--r-sm)",
          fontSize: 12,
        }}
      >
        <span style={{ color: "var(--text-dim)" }}>주문 금액</span>
        <span className="num" style={{ fontWeight: 700 }}>
          {won(notional)}원
        </span>
      </div>

      <button
        onClick={send}
        disabled={busy || price <= 0 || qty <= 0}
        style={{
          padding: "12px 0",
          fontSize: 15,
          fontWeight: 700,
          color: "#fff",
          background: accent,
          borderColor: accent,
        }}
      >
        {busy ? "보내는 중…" : isBuy ? "매수 주문" : "매도 주문"}
      </button>

      {result && (
        <div
          style={{
            padding: "10px var(--s-3)",
            borderRadius: "var(--r-sm)",
            border: `1px solid ${OUTCOME_STYLE[result.outcome].color}55`,
            background: `${OUTCOME_STYLE[result.outcome].color}18`,
            fontSize: 12,
            display: "grid",
            gap: 4,
          }}
        >
          <strong style={{ color: OUTCOME_STYLE[result.outcome].color }}>
            {OUTCOME_STYLE[result.outcome].label}
          </strong>
          <span style={{ color: "var(--text-dim)" }}>{result.message}</span>
          {OUTCOME_STYLE[result.outcome].hint && (
            <span style={{ color: "var(--text-faint)", lineHeight: 1.5 }}>
              {OUTCOME_STYLE[result.outcome].hint}
            </span>
          )}
        </div>
      )}
    </div>
  );
}
