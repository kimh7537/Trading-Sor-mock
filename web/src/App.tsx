import { useCallback, useEffect, useRef, useState } from "react";
import { MARKET_AUTO, ORDER_LIMIT, SIDE_BUY, marketName, sideText, type Side } from "./lib/wire";
import { won, qty as fq } from "./lib/format";
import { useTrading, type NewOrder } from "./lib/useTrading";
import { useToasts } from "./lib/useToasts";
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
import { PriceChart } from "./components/PriceChart";

type View = "trade" | "strategies" | "ops";

const VIEWS: { id: View; label: string }[] = [
  { id: "trade", label: "거래" },
  { id: "strategies", label: "전략 비교" },
  { id: "ops", label: "관제" },
];

export default function App() {
  const t = useTrading();
  const { toasts, notify, dismiss } = useToasts();
  const [view, setView] = useState<View>("trade");
  const [draft, setDraft] = useState<NewOrder>({
    side: SIDE_BUY,
    market: MARKET_AUTO,
    type: ORDER_LIMIT,
    price: 70000,
    qty: 10,
  });
  const [modeBusy, setModeBusy] = useState(false);
  const patch = useCallback((p: Partial<NewOrder>) => setDraft((d) => ({ ...d, ...p })), []);
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

  const live = t.feed?.mode === "live";
  /* 실시세는 통합 시세라 시장이 하나다. 그 시장만 보인다(T8-05) */
  const only = live && t.feed ? (marketName(t.feed.market) as "KRX" | "NXT") : undefined;

  const changeMode = useCallback(
    (mode: "sim" | "live") => {
      setModeBusy(true);
      void t.setMode(mode).then((r) => {
        setModeBusy(false);
        notify(r.ok ? "ok" : "warn", r.ok ? r.message : "모드를 바꾸지 못했다", r.message);
      });
    },
    [t, notify],
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
          <div className="workspace">
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
              title="시세 모드와 가격"
              sub={live ? "바깥 시세 · 주문은 모의" : "가상 참가자가 만드는 호가"}
            >
              <FeedMode feed={t.feed} onChange={changeMode} busy={modeBusy} />
              <PriceChart ticks={t.ticks} />
            </Panel>

            <Panel
              className="area-compare"
              title="시장 비교"
              sub={`${sideText(draft.side)} ${fq(draft.qty)}주 · ${won(draft.price)}원 기준 예상`}
            >
              <MarketCompare books={t.books} draft={draft} orders={t.orders} live={live} />
            </Panel>

            <Activity
              className="area-activity"
              orders={t.orders}
              rejects={t.rejects}
              fills={t.fills}
              onCancel={t.cancel}
              notify={notify}
            />

            <Panel className="area-ticket" title="주문" sub="삼성전자 005930">
              <OrderTicket
                draft={draft}
                onChange={patch}
                books={t.books}
                balance={t.balance}
                submit={t.submit}
                notify={notify}
                liveFills={t.ws.state === "open"}
              />
            </Panel>
          </div>
        )}

        {view === "strategies" && (
          <div style={{ maxWidth: 880 }}>
            <Panel title="전략별 집행 품질" sub="Phase 2 측정 결과">
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
