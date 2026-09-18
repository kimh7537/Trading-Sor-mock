import type { Book, Level, Market, OrderView } from "../lib/types";
import { SIDE_BUY, SIDE_SELL, marketName, type Side } from "../lib/wire";
import { won, qty as fq } from "../lib/format";
import { useFlash, type Change } from "../lib/useFlash";

const MARKETS: Market[] = ["KRX", "NXT"];

/** 좁은 화면에서 보이는 호가 단수 */
const FAR_DEPTH = 5;

const key = (m: string, ask: boolean, price: number) => `${m}:${ask ? "a" : "b"}:${price}`;

function Row({
  market,
  ask,
  level,
  max,
  best,
  mine,
  far,
  flash,
  onPick,
}: {
  market: Market;
  ask: boolean;
  level: Level;
  max: number;
  best: boolean;
  mine: number;
  /** 최우선에서 FAR_DEPTH단 이상 먼 호가. 좁은 화면에서는 숨긴다 */
  far: boolean;
  flash: Change | undefined;
  onPick: (price: number, side: Side) => void;
}) {
  const label =
    `${market} ${ask ? "매도" : "매수"}호가 ${won(level.price)}원 ${fq(level.qty)}주` +
    (best ? ", 두 시장 최우선" : "") +
    (mine > 0 ? `, 내 주문 ${fq(mine)}주` : "") +
    `. 누르면 이 가격으로 ${ask ? "매수" : "매도"} 준비`;

  return (
    <button
      className={`lvl ${ask ? "ask" : "bid"}${best ? " best" : ""}${far ? " far" : ""}${flash ? ` flash-${flash}` : ""}`}
      onClick={() => onPick(level.price, ask ? SIDE_BUY : SIDE_SELL)}
      aria-label={label}
      title={`누르면 ${won(level.price)}원 ${ask ? "매수" : "매도"} 주문을 준비한다`}
    >
      <span className="bar" style={{ width: `${Math.min(100, (level.qty / max) * 100)}%` }} />
      <span className="px num">
        {won(level.price)}
        {best && <span className="best-tag">최우선</span>}
        {mine > 0 && <span className="mine num">내 {fq(mine)}</span>}
      </span>
      <span className="qt num">
        {fq(level.qty)}
        {flash && (
          <span className={`delta ${flash}`} aria-hidden="true">
            {flash === "up" ? "▲" : "▼"}
          </span>
        )}
      </span>
    </button>
  );
}

/**
 * 두 시장 호가를 나란히(T7-05). 매도는 위(높은 가격이 위), 매수는 아래.
 *
 * - 두 시장을 합친 최우선호가에 "최우선" 표시
 * - 내가 걸어 둔 주문이 있는 가격에 "내 N" 표시 — 원장이 알려 준 시장별 다리 기준
 * - 잔량이 바뀐 줄은 잠깐 깜빡이고 ▲▼로 방향을 보인다
 * - 누르면 그 가격과 반대 방향(매도호가 → 매수)을 주문창에 담는다
 */
export function OrderBook({
  books,
  orders,
  onPick,
  only,
}: {
  books: Partial<Record<Market, Book>>;
  orders: OrderView[];
  onPick: (price: number, side: Side) => void;
  /** 이 시장 하나만 보인다. 실시세 모드에서 통합 시세를 심는 시장이다(T8-05) */
  only?: Market;
}) {
  const shown = only ? [only] : MARKETS;
  const mine = new Map<string, number>();
  for (const o of orders) {
    if (o.done) continue;
    for (const l of o.legs) {
      const w = l.sent - l.filled - l.canceled;
      if (w <= 0) continue;
      // 내 매수 주문은 매수호가 쪽에 걸린다
      const k = key(marketName(l.market), o.side === SIDE_SELL, o.price);
      mine.set(k, (mine.get(k) ?? 0) + w);
    }
  }

  const all = shown.flatMap((m) => [
    ...(books[m]?.asks ?? []).map((l) => [key(m, true, l.price), l.qty] as [string, number]),
    ...(books[m]?.bids ?? []).map((l) => [key(m, false, l.price), l.qty] as [string, number]),
  ]);
  const flash = useFlash(all);

  const asks = shown.map((m) => books[m]?.asks[0]?.price).filter((p): p is number => !!p);
  const bids = shown.map((m) => books[m]?.bids[0]?.price).filter((p): p is number => !!p);
  const bestAsk = asks.length ? Math.min(...asks) : 0;
  const bestBid = bids.length ? Math.max(...bids) : 0;
  const max = Math.max(1, ...all.map(([, q]) => q));
  // 두 열의 가운데 줄이 같은 높이에 오도록 빈 줄로 채운다
  const askDepth = Math.max(0, ...shown.map((m) => books[m]?.asks.length ?? 0));
  const bidDepth = Math.max(0, ...shown.map((m) => books[m]?.bids.length ?? 0));

  return (
    <div className={"book-cols" + (only ? " single" : "")}>
      {shown.map((m) => {
        const b = books[m];
        if (!b) {
          return (
            <div key={m} className="book-col">
              <div className="book-head">
                <span className={`tag ${m.toLowerCase()}`}>{m}</span>
              </div>
              <div className="empty">호가를 읽는 중…</div>
            </div>
          );
        }
        const spread = b.asks[0] && b.bids[0] ? b.asks[0].price - b.bids[0].price : null;
        const sum = (ls: Level[]) => ls.reduce((s, l) => s + l.qty, 0);
        const row = (l: Level, ask: boolean, i: number) => (
          <Row
            key={`${l.price}-${l.qty}`}
            market={m}
            ask={ask}
            level={l}
            max={max}
            best={i === 0 && l.price === (ask ? bestAsk : bestBid)}
            mine={mine.get(key(m, ask, l.price)) ?? 0}
            far={i >= FAR_DEPTH}
            flash={flash(key(m, ask, l.price))}
            onPick={onPick}
          />
        );
        // 빈 줄은 실제 호가보다 먼 자리다
        const pad = (from: number, to: number, p: string) =>
          Array.from({ length: Math.max(0, to - from) }, (_, j) => (
            <div key={`${p}${j}`} className={from + j >= FAR_DEPTH ? "lvl far" : "lvl"} aria-hidden="true" />
          ));

        return (
          <div key={m} className="book-col">
            <div className="book-head">
              <span className={`tag ${m.toLowerCase()}`}>{only ? `${m} · 통합 시세` : m}</span>
              <span className="num">스프레드 {spread === null ? "—" : won(spread)}</span>
            </div>
            <div role="group" aria-label={`${m} 매도호가`}>
              {pad(b.asks.length, askDepth, "pa").reverse()}
              {[...b.asks].reverse().map((l) => row(l, true, b.asks.indexOf(l)))}
            </div>
            <div className="book-mid num">
              <span>매도 잔량 {fq(sum(b.asks))}</span>
              <span>매수 잔량 {fq(sum(b.bids))}</span>
            </div>
            <div role="group" aria-label={`${m} 매수호가`}>
              {b.bids.map((l, i) => row(l, false, i))}
              {pad(b.bids.length, bidDepth, "pb")}
            </div>
          </div>
        );
      })}
    </div>
  );
}
