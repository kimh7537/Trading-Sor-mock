import { useEffect, useRef, useState } from "react";
import { searchStocks, type CurrentSymbol, type StockHit } from "../lib/api";

/**
 * 종목 고르기 (T8-10).
 *
 * **바꾸면 원장이 새로 열린다** — 미체결 주문과 잔고가 초기화된다. 원장 호가창은 기준가
 * ±30%만 펼쳐 두므로, 가격대가 다른 종목을 같은 호가창에 담을 수 없어서다. 그 사실을
 * 목록 아래에 적어 둔다.
 *
 * 검색은 채널계가 토스에서 받아 둔 국내 보통주 목록에서 고른다. 이름으로 찾는
 * 엔드포인트가 토스에 없어 목록을 받아 거르는 방식이다.
 */
export function SymbolPicker({
  current,
  onPick,
  disabled,
}: {
  current: CurrentSymbol;
  onPick: (code: string) => Promise<{ ok: boolean; message: string }>;
  disabled?: boolean;
}) {
  const [open, setOpen] = useState(false);
  const [q, setQ] = useState("");
  const [hits, setHits] = useState<StockHit[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const box = useRef<HTMLDivElement>(null);

  /* 타자를 칠 때마다 부르지 않는다. 멈춘 뒤에 한 번 */
  useEffect(() => {
    if (!open) return;
    const id = window.setTimeout(() => {
      searchStocks(q)
        .then((r) => {
          setHits(r.stocks);
          setError(r.error);
        })
        .catch(() => setError("종목을 찾지 못했다"));
    }, 250);
    return () => window.clearTimeout(id);
  }, [q, open]);

  /* 바깥을 누르면 닫는다 */
  useEffect(() => {
    if (!open) return;
    const away = (e: MouseEvent) => {
      if (box.current && !box.current.contains(e.target as Node)) setOpen(false);
    };
    document.addEventListener("mousedown", away);
    return () => document.removeEventListener("mousedown", away);
  }, [open]);

  const choose = (code: string) => {
    setBusy(true);
    void onPick(code)
      .then((r) => {
        setBusy(false);
        if (r.ok) {
          setOpen(false);
          setQ("");
        } else {
          setError(r.message);
        }
      })
      .catch(() => {
        setBusy(false);
        setError("종목을 바꾸지 못했다");
      });
  };

  return (
    <div className="picker" ref={box}>
      <button
        type="button"
        className="picker-btn"
        aria-expanded={open}
        disabled={disabled}
        onClick={() => setOpen((v) => !v)}
      >
        <b>{current.name}</b>
        <span className="num">{current.code}</span>
        <span aria-hidden="true">▾</span>
      </button>

      {open ? (
        <div className="picker-pop" role="dialog" aria-label="종목 고르기">
          <input
            autoFocus
            type="search"
            value={q}
            placeholder="이름이나 코드 (예: 삼성, 000660)"
            onChange={(e) => setQ(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Escape") setOpen(false);
              if (e.key === "Enter" && hits.length > 0) choose(hits[0].symbol);
            }}
          />

          {error ? <p className="picker-err">{error}</p> : null}

          <ul className="picker-list">
            {hits.map((s) => (
              <li key={s.symbol}>
                <button
                  type="button"
                  disabled={busy}
                  aria-current={s.symbol === current.code}
                  onClick={() => choose(s.symbol)}
                >
                  <span>{s.name}</span>
                  <span className="num muted">
                    {s.symbol} · {s.market}
                  </span>
                </button>
              </li>
            ))}
            {hits.length === 0 && !error ? <li className="muted">찾는 종목이 없다</li> : null}
          </ul>

          <p className="picker-note">
            종목을 바꾸면 <b>그 종목의 원장을 새로 연다</b> — 미체결 주문과 잔고가 초기화된다.
            호가창이 기준가 ±30%만 펼쳐 두기 때문이다.
          </p>
        </div>
      ) : null}
    </div>
  );
}
