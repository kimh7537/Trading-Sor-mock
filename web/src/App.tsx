import { useCallback, useEffect, useRef, useState } from "react";
import { MARKET_AUTO, ORDER_LIMIT, SIDE_BUY, marketName, sideText, type Side } from "./lib/wire";
import { won, qty as fq } from "./lib/format";
import { useTrading, type NewOrder } from "./lib/useTrading";
import { useToasts } from "./lib/useToasts";
import { useTheme } from "./lib/useTheme";
import { Panel } from "./components/Panel";
import { Header } from "./components/Header";
import { OrderBook } from "./components/OrderBook";
import { OrderTicket } from "./components/OrderTicket";
import { MarketCompare } from "./components/MarketCompare";
import { Activity } from "./components/Activity";
import { Toasts } from "./components/Toasts";
import { Strategies } from "./components/Strategies";
import { Ops } from "./components/Ops";
import { FeedMode } from "./components/FeedMode";
import { ChartPanel } from "./components/ChartPanel";

type View = "trade" | "strategies" | "ops";

/** 좁은 화면에서 한 번에 하나씩 보는 칸. 넓은 화면에서는 전부 나란히 놓인다 */
type Pane = "book" | "chart" | "ticket" | "activity" | "mode";

const PANES: { id: Pane; label: string }[] = [
  { id: "book", label: "호가" },
  { id: "chart", label: "차트" },
  { id: "ticket", label: "주문" },
  { id: "activity", label: "체결·잔고" },
  { id: "mode", label: "시세 모드" },
];

const VIEWS: { id: View; label: string }[] = [
  { id: "trade", label: "거래" },
  { id: "strategies", label: "전략 비교" },
  { id: "ops", label: "관제" },
];

