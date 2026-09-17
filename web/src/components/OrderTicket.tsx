import { useState } from "react";
import {
  MARKET_AUTO,
  MARKET_KRX,
  MARKET_NXT,
  ORDER_FOK,
  ORDER_IOC,
  ORDER_LIMIT,
  SIDE_BUY,
  SIDE_SELL,
  STATUS_FILLED,
  reasonText,
  type Side,
} from "../lib/wire";
import type { OrderResponse } from "../lib/api";
import type { NewOrder } from "../lib/useTrading";
import { won } from "../lib/format";

const OUTCOME_STYLE: Record<string, { color: string; label: string; hint: string }> = {
  ACCEPTED: { color: "var(--ok)", label: "접수됨", hint: "" },
  REJECTED: { color: "var(--danger)", label: "거절됨", hint: "" },
  IN_DOUBT: {
    color: "var(--warn)",
    label: "확인 필요",
    hint: "원장 응답을 못 받았다. 다시 보내면 중복 주문이 될 수 있다 — 호가창에 걸렸는지 먼저 확인한다.",
  },
};

export function OrderTicket({
  price,
  onPriceChange,
  submit,
  available,
}: {
  price: number;
  onPriceChange: (p: number) => void;
  submit: (o: NewOrder) => Promise<OrderResponse>;
  /** 주문 가능 금액. 잔고를 아직 못 읽었으면 null */
  available: number | null;
}) {
  const [side, setSide] = useState<Side>(SIDE_BUY);
  const [qty, setQty] = useState(10);
  const [market, setMarket] = useState<number>(MARKET_AUTO);
  const [type, setType] = useState<number>(ORDER_LIMIT);
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState<OrderResponse | null>(null);

  const isBuy = side === SIDE_BUY;
  const accent = isBuy ? "var(--buy)" : "var(--sell)";
  const notional = price * qty;

  const send = async () => {
    setBusy(true);
    setResult(null);
    try {
      setResult(await submit({ side, type, market, price, qty }));
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
        {([SIDE_BUY, SIDE_SELL] as const).map((s) => {
          const on = side === s;
          const c = s === SIDE_BUY ? "var(--buy)" : "var(--sell)";
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
              {s === SIDE_BUY ? "매수" : "매도"}
            </button>
          );
        })}
      </div>

      <div style={{ display: "grid", gap: 6 }}>
        {label("시장")}
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: "var(--s-2)" }}>
          {[
            { v: MARKET_AUTO, t: "SOR 자동" },
            { v: MARKET_KRX, t: "KRX" },
            { v: MARKET_NXT, t: "NXT" },
          ].map((m) => (
            <button
              key={m.v}
              onClick={() => setMarket(m.v)}
              title={m.v === MARKET_AUTO ? "원장이 두 시장 호가를 보고 유리한 쪽으로 보낸다" : ""}
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
        {label("유형")}
        <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: "var(--s-2)" }}>
          {[
            { v: ORDER_LIMIT, t: "지정가", h: "안 맞은 수량은 호가창에 남는다" },
            { v: ORDER_IOC, t: "IOC", h: "맞는 만큼만 체결하고 나머지는 취소" },
            { v: ORDER_FOK, t: "FOK", h: "전량 체결될 때만 체결, 아니면 전부 취소" },
          ].map((m) => (
            <button
              key={m.v}
              onClick={() => setType(m.v)}
              title={m.h}
              aria-pressed={type === m.v}
              style={{
                padding: "8px 0",
                fontSize: 12,
                fontWeight: 600,
                background: type === m.v ? "var(--bg-raised)" : "transparent",
                borderColor: type === m.v ? "#3d4a63" : "var(--line)",
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
      {isBuy && available !== null && (
        <div style={{ display: "flex", justifyContent: "space-between", fontSize: 11, color: notional > available ? "var(--danger)" : "var(--text-dim)" }}>
          <span>주문 가능 금액</span>
          <span className="num">
            {won(available)}원{notional > available && " · 모자람"}
          </span>
        </div>
      )}

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
          <span style={{ color: "var(--text-dim)" }}>
            {result.outcome === "ACCEPTED"
              ? result.filledQty > 0
                ? `${result.filledQty.toLocaleString("ko-KR")}주 체결 · 평균 ${won(result.avgPrice)}원` +
                  (result.status === STATUS_FILLED ? " · 전량" : " · 나머지는 호가창에 대기")
                : "체결 없음 · 호가창에 대기"
              : result.outcome === "REJECTED"
                ? reasonText(result.reason)
                : result.message}
          </span>
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
