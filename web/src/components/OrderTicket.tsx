import { useEffect, useMemo, useRef, useState } from "react";
import type { OrderResponse } from "../lib/api";
import type { NewOrder } from "../lib/useTrading";
import type { Notify } from "../lib/useToasts";
import type { Balance, Book, Market } from "../lib/types";
import { estimate, marketsOf } from "../lib/estimate";
import { won, qty as fq } from "../lib/format";
import {
  MARKET_AUTO,
  MARKET_KRX,
  MARKET_NXT,
  ORDER_FOK,
  ORDER_IOC,
  ORDER_LIMIT,
  SIDE_BUY,
  SIDE_SELL,
  isValidTick,
  reasonText,
  sideText,
  stepPrice,
  tickSize,
} from "../lib/wire";

const MARKET_OPTIONS = [
  { v: MARKET_AUTO, t: "SOR 자동", help: "원장이 두 시장 호가를 보고 나눠 보낸다" },
  { v: MARKET_KRX, t: "KRX", help: "KRX에만 보낸다" },
  { v: MARKET_NXT, t: "NXT", help: "NXT에만 보낸다" },
];

const TYPE_OPTIONS = [
  { v: ORDER_LIMIT, t: "지정가", help: "바로 체결되지 않은 수량은 호가창에 남는다" },
  { v: ORDER_IOC, t: "IOC", help: "지금 체결되는 만큼만, 나머지는 취소" },
  { v: ORDER_FOK, t: "FOK", help: "전량이 지금 체결될 때만, 아니면 전부 취소" },
];

/**
 * 주문창(T7-05). 보내기 전에 **예상 체결·필요 금액·주문 가능 여부**를 보인다.
 * 판정은 원장이 다시 한다 — 여기서 막는 것은 원장이 틀림없이 거절할 입력(호가 단위, 증거금)뿐이다.
 *
 * 키보드: B 매수 · S 매도(입력칸 밖에서) · 가격칸 ↑↓ 한 호가 · Ctrl+Enter 주문
 */
