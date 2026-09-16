import type { Book } from "../lib/types";
import { won } from "../lib/format";

/**
 * SOR가 왜 그렇게 나눴는지를 보여 준다.
 * 총점만 보이면 "가격 때문인가 수수료 때문인가"를 영원히 알 수 없다(T2-12).
 */
export function SorPanel({ books, side }: { books: Book[]; side: 1 | 2 }) {
  const isBuy = side === 1;

  // 매수는 낮은 매도호가가, 매도는 높은 매수호가가 유리하다.
  const rows = books.map((b) => {
    const top = isBuy ? b.asks[0] : b.bids[0];
    return { market: b.market, price: top?.price ?? 0, qty: top?.qty ?? 0 };
  });

  const best = isBuy
    ? Math.min(...rows.map((r) => r.price))
    : Math.max(...rows.map((r) => r.price));

  const totalQty = rows.reduce((s, r) => s + r.qty, 0);

  return (
    <div style={{ display: "grid", gap: "var(--s-4)" }}>
      <div style={{ display: "grid", gap: "var(--s-2)" }}>
        {rows.map((r) => {
          const isBest = r.price === best;
          const share = totalQty > 0 ? r.qty / totalQty : 0;
          const diff = r.price - best;

          return (
            <div
              key={r.market}
              style={{
                padding: "10px var(--s-3)",
                borderRadius: "var(--r-sm)",
                border: `1px solid ${isBest ? "#2f4a3a" : "var(--line-soft)"}`,
                background: isBest ? "rgba(23,201,100,0.08)" : "var(--bg)",
                display: "grid",
                gap: 8,
              }}
            >
              <div style={{ display: "flex", justifyContent: "space-between", alignItems: "center" }}>
                <span style={{ fontSize: 12, fontWeight: 700 }}>
                  {r.market}
                  {isBest && (
                    <span style={{ marginLeft: 6, fontSize: 10, color: "var(--ok)" }}>
                      유리
                    </span>
                  )}
                </span>
                <span className="num" style={{ fontWeight: 600 }}>
                  {won(r.price)}
                  {diff !== 0 && (
                    <span style={{ color: "var(--text-faint)", fontSize: 11, marginLeft: 6 }}>
                      {diff > 0 ? "+" : ""}
                      {won(diff)}
                    </span>
                  )}
                </span>
              </div>

              {/* 배분 비중을 막대로. 숫자보다 먼저 읽힌다 */}
              <div style={{ height: 4, background: "var(--line)", borderRadius: 2 }}>
                <div
                  style={{
                    width: `${share * 100}%`,
                    height: "100%",
                    borderRadius: 2,
                    background: r.market === "KRX" ? "var(--krx)" : "var(--nxt)",
                  }}
                />
              </div>
              <div style={{ display: "flex", justifyContent: "space-between", fontSize: 11, color: "var(--text-dim)" }}>
                <span>체결 가능</span>
                <span className="num">
                  {r.qty.toLocaleString("ko-KR")}주 · {(share * 100).toFixed(0)}%
                </span>
              </div>
            </div>
          );
        })}
      </div>

      <p
        style={{
          margin: 0,
          padding: "10px var(--s-3)",
          background: "var(--bg)",
          border: "1px solid var(--line-soft)",
          borderRadius: "var(--r-sm)",
          fontSize: 11,
          lineHeight: 1.7,
          color: "var(--text-faint)",
        }}
      >
        Phase 2 측정: 복수시장 라우팅의 이득은 단가보다 <b style={{ color: "var(--text-dim)" }}>체결률</b>에서
        먼저 온다. BALANCED에서 KRX 단독 53% → 라우팅 100%.
      </p>
    </div>
  );
}
