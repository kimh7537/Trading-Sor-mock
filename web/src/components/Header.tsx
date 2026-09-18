import type { ConnState } from "../lib/useStream";
import type { Balance, Book, FeedStatus, Market } from "../lib/types";
import { ACCOUNT } from "../lib/api";
import { won } from "../lib/format";
import { useFlash } from "../lib/useFlash";

const CONN_LABEL: Record<ConnState, string> = {
  connecting: "연결 중",
  open: "실시간",
  closed: "재연결 중",
};

const MARKETS: Market[] = ["KRX", "NXT"];

/**
 * 두 시장을 합친 최우선호가와 그 시장. 같은 가격이면 둘 다 적는다.
 *
 * 실시세 모드에서는 시장이 하나다 — 바깥 시세가 통합(KRX+NXT)이라 나눌 시장이 없다.
 * 그때 `only`가 그 시장이고, 다른 쪽 호가창은 화면에 보이지도 않는다(T8-05).
 */
function best(books: Partial<Record<Market, Book>>, ask: boolean, only?: Market) {
  let price = 0;
  let where: Market[] = [];
  for (const m of only ? [only] : MARKETS) {
    const p = (ask ? books[m]?.asks[0] : books[m]?.bids[0])?.price;
    if (!p) continue;
    if (price === 0 || (ask ? p < price : p > price)) {
      price = p;
      where = [m];
    } else if (p === price) {
      where.push(m);
    }
  }
  return { price, where: where.join("·") };
}

export function Header({
  ws,
  ledgerDown,
  balance,
  books,
  feed,
  only,
}: {
  ws: { state: ConnState; attempt: number };
  ledgerDown: string | null;
  balance: Balance | null;
  books: Partial<Record<Market, Book>>;
  /** 지금 시뮬인가 실시세인가(T8-05). 어느 탭에서도 보여야 한다 */
  feed: FeedStatus | null;
  /** 실시세 모드에서 통합 시세를 심는 시장. 그때는 이 시장만 센다 */
  only?: Market;
}) {
  const live = feed?.mode === "live";
  const cells: [key: keyof Balance, label: string][] = [
    ["cash", "예수금"],
    ["reserved", "묶인 금액"],
    ["available", "주문 가능"],
  ];
  const flash = useFlash(balance ? cells.map(([k]) => [k, balance[k] as number]) : []);
  const ask = best(books, true, only);
  const bid = best(books, false, only);

  return (
    <>
      <header className="topbar">
        <div className="brand">
          <strong>mock-sor</strong>
          <span>KRX·NXT 복수시장 주문 집행</span>
        </div>
        <div className="symbol">
          <b>삼성전자</b>
          <span className="num">005930 · 계좌 {ACCOUNT}</span>
        </div>
        <div className="quote">
          <div>
            <span className="k">{live ? "최우선 매도" : "통합 최우선 매도"}</span>
            <span className="v num" style={{ color: "var(--sell)" }}>
              {ask.price ? won(ask.price) : "—"} <span className="muted">{ask.where}</span>
            </span>
          </div>
          <div>
            <span className="k">{live ? "최우선 매수" : "통합 최우선 매수"}</span>
            <span className="v num" style={{ color: "var(--buy)" }}>
              {bid.price ? won(bid.price) : "—"} <span className="muted">{bid.where}</span>
            </span>
          </div>
        </div>

        <div className="spacer" />

        <div className="balance" aria-label="잔고">
          {cells.map(([k, label]) => {
            const v = balance ? (balance[k] as number) : null;
            const f = flash(k);
            return (
              <div key={k}>
                <span className="k">{label}</span>
                <span
                  key={v ?? "none"}
                  className={`v num${f ? ` flash-${f}` : ""}`}
                  style={k === "available" ? { color: "var(--ok)" } : undefined}
                >
                  {v === null ? "—" : `${won(v)}원`}
                </span>
              </div>
            );
          })}
        </div>

        <span
          className={`mode-badge${live ? " live" : ""}`}
          title={live ? "바깥 시세를 원장 호가창에 심고 있다. 주문은 모의다" : "가상 참가자가 호가를 만든다"}
        >
          {live ? "실시세 · 주문은 모의" : "시뮬"}
        </span>

        <span className={`conn ${ws.state}`} title="채널계 실시간 방송 연결">
          <span className="dot" aria-hidden="true" />
          {CONN_LABEL[ws.state]}
          {ws.state === "closed" && ws.attempt > 0 && <span className="muted">· {ws.attempt}회</span>}
        </span>
      </header>

      {ledgerDown && (
        <div className="alert-bar danger" role="alert">
          <b>원장에 연결되지 않음</b> — {ledgerDown}. 주문·취소는 원장이 돌아오면 다시 하세요.
        </div>
      )}
      {!ledgerDown && ws.state === "closed" && (
        <div className="alert-bar warn" role="status">
          <b>실시간 연결 끊김</b> — 다시 붙는 동안 3초마다 원장 상태를 직접 읽는다.
        </div>
      )}
    </>
  );
}