export function OrderTicket({
  draft,
  onChange,
  books,
  balance,
  submit,
  notify,
  liveFills,
}: {
  draft: NewOrder;
  onChange: (p: Partial<NewOrder>) => void;
  books: Partial<Record<Market, Book>>;
  balance: Balance | null;
  submit: (o: NewOrder) => Promise<OrderResponse>;
  notify: Notify;
  /** 체결 방송을 받는 중이면 체결 알림은 방송 쪽이 띄운다(두 번 뜨지 않게) */
  liveFills: boolean;
}) {
  const { side, market, type, price, qty } = draft;
  const buy = side === SIDE_BUY;
  const [busy, setBusy] = useState(false);

  const est = useMemo(
    () => estimate(books, marketsOf(market), buy, price, qty, type === ORDER_FOK),
    [books, market, buy, price, qty, type],
  );

  const amount = price * qty;
  const available = balance?.available ?? null;
  const short = buy && available !== null && amount > available ? amount - available : 0;
  const markets = marketsOf(market);
  const opposite = markets
    .map((m) => (buy ? books[m]?.asks[0]?.price : books[m]?.bids[0]?.price))
    .filter((p): p is number => !!p);
  const bestOpposite = opposite.length ? (buy ? Math.min(...opposite) : Math.max(...opposite)) : 0;
  const maxQty = buy && available !== null && price > 0 ? Math.floor(available / price) : 0;

  const problem =
    !(price > 0)
      ? "가격을 입력하세요"
      : !isValidTick(price)
        ? `호가 단위(${won(tickSize(price))}원)에 맞지 않는 가격 — ↑↓로 맞추세요`
        : !(qty > 0) || !Number.isInteger(qty)
          ? "수량은 1주 이상"
          : short > 0
            ? `주문 가능 금액이 ${won(short)}원 모자람`
            : null;

  const send = async () => {
    if (busy || problem) return;
    setBusy(true);
    const what = `${sideText(side)} ${fq(qty)}주 · ${won(price)}원`;
    try {
      const res = await submit(draft);
      if (res.outcome === "REJECTED") {
        const why = res.reason < 0 && res.reason !== -16 ? reasonText(res.reason) : res.message;
        notify("error", "주문 거절", `${what} — ${why}`);
      } else if (res.outcome === "IN_DOUBT") {
        notify("warn", "확인 필요", `${what} — 원장 응답을 못 받았다. 다시 보내기 전에 주문 내역을 확인하세요.`);
      } else if (res.filledQty === 0) {
        notify("info", "주문 접수", `${what} — ${type === ORDER_LIMIT ? "호가창에 대기" : "체결 없이 끝남"}`);
      } else if (!liveFills) {
        notify("ok", "체결", `${what} — ${fq(res.filledQty)}주 · 평균 ${won(res.avgPrice)}원`);
      }
    } finally {
      setBusy(false);
    }
  };

  // 전역 단축키. 최신 send를 부르도록 ref로 잇는다
  const sendRef = useRef(send);
  useEffect(() => {
    sendRef.current = send;
  });
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
        e.preventDefault();
        void sendRef.current();
        return;
      }
      if (e.ctrlKey || e.metaKey || e.altKey || e.repeat) return;
      const el = e.target as HTMLElement;
      if (el.tagName === "INPUT" || el.tagName === "TEXTAREA" || el.isContentEditable) return;
      // e.code는 한글 입력 상태(ㅠ·ㄴ)에서도 같은 키다
      if (e.code === "KeyB") onChange({ side: SIDE_BUY });
      if (e.code === "KeyS") onChange({ side: SIDE_SELL });
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onChange]);

  const typeHelp = TYPE_OPTIONS.find((o) => o.v === type)?.help;

  return (
    <form
      className="ticket"
      // 입력칸에서 Enter만 쳐도 주문이 나가면 사고다. 주문은 버튼이나 Ctrl+Enter로만
      onSubmit={(e) => e.preventDefault()}
      aria-label="주문 입력"
    >
      <div className="seg side" role="group" aria-label="매수·매도">
        <button type="button" className="buy" aria-pressed={buy} onClick={() => onChange({ side: SIDE_BUY })}>
          매수 <kbd>B</kbd>
        </button>
        <button type="button" className="sell" aria-pressed={!buy} onClick={() => onChange({ side: SIDE_SELL })}>
          매도 <kbd>S</kbd>
        </button>
      </div>

      <div className="field">
        <span className="field-label" id="mk-label">
          시장
        </span>
        <div className="seg" role="group" aria-labelledby="mk-label">
          {MARKET_OPTIONS.map((o) => (
            <button
              type="button"
              key={o.v}
              title={o.help}
              aria-pressed={market === o.v}
              onClick={() => onChange({ market: o.v })}
            >
              {o.t}
            </button>
          ))}
        </div>
      </div>

      <div className="field">
        <span className="field-label" id="ty-label">
          유형
        </span>
        <div className="seg" role="group" aria-labelledby="ty-label">
          {TYPE_OPTIONS.map((o) => (
            <button type="button" key={o.v} aria-pressed={type === o.v} onClick={() => onChange({ type: o.v })}>
              {o.t}
            </button>
          ))}
        </div>
        <span className="help">{typeHelp}</span>
      </div>

      <div className="field">
        <div className="field-label">
          <label htmlFor="price">가격</label>
          <button
            type="button"
            className="btn-sm"
            disabled={!bestOpposite}
            onClick={() => onChange({ price: bestOpposite })}
            title="반대쪽 최우선호가 — 바로 체결되는 가격"
          >
            {buy ? "최우선 매도" : "최우선 매수"} <span className="num">{bestOpposite ? won(bestOpposite) : "—"}</span>
          </button>
        </div>
        <div className="stepper">
          <button type="button" aria-label="한 호가 내리기" onClick={() => onChange({ price: stepPrice(price, -1) })}>
            −
          </button>
          <input
            id="price"
            className="num"
            type="number"
            inputMode="numeric"
            min={1}
            value={price || ""}
            onChange={(e) => onChange({ price: Math.max(0, Math.trunc(Number(e.target.value))) })}
            onKeyDown={(e) => {
              if (e.key === "ArrowUp" || e.key === "ArrowDown") {
                e.preventDefault();
                onChange({ price: stepPrice(price, e.key === "ArrowUp" ? 1 : -1) });
              }
            }}
          />
          <button type="button" aria-label="한 호가 올리기" onClick={() => onChange({ price: stepPrice(price, 1) })}>
            +
          </button>
        </div>
        <span className="help num">호가 단위 {won(tickSize(Math.max(1, price)))}원</span>
      </div>

      <div className="field">
        <div className="field-label">
          <label htmlFor="qty">수량</label>
          {buy && maxQty > 0 && <span className="muted num">최대 {fq(maxQty)}주</span>}
        </div>
        <div className="stepper">
          <button type="button" aria-label="1주 줄이기" onClick={() => onChange({ qty: Math.max(1, qty - 1) })}>
            −
          </button>
          <input
            id="qty"
            className="num"
            type="number"
            inputMode="numeric"
            min={1}
            value={qty || ""}
            onChange={(e) => onChange({ qty: Math.max(0, Math.trunc(Number(e.target.value))) })}
          />
          <button type="button" aria-label="1주 늘리기" onClick={() => onChange({ qty: qty + 1 })}>
            +
          </button>
        </div>
        <div className="chips">
          {[10, 100, 1000].map((n) => (
            <button type="button" key={n} onClick={() => onChange({ qty: n })}>
              {fq(n)}
            </button>
          ))}
          {buy && (
            <button type="button" disabled={maxQty <= 0} onClick={() => onChange({ qty: maxQty })}>
              최대
            </button>
          )}
        </div>
      </div>

      <dl className="summary" aria-label="주문 전 확인">
        <div>
          <dt>예상 체결</dt>
          <dd className="num">
            {est.fill > 0 ? `${fq(est.fill)}주 · 평균 ${won(est.avg)}원` : "없음"}
            {market === MARKET_AUTO && est.fill > 0 && (
              <span className="fine">
                KRX {fq(est.byMarket.KRX)} · NXT {fq(est.byMarket.NXT)}
              </span>
            )}
          </dd>
        </div>
        <div>
          <dt>나머지</dt>
          <dd className="num">
            {est.rest === 0
              ? "없음"
              : type === ORDER_LIMIT
                ? `${fq(est.rest)}주 호가창 대기`
                : type === ORDER_IOC
                  ? `${fq(est.rest)}주 취소`
                  : "전량 불가 — 주문 전체 취소"}
          </dd>
        </div>
        {buy ? (
          <>
            <div>
              <dt>필요 금액</dt>
              <dd className="num">
                {won(amount)}원<span className="fine">체결되면 실제 체결가로 정산</span>
              </dd>
            </div>
            <div className="total">
              <dt>주문 가능</dt>
              <dd className="num">
                {available === null ? (
                  "잔고 확인 중"
                ) : short > 0 ? (
                  <span className="bad">✕ {won(short)}원 부족</span>
                ) : (
                  <span className="good">✓ 가능 · 남는 금액 {won(available - amount)}원</span>
                )}
              </dd>
            </div>
          </>
        ) : (
          <div className="total">
            <dt>예상 수령</dt>
            <dd className="num">{won(est.notional)}원</dd>
          </div>
        )}
      </dl>

      {/* 주문 버튼은 주문창이 길어져도 늘 보이게 아래에 붙인다 */}
      <div className="ticket-submit">
        <button
          type="button"
          onClick={() => void send()}
          className={`submit ${buy ? "buy" : "sell"}`}
          disabled={busy || problem !== null}
          aria-describedby="ticket-problem"
        >
          {busy ? "보내는 중…" : `${sideText(side)} ${fq(qty)}주 주문`}
        </button>
        <span id="ticket-problem" className={problem ? "help warn" : "help"}>
          {problem}
        </span>
        <div className="shortcuts" aria-label="단축키">
          <span>
            <kbd>B</kbd> 매수 <kbd>S</kbd> 매도
          </span>
          <span>
            가격칸 <kbd>↑</kbd>
            <kbd>↓</kbd> 한 호가
          </span>
          <span>
            <kbd>Ctrl</kbd>+<kbd>Enter</kbd> 주문
          </span>
        </div>
      </div>
    </form>
  );
}
