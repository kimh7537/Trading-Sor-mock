import type { ConnState } from "../lib/useStream";
import type { Balance } from "../lib/types";
import { won } from "../lib/format";

const LABEL: Record<ConnState, string> = {
  connecting: "연결 중",
  open: "실시간",
  closed: "끊김",
};

const COLOR: Record<ConnState, string> = {
  connecting: "var(--warn)",
  open: "var(--ok)",
  closed: "var(--danger)",
};

export function StatusBar({
  state,
  attempt,
  ledgerDown,
  balance,
}: {
  state: ConnState;
  attempt: number;
  ledgerDown: string | null;
  balance: Balance | null;
}) {
  return (
    <header
      style={{
        display: "flex",
        alignItems: "center",
        gap: "var(--s-4)",
        padding: "0 var(--s-5)",
        height: 52,
        background: "var(--bg-panel)",
        borderBottom: "1px solid var(--line)",
        flex: "0 0 auto",
      }}
    >
      <div style={{ display: "flex", alignItems: "baseline", gap: 10 }}>
        <strong style={{ fontSize: 15, letterSpacing: "-0.01em" }}>mock-sor</strong>
        <span style={{ fontSize: 11, color: "var(--text-faint)" }}>
          복수시장 주문 집행
        </span>
      </div>

      <div style={{ flex: 1 }} />

      {balance && (
        <span style={{ display: "inline-flex", gap: 14, fontSize: 12, color: "var(--text-dim)" }}>
          <span>
            예수금 <b className="num" style={{ color: "var(--text)" }}>{won(balance.cash)}</b>
          </span>
          <span>
            묶인 금액 <b className="num" style={{ color: "var(--text)" }}>{won(balance.reserved)}</b>
          </span>
          <span>
            주문 가능 <b className="num" style={{ color: "var(--ok)" }}>{won(balance.available)}</b>
          </span>
        </span>
      )}

      {ledgerDown && (
        <span
          style={{
            display: "inline-flex",
            alignItems: "center",
            gap: 6,
            fontSize: 12,
            color: "var(--danger)",
            background: "rgba(255,77,79,0.12)",
            border: "1px solid rgba(255,77,79,0.3)",
            borderRadius: 999,
            padding: "4px 10px",
          }}
        >
          원장 끊김 · {ledgerDown}
        </span>
      )}

      <span
        style={{
          display: "inline-flex",
          alignItems: "center",
          gap: 7,
          fontSize: 12,
          color: "var(--text-dim)",
        }}
      >
        <span
          style={{
            width: 7,
            height: 7,
            borderRadius: "50%",
            background: COLOR[state],
            boxShadow: state === "open" ? `0 0 0 3px ${COLOR[state]}22` : "none",
          }}
        />
        {LABEL[state]}
        {state === "closed" && attempt > 0 && (
          <span style={{ color: "var(--text-faint)" }}>· 재시도 {attempt}</span>
        )}
      </span>
    </header>
  );
}
