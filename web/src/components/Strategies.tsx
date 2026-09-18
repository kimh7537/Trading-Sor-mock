/**
 * Phase 2의 측정 결과. 이 프로젝트의 최종 산출물이다
 * (`bench/results/strategies-2026-09-16.md`, 시드 20260916 한 장면).
 *
 * bp는 **슬리피지**(접수 시점 통합 최우선호가 대비)다. 라우팅 줄은 SPLIT 전략의 값이다 —
 * 복수시장 전략 셋의 체결률은 같고 슬리피지만 0~2bp로 다르다.
 */
const DATA = [
  { s: "BALANCED", krxBp: 11, krxFill: 53.46, sorBp: 1, sorFill: 100.0 },
  { s: "KRX_THIN", krxBp: 15, krxFill: 5.36, sorBp: 2, sorFill: 72.83 },
  { s: "NXT_THIN", krxBp: 11, krxFill: 53.46, sorBp: 1, sorFill: 58.71 },
  { s: "CROSSED", krxBp: 0, krxFill: 21.28, sorBp: 0, sorFill: 21.28 },
];

function Bar({ v, max, color }: { v: number; max: number; color: string }) {
  return (
    <div style={{ height: 6, background: "var(--line)", borderRadius: 3, minWidth: 60 }}>
      <div
        style={{
          width: `${Math.min(100, (v / max) * 100)}%`,
          height: "100%",
          borderRadius: 3,
          background: color,
        }}
      />
    </div>
  );
}

export function Strategies() {
  return (
    <div style={{ display: "grid", gap: "var(--s-3)" }}>
      <p style={{ margin: 0, fontSize: 12, lineHeight: 1.7, color: "var(--text-dim)" }}>
        시나리오마다 같은 주문 흐름을 KRX 단독과 복수시장 라우팅으로 각각 집행해
        <b style={{ color: "var(--text)" }}> 슬리피지(bp)</b>와
        <b style={{ color: "var(--text)" }}> 체결률</b>을 잰 결과다.
      </p>

      {DATA.map((d) => (
        <div
          key={d.s}
          style={{
            padding: "10px var(--s-3)",
            border: "1px solid var(--line-soft)",
            borderRadius: "var(--r-sm)",
            background: "var(--bg)",
            display: "grid",
            gap: 8,
          }}
        >
          <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
            <strong style={{ fontSize: 12 }}>{d.s}</strong>
            <span
              className="num"
              style={{
                fontSize: 11,
                color: d.sorFill > d.krxFill ? "var(--ok)" : "var(--text-faint)",
              }}
            >
              체결률 {d.krxFill.toFixed(1)}% → {d.sorFill.toFixed(1)}%
            </span>
          </div>

          <div style={{ display: "grid", gridTemplateColumns: "54px 1fr 54px", gap: 8, alignItems: "center", fontSize: 11 }}>
            <span style={{ color: "var(--text-faint)" }}>KRX 단독</span>
            <Bar v={d.krxFill} max={100} color="var(--krx)" />
            <span className="num" style={{ textAlign: "right", color: "var(--text-dim)" }}>
              +{d.krxBp}bp
            </span>

            <span style={{ color: "var(--text-faint)" }}>SPLIT</span>
            <Bar v={d.sorFill} max={100} color="var(--nxt)" />
            <span className="num" style={{ textAlign: "right", color: "var(--text-dim)" }}>
              +{d.sorBp}bp
            </span>
          </div>
        </div>
      ))}

      <p
        style={{
          margin: 0,
          padding: "10px var(--s-3)",
          background: "rgba(23,201,100,0.08)",
          border: "1px solid #2f4a3a",
          borderRadius: "var(--r-sm)",
          fontSize: 12,
          lineHeight: 1.7,
        }}
      >
        <b>결론</b> — 복수시장 라우팅의 이득은 단가보다 <b>체결률</b>에서 먼저 온다.
        <br />
        <span style={{ color: "var(--text-dim)" }}>
          NXT_THIN에서 라우팅의 평균 단가가 KRX 단독보다 1bp 비싼 것은 전략이 나빠서가
          아니라 <b>더 많이 채우느라</b>(체결률 53% → 59%) 비싼 호가까지 갔기 때문이다.
          위 막대 옆 bp는 단가 차이가 아니라 슬리피지다.
        </span>
        <br />
        <span style={{ color: "var(--text-dim)" }}>
          <b>시드 하나의 한 장면이다.</b> README의 표는 시드 30개를 돌린{" "}
          <code>quality-2026-09-16.md</code>의 중앙값이라 수치가 다르다 — 같은 것을 두 번 잰
          것이 아니라 서로 다른 측정이다.
        </span>
      </p>
    </div>
  );
}