export default function App() {
  const t = useTrading();
  const { toasts, notify, dismiss } = useToasts();
  const [view, setView] = useState<View>("trade");
  const [pane, setPane] = useState<Pane>("book");
  const [theme, toggleTheme] = useTheme();
  /* 사용자가 정한 것만 담는다. price 0은 "아직 안 정했다" — 아래에서 호가로 채운다 */
  const [picked, setPicked] = useState<NewOrder>({
    side: SIDE_BUY,
    market: MARKET_AUTO,
    type: ORDER_LIMIT,
    price: 0,
    qty: 10,
  });
  const [modeBusy, setModeBusy] = useState(false);
  const patch = useCallback((p: Partial<NewOrder>) => setPicked((d) => ({ ...d, ...p })), []);
  const pick = useCallback((price: number, side: Side) => patch({ price, side }), [patch]);

  // 새로 들어온 체결마다 알림. 체결 목록은 최신이 앞이고 이 화면을 연 뒤의 것만 있다
  const seen = useRef("");
  useEffect(() => {
    const fresh = [];
    for (const f of t.fills) {
      if (f.id === seen.current) break;
      fresh.push(f);
    }
    seen.current = t.fills[0]?.id ?? "";
    for (const f of fresh.reverse()) {
      notify("ok", `체결 · ${f.market} ${sideText(f.side)}`, `${fq(f.qty)}주 · ${won(f.price)}원`);
    }
  }, [t.fills, notify]);

  /*
   * **주문창 가격을 호가에서 끌어온다.** 기본값을 숫자로 박아 두면 그것은 시뮬 기준가일
   * 뿐이다 — 실시세를 켜면 종목의 실제 가격이 26만원대일 수도 있어 70,000원짜리 주문은
   * 아무것도 체결되지 않는다.
   *
   * 상태에는 **사용자가 정한 값만** 둔다(0 = 아직 안 정했다). 보여 줄 때 최우선 매도가로
   * 채운다 — 화면이 값을 기억했다가 되돌리는 것보다 파생이 단순하다. 모드를 바꾸면 다시
   * 0으로 돌려 그 시장의 호가를 따르게 한다.
   */
  const asks = [t.books.KRX?.asks[0]?.price, t.books.NXT?.asks[0]?.price].filter(
    (p): p is number => !!p,
  );
  const draft: NewOrder =
    picked.price > 0 ? picked : { ...picked, price: asks.length > 0 ? Math.min(...asks) : 0 };

  const live = t.feed?.mode === "live";
  /* 실시세는 통합 시세라 시장이 하나다. 그 시장만 보인다(T8-05) */
  const only = live && t.feed ? (marketName(t.feed.market) as "KRX" | "NXT") : undefined;

  const changeMode = useCallback(
    (mode: "sim" | "live") => {
      setModeBusy(true);
      /* 가격대가 통째로 달라진다. 그 시장의 호가를 따르게 되돌린다 */
      patch({ price: 0 });
      void t.setMode(mode).then((r) => {
        setModeBusy(false);
        notify(r.ok ? "ok" : "warn", "시세 모드", r.message);
      });
    },
    [t, notify, patch],
  );

  return (
    <div className="app">
      <Header
        ws={t.ws}
        ledgerDown={t.ledgerDown}
        balance={t.balance}
        books={t.books}
        feed={t.feed}
        only={only}
        symbol={t.symbol}
        onPickSymbol={t.pickSymbol}
        theme={theme}
        onToggleTheme={toggleTheme}
      />

      <nav className="tabs" role="tablist" aria-label="화면">
        {VIEWS.map((v) => (
          <button key={v.id} role="tab" aria-selected={view === v.id} onClick={() => setView(v.id)}>
            {v.label}
          </button>
        ))}
      </nav>

      <main className="page">
        {view === "trade" && (
          <nav className="pane-tabs" role="tablist" aria-label="거래 화면 칸">
            {PANES.map((p) => (
              <button
                key={p.id}
                role="tab"
                aria-selected={pane === p.id}
                onClick={() => setPane(p.id)}
              >
                {p.label}
              </button>
            ))}
          </nav>
        )}

        {view === "trade" && (
          <div className="workspace" data-pane={pane}>
            <Panel className="area-book" title="호가" sub="누르면 가격·방향을 주문창에 담는다" flush>
              {t.bookError && (
                <div className="alert-bar danger" role="alert">
                  원장 호가를 읽지 못했다 — ledgerd와 채널계가 떠 있는지 확인
                </div>
              )}
              <OrderBook books={t.books} orders={t.orders} onPick={pick} only={only} />
            </Panel>

            <Panel
              className="area-chart"
              title="차트"
              sub={live ? "바깥 시세 · 주문은 모의" : "가상 참가자가 만드는 호가"}
            >
              <ChartPanel ticks={t.ticks} feed={t.feed} />
            </Panel>

            <Panel className="area-mode" title="시세 모드">
              <FeedMode feed={t.feed} onChange={changeMode} busy={modeBusy} />
            </Panel>

            <Activity
              className="area-activity"
              orders={t.orders}
              rejects={t.rejects}
              fills={t.fills}
              onCancel={t.cancel}
              notify={notify}
            />

            <Panel className="area-ticket" title="주문" sub={`${t.symbol.name} ${t.symbol.code}`}>
              <div className="stack">
                <OrderTicket
                  draft={draft}
                  onChange={patch}
                  books={t.books}
                  balance={t.balance}
                  submit={t.submit}
                  notify={notify}
                  liveFills={t.ws.state === "open"}
                />
                <hr className="rule" />
                <h3 className="sub-head">
                  시장 비교
                  <span className="muted">
                    {sideText(draft.side)} {fq(draft.qty)}주 · {won(draft.price)}원 기준
                  </span>
                </h3>
                <MarketCompare books={t.books} draft={draft} orders={t.orders} live={live} />
              </div>
            </Panel>
          </div>
        )}

        {view === "strategies" && (
          <div style={{ maxWidth: 880 }}>
            <Panel
              title="전략별 집행 품질"
              sub="Phase 2 측정 · 시드 20260916 한 장면 (bench/results/strategies-2026-09-16.md)"
            >
              <Strategies />
            </Panel>
          </div>
        )}

        {view === "ops" && (
          <div style={{ maxWidth: 880 }}>
            <Panel title="관제">
              <Ops wsState={t.ws.state} ledgerDown={t.ledgerDown} events={t.events} />
            </Panel>
          </div>
        )}
      </main>

      <Toasts toasts={toasts} onDismiss={dismiss} />
    </div>
  );
}
