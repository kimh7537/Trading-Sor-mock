import type { Book, Market, OrderView } from "../lib/types";
import type { NewOrder } from "../lib/useTrading";
import { estimate, type Estimate } from "../lib/estimate";
import { money, qty as fq } from "../lib/format";
import { MARKET_AUTO, ORDER_FOK, SIDE_BUY, marketName, sideText, statusText } from "../lib/wire";

const OPTIONS: { id: string; label: string; markets: Market[] }[] = [
  { id: "KRX", label: "KRX 단독", markets: ["KRX"] },
  { id: "NXT", label: "NXT 단독", markets: ["NXT"] },
  { id: "SOR", label: "두 시장 (SOR)", markets: ["KRX", "NXT"] },
];

const COLOR: Record<Market, string> = { KRX: "var(--krx)", NXT: "var(--nxt)" };

/**
 * 지금 주문창의 조건(방향·가격·수량·유형)을 시장별로 넣어 보면 어떻게 되는지(T7-05).
 *
 * 전에는 "SOR 판단"이라며 각 시장 최우선호가만 비교했다 — 수량을 모르니 판단이 아니었다.
 * 아래에는 원장이 **실제로** 나눈 결과(최근 SOR 주문의 시장별 다리)를 둔다. 예상과 실제를 나란히 본다.
 */
export function MarketCompare({
  books,
  draft,
  orders,
  live = false,
}: {
  books: Partial<Record<Market, Book>>;
  draft: NewOrder;
  orders: OrderView[];
  /** 실시세 모드인가. 통합 시세에는 나눌 시장이 없어 SOR이 할 일이 없다(T8-05) */
  live?: boolean;
}) {
  if (live) {
    return (
      <div className="stack">
        <div className="alert-bar warn" role="status">
          <b>실시세 모드에서는 SOR이 할 일이 없다</b>
        </div>
        <p className="note">
          바깥에서 받는 국내 호가는 <b>통합 시세(KRX+NXT)</b>다. 토스증권 Open API의 호가 조회는
          파라미터가 종목 하나뿐이고 시장을 고르는 인자가 없다 — 나눌 시장이 없으니
          배분할 것도 없다.
        </p>
        <p className="note">
          두 시장·전략 4종·집행 품질 측정은 <b>시뮬 모드</b>에서만 볼 수 있고, 그것이 이
          프로젝트의 논지다. 두 모드가 같은 원장·같은 전문·같은 매칭 엔진을 쓴다.
        </p>
      </div>
    );
  }

  const buy = draft.side === SIDE_BUY;
  const rows = OPTIONS.map((o) => ({
    ...o,
    e: estimate(books, o.markets, buy, draft.price, draft.qty, draft.type === ORDER_FOK),
  }));
  // 더 많이 체결되는 쪽이 낫고, 같으면 매수는 싼 쪽·매도는 비싼 쪽
  const beats = (a: Estimate, b: Estimate) =>
    a.fill !== b.fill ? a.fill > b.fill : buy ? a.avg < b.avg : a.avg > b.avg;
  const top = rows.reduce((x, y) => (beats(y.e, x.e) ? y : x)).e;

  const lastSor = orders.find((o) => o.market === MARKET_AUTO);

  return (
    <div className="stack">
      <div className="venues">
        {rows.map((r) => {
          const isBest = top.fill > 0 && r.e.fill === top.fill && r.e.avg === top.avg;
          const qty = Math.max(1, draft.qty);
          return (
            <div key={r.id} className={`venue${isBest ? " best" : ""}`}>
              <div className="venue-row">
                <b style={{ fontSize: 12 }}>{r.label}</b>
                {isBest && <span className="tag ok">유리</span>}
              </div>
              <div className="big num">{r.e.fill > 0 ? money(r.e.avg) : "—"}</div>
              <span className="muted">예상 평균가</span>
              <div className="meter" aria-hidden="true">
                {r.markets.map((m) => (
                  <span key={m} style={{ width: `${(r.e.byMarket[m] / qty) * 100}%`, background: COLOR[m] }} />
                ))}
              </div>
              <div className="venue-row">
                <span className="muted">체결</span>
                <span className="num" style={{ fontSize: 11 }}>
                  {fq(r.e.fill)}/{fq(draft.qty)}주 · {Math.round((r.e.fill / qty) * 100)}%
                </span>
              </div>
            </div>
          );
        })}
      </div>

      {lastSor ? (
        <div className="venue">
          <div className="venue-row">
            <b style={{ fontSize: 12 }}>최근 SOR 주문 — 원장이 실제로 나눈 결과</b>
            <span className="muted num">#{lastSor.orderId}</span>
          </div>
          <div className="venue-row">
            <span>
              <span className={`tag ${lastSor.side === SIDE_BUY ? "buy" : "sell"}`}>{sideText(lastSor.side)}</span>{" "}
              <span className="num">
                {money(lastSor.price)} × {fq(lastSor.qty)}
              </span>
            </span>
            <span className="muted">{statusText(lastSor.status)}</span>
          </div>
          <div className="meter" aria-hidden="true" style={{ height: 6 }}>
            {lastSor.legs.map((l) => (
              <span
                key={l.market}
                style={{
                  width: `${(l.sent / Math.max(1, lastSor.qty)) * 100}%`,
                  background: COLOR[marketName(l.market) as Market],
                }}
              />
            ))}
          </div>
          <div className="split-legend num">
            {lastSor.legs.length === 0 && <span>시장으로 나간 몫이 없다</span>}
            {lastSor.legs.map((l) => (
              <span key={l.market}>
                <span className="swatch" style={{ background: COLOR[marketName(l.market) as Market] }} />
                {marketName(l.market)} 보냄 {fq(l.sent)} · 체결 {fq(l.filled)}
                {l.filled > 0 && ` @ ${money(l.avgPrice)}`}
              </span>
            ))}
          </div>
        </div>
      ) : (
        <p className="note">시장을 "SOR 자동"으로 주문하면 원장이 실제로 나눈 결과가 여기 보인다.</p>
      )}

      <p className="note">
        예상은 지금 보이는 호가(시장마다 10단)로 계산한 참고값이다. 실제 배분은 원장이 정한다.
      </p>
    </div>
  );
}
