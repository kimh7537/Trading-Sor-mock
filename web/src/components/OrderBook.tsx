import type { Book, Level } from "../lib/types";
import { won, qty as fq } from "../lib/format";

const MARKET_COLOR: Record<string, string> = {
  KRX: "var(--krx)",
  NXT: "var(--nxt)",
};

function Row({
  level,
  side,
  max,
  best,
  onPick,
}: {
  level: Level;
  side: "bid" | "ask";
  max: number;
  best: boolean;
  onPick: (p: number) => void;
}) {
  const isBid = side === "bid";
  const color = isBid ? "var(--buy)" : "var(--sell)";
  const bg = isBid ? "var(--buy-bg)" : "var(--sell-bg)";
  const ratio = max > 0 ? (level.qty / max) * 100 : 0;

  return (
    <button
      onClick={() => onPick(level.price)}
      title="클릭하면 주문 가격에 담긴다"
      style={{
        position: "relative",
        display: "grid",
        gridTemplateColumns: "1fr 1fr",
        alignItems: "center",
        width: "100%",
        border: "none",
        borderRadius: 0,
        background: "transparent",
        padding: "5px var(--s-3)",
        textAlign: isBid ? "right" : "left",
        direction: isBid ? "rtl" : "ltr",
      }}
    >
      {/* 잔량 막대. 숫자보다 먼저 눈에 들어온다. */}
      <span
        style={{
          position: "absolute",
          inset: 0,
          [isBid ? "right" : "left"]: 0,
          width: `${ratio}%`,
          background: bg,
          pointerEvents: "none",
        }}
      />
      <span
        className="num"
        style={{
          position: "relative",
          color,
          fontWeight: best ? 700 : 500,
          direction: "ltr",
          textAlign: isBid ? "right" : "left",
        }}
      >
        {won(level.price)}
      </span>
      <span
        className="num"
        style={{
          position: "relative",
          color: "var(--text-dim)",
          fontSize: 12,
          direction: "ltr",
          textAlign: isBid ? "right" : "left",
        }}
      >
        {fq(level.qty)}
      </span>
    </button>
  );
}

export function OrderBook({
  book,
  onPick,
  bestOverall,
}: {
  book: Book;
  onPick: (p: number) => void;
  bestOverall?: { bid?: number; ask?: number };
}) {
  const max = Math.max(
    ...book.bids.map((l) => l.qty),
    ...book.asks.map((l) => l.qty),
    1,
  );
  const spread = (book.asks[0]?.price ?? 0) - (book.bids[0]?.price ?? 0);

  return (
    <div style={{ display: "flex", flexDirection: "column", minHeight: 0 }}>
      <div
        style={{
          display: "flex",
          alignItems: "center",
          justifyContent: "space-between",
          padding: "6px var(--s-3)",
          borderBottom: "1px solid var(--line-soft)",
        }}
      >
        <span
          style={{
            display: "inline-flex",
            alignItems: "center",
            gap: 6,
            fontSize: 12,
            fontWeight: 700,
            color: MARKET_COLOR[book.market],
          }}
        >
          <span
            style={{
              width: 6,
              height: 6,
              borderRadius: 2,
              background: MARKET_COLOR[book.market],
            }}
          />
          {book.market}
        </span>
        <span className="num" style={{ fontSize: 11, color: "var(--text-faint)" }}>
          스프레드 {won(spread)}
        </span>
      </div>

      {/* 매도는 위, 매수는 아래. 가격이 위로 갈수록 높다. */}
      <div style={{ display: "flex", flexDirection: "column-reverse" }}>
        {book.asks.map((l, i) => (
          <Row
            key={`a${l.price}`}
            level={l}
            side="ask"
            max={max}
            best={i === 0 && bestOverall?.ask === l.price}
            onPick={onPick}
          />
        ))}
      </div>

      <div
        style={{
          height: 1,
          background: "var(--line)",
          margin: "3px 0",
        }}
      />

      <div>
        {book.bids.map((l, i) => (
          <Row
            key={`b${l.price}`}
            level={l}
            side="bid"
            max={max}
            best={i === 0 && bestOverall?.bid === l.price}
            onPick={onPick}
          />
        ))}
      </div>
    </div>
  );
}
