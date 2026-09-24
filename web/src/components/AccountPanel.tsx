import { useCallback, useEffect, useState } from "react";
import { fetchHistory, fetchPortfolio, type FillRow, type PortfolioView } from "../lib/api";
import { money, moneyUnit, qty as fq } from "../lib/format";
import { marketName, sideText } from "../lib/wire";
import { Panel } from "./Panel";

/**
 * 내 계좌 — 보유, 손익, 거래 내역 (T11-04).
 *
 * **남는 것과 안 남는 것을 화면에서도 구분한다.** 거래 내역과 실현 손익은 파일에
 * 적혀 있어 컴퓨터를 꺼도 그대로다. 평가 손익은 **지금 보이는 시세** 기준이라,
 * 꺼져 있던 동안의 변동은 들어 있지 않다.
 */
export function AccountPanel({ tick }: { tick: number }) {
  const [p, setP] = useState<PortfolioView | null>(null);
  const [rows, setRows] = useState<FillRow[]>([]);
  const [error, setError] = useState<string | null>(null);

  const load = useCallback(() => {
    void Promise.all([fetchPortfolio(), fetchHistory(200)])
      .then(([view, history]) => {
        setP(view);
        setRows(history);
        setError(null);
      })
      .catch((e: unknown) => setError(e instanceof Error ? e.message : "읽지 못했다"));
  }, []);

  /* 체결이 나면 다시 읽는다. tick은 거래 화면이 올려 주는 값이다 */
  useEffect(load, [load, tick]);

  if (error) return <Panel title="내 계좌">{error}</Panel>;
  if (!p) return <Panel title="내 계좌">읽는 중…</Panel>;

  const pct = (p.returnRate * 100).toFixed(2);
  const up = p.returnRate >= 0;
  const pnl = p.realized + p.unrealized;

  return (
    <div className="account">
      <Panel title="평가" sub={`${p.symbol} · ${p.currency === "USD" ? "달러" : "원"}`}>
        <div className="kpis">
          <div className="kpi">
            <span className="k">총 자산</span>
            <span className="v num">{moneyUnit(p.equity)}</span>
          </div>
          <div className="kpi">
            <span className="k">수익률</span>
            <span className="v num" style={{ color: up ? "var(--buy)" : "var(--sell)" }}>
              {up ? "+" : ""}
              {pct}%
            </span>
          </div>
          <div className="kpi">
            <span className="k">손익 합계</span>
            <span className="v num" style={{ color: pnl >= 0 ? "var(--buy)" : "var(--sell)" }}>
              {pnl >= 0 ? "+" : ""}
              {money(pnl)}
            </span>
          </div>
        </div>

        <dl className="rows">
          <div>
            <dt>시작 자금</dt>
            <dd className="num">{moneyUnit(p.seedCash)}</dd>
          </div>
          <div>
            <dt>예수금</dt>
            <dd className="num">{moneyUnit(p.cash)}</dd>
          </div>
          <div>
            <dt>보유</dt>
            <dd className="num">
              {p.qty > 0 ? `${fq(p.qty)}주 · 평균 ${money(p.avgCost)}` : "없음"}
            </dd>
          </div>
          <div>
            <dt>평가 금액</dt>
            <dd className="num">{p.lastPrice > 0 ? moneyUnit(p.marketValue) : "시세 모름"}</dd>
          </div>
          <div>
            <dt>평가 손익</dt>
            <dd
              className="num"
              style={{ color: p.unrealized >= 0 ? "var(--buy)" : "var(--sell)" }}>
              {p.lastPrice > 0 ? `${p.unrealized >= 0 ? "+" : ""}${money(p.unrealized)}` : "—"}
            </dd>
          </div>
          <div>
            <dt>실현 손익</dt>
            <dd
              className="num"
              style={{ color: p.realized >= 0 ? "var(--buy)" : "var(--sell)" }}>
              {p.realized >= 0 ? "+" : ""}
              {money(p.realized)}
            </dd>
          </div>
        </dl>

        <p className="fine">
          거래 내역과 실현 손익은 <b>파일에 남아</b> 컴퓨터를 꺼도 그대로다. 평가 손익은
          지금 보이는 시세 기준이라, 꺼져 있던 동안의 시세 변동은 들어 있지 않다. 미체결
          주문은 서버가 꺼지면 사라진다 — 그동안 그 주문은 어느 시장에도 없었다.
        </p>
      </Panel>

      <Panel title="거래 내역" sub={`${rows.length}건`}>
        {rows.length === 0 ? (
          <p className="muted">아직 체결된 거래가 없다.</p>
        ) : (
          <table className="fills">
            <thead>
              <tr>
                <th>시각</th>
                <th>종목</th>
                <th>구분</th>
                <th>시장</th>
                <th className="r">가격</th>
                <th className="r">수량</th>
                <th className="r">금액</th>
              </tr>
            </thead>
            <tbody>
              {rows.map((r) => (
                <tr key={r.id}>
                  <td className="num muted">{r.at.slice(5, 19).replace("T", " ")}</td>
                  <td>{r.symbol}</td>
                  <td style={{ color: r.side === 0 ? "var(--buy)" : "var(--sell)" }}>
                    {sideText(r.side as 0 | 1)}
                  </td>
                  <td className="muted">{marketName(r.market)}</td>
                  <td className="num r">{money(r.price)}</td>
                  <td className="num r">{fq(r.qty)}</td>
                  <td className="num r">{money(r.price * r.qty)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </Panel>
    </div>
  );
}
