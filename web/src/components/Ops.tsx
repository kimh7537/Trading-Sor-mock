import type { ConnState } from "../lib/useStream";

interface Row {
  name: string;
  ok: boolean;
  detail: string;
}

/** 관제. 지금 무엇이 살아 있고 무엇이 아닌지만 본다. */
export function Ops({
  wsState,
  ledgerDown,
  events,
}: {
  wsState: ConnState;
  ledgerDown: string | null;
  events: number;
}) {
  const rows: Row[] = [
    {
      name: "채널계 구독",
      ok: wsState === "open",
      detail: wsState === "open" ? "실시간 수신 중" : "끊김 — 자동 재시도",
    },
    {
      name: "원장",
      ok: !ledgerDown,
      detail: ledgerDown ?? "정상",
    },
    {
      name: "받은 사건",
      ok: true,
      detail: `${events}건`,
    },
  ];

  return (
    <div style={{ display: "grid", gap: "var(--s-2)" }}>
      {rows.map((r) => (
        <div
          key={r.name}
          style={{
            display: "flex",
            alignItems: "center",
            gap: 10,
            padding: "10px var(--s-3)",
            background: "var(--bg)",
            border: "1px solid var(--line-soft)",
            borderRadius: "var(--r-sm)",
          }}
        >
          <span
            style={{
              width: 8,
              height: 8,
              borderRadius: "50%",
              background: r.ok ? "var(--ok)" : "var(--danger)",
              flex: "0 0 auto",
            }}
          />
          <span style={{ fontSize: 12, minWidth: 92 }}>{r.name}</span>
          <span style={{ fontSize: 11, color: "var(--text-dim)" }}>{r.detail}</span>
        </div>
      ))}

      <p style={{ margin: 0, fontSize: 11, lineHeight: 1.7, color: "var(--text-faint)" }}>
        원장이 끊기면 화면이 조용히 멈추는 대신 여기와 상단에 표시된다.
        "시장이 조용한 것"과 "우리가 못 받는 것"은 다르다.
      </p>
    </div>
  );
}
