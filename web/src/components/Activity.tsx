import { useState } from "react";
import type { Fill, LocalReject, OrderView } from "../lib/types";
import type { Notify } from "../lib/useToasts";
import { useFlash, type Change } from "../lib/useFlash";
import { won, qty as fq } from "../lib/format";
import {
  SIDE_BUY,
  SIDE_SELL,
  STATUS_FILLED,
  STATUS_PARTIAL,
  marketName,
  orderTypeText,
  sideText,
  statusText,
} from "../lib/wire";
import { Panel } from "./Panel";

type View = "open" | "all" | "fills";

const sideClass = (s: number) => (s === SIDE_BUY ? "buy" : "sell");

/**
 * 상태를 글자와 색으로 함께. 원장은 일부 체결 뒤 취소한 주문을 "부분 체결"로 두므로(T7-01)
 * 끝난 주문은 취소 여부로 다시 적는다.
 */
function StateTag({ o }: { o: OrderView }) {
  if (o.done && o.canceled > 0) {
    return <span className="tag muted">{o.filled > 0 ? "부분 체결 후 취소" : "취소"}</span>;
  }
  if (o.status === STATUS_FILLED) return <span className="tag ok">전량 체결</span>;
  if (o.status === STATUS_PARTIAL) return <span className="tag warn">부분 체결</span>;
  if (!o.done) return <span className="tag warn">대기</span>;
  return <span className="tag danger">{statusText(o.status)}</span>;
}

function OrderRow({
  o,
  open,
  busy,
  flash,
  onToggle,
  onCancel,
}: {
  o: OrderView;
  open: boolean;
  busy: boolean;
  flash: Change | undefined;
  onToggle: () => void;
  onCancel: () => void;
}) {
  const req = marketName(o.market);
  const qty = Math.max(1, o.qty);

  return (
    <li className="order">
      <div key={`${o.filled}-${o.canceled}`} className={`order-main${flash ? ` flash-${flash}` : ""}`}>
        <div className="order-line">
          <button
            className="toggle"
            aria-expanded={open}
            aria-label={`주문 ${o.orderId} 시장별 내역 ${open ? "접기" : "펼치기"}`}
            onClick={onToggle}
          >
            {open ? "▾" : "▸"}
          </button>
          <span className={`tag ${sideClass(o.side)}`}>{sideText(o.side)}</span>
          <span className={`tag ${req.toLowerCase()}`}>{req}</span>
          <span className="tag muted">{orderTypeText(o.type)}</span>
          <span className="px num">
            {won(o.price)} × {fq(o.qty)}
          </span>
          <span className="grow" />
          <StateTag o={o} />
        </div>
        <div
          className="meter"
          role="progressbar"
          aria-label="체결 진행"
          aria-valuemin={0}
          aria-valuemax={o.qty}
          aria-valuenow={o.filled}
        >
          <span style={{ width: `${(o.filled / qty) * 100}%`, background: `var(--${sideClass(o.side)})` }} />
          <span style={{ width: `${(o.canceled / qty) * 100}%`, background: "var(--line-strong)" }} />
        </div>
        <div className="order-line">
          <span className="stat num">
            체결 {fq(o.filled)}
            {o.filled > 0 && ` · 평균 ${won(o.avgPrice)}원`}
            {flash && (
              <span className={`delta ${flash}`} aria-hidden="true">
                ▲
              </span>
            )}
          </span>
          {o.canceled > 0 && <span className="stat num">취소 {fq(o.canceled)}</span>}
          {o.working > 0 && <span className="stat num">대기 {fq(o.working)}</span>}
          <span className="grow" />
          <span className="muted num">#{o.orderId}</span>
          {!o.done && (
            <button className="btn-sm btn-danger-ghost" onClick={onCancel} disabled={busy}>
              {busy ? "취소 중…" : "취소"}
            </button>
          )}
        </div>
      </div>

      {open && (
        <div className="legs">
          <span className="caption">논리 주문 1건 → 시장별 물리 주문</span>
          {o.legs.length === 0 && <span className="caption">시장으로 나간 몫이 없다</span>}
          {o.legs.map((l) => {
            const m = marketName(l.market);
            return (
              <div key={l.market} className="leg">
                <span className={`tag ${m.toLowerCase()}`}>{m}</span>
                <span>
                  보냄 <b className="num">{fq(l.sent)}</b>
                </span>
                <span>
                  체결 <b className="num">{fq(l.filled)}</b>
                  {l.filled > 0 && (
                    <>
                      {" "}
                      @ <b className="num">{won(l.avgPrice)}</b>
                    </>
                  )}
                </span>
                <span>
                  취소 <b className="num">{fq(l.canceled)}</b>
                </span>
                <span>
                  대기 <b className="num">{fq(l.sent - l.filled - l.canceled)}</b>
                </span>
              </div>
            );
          })}
        </div>
      )}
    </li>
  );
}

function RejectRow({ r }: { r: LocalReject }) {
  return (
    <li className="order">
      <div className="order-main">
        <div className="order-line">
          <span className={`tag ${sideClass(r.side)}`}>{sideText(r.side)}</span>
          <span className={`tag ${marketName(r.market).toLowerCase()}`}>{marketName(r.market)}</span>
          <span className="px num">
            {won(r.price)} × {fq(r.qty)}
          </span>
          <span className="grow" />
          <span className={`tag ${r.outcome === "IN_DOUBT" ? "warn" : "danger"}`}>
            {r.outcome === "IN_DOUBT" ? "확인 필요" : "거부"}
          </span>
        </div>
        <div className="order-line">
          <span className="stat">{r.reason}</span>
          <span className="grow" />
          <span className="muted num">{r.at}</span>
        </div>
      </div>
    </li>
  );
}

function FillTable({ fills }: { fills: Fill[] }) {
  const flash = useFlash(fills.map((f) => [f.id, 1] as [string, number]));
  // 매수와 매도를 섞은 평균은 뜻이 없다 — 방향별로 나눈다
  const summary = [SIDE_BUY, SIDE_SELL].map((side) => {
    const mine = fills.filter((f) => f.side === side);
    const q = mine.reduce((s, f) => s + f.qty, 0);
    const amt = mine.reduce((s, f) => s + f.price * f.qty, 0);
    return { side, q, avg: q > 0 ? Math.floor(amt / q) : 0 };
  });

  if (fills.length === 0) {
    return <div className="empty">체결 내역이 없다. 이 화면을 연 뒤에 들어온 체결이 여기 쌓인다.</div>;
  }
  return (
    <>
      <div className="fill-summary num">
        {summary.map((s) => (
          <span key={s.side}>
            <span className={`tag ${sideClass(s.side)}`}>{sideText(s.side)}</span> 평균{" "}
            <b>{s.q > 0 ? `${won(s.avg)}원 · ${fq(s.q)}주` : "—"}</b>
          </span>
        ))}
      </div>
      <table className="fills">
        <thead>
          <tr>
            <th scope="col">시각</th>
            <th scope="col">시장</th>
            <th scope="col">구분</th>
            <th scope="col" className="r">
              가격
            </th>
            <th scope="col" className="r">
              수량
            </th>
          </tr>
        </thead>
        <tbody>
          {fills.map((f) => {
            const fl = flash(f.id);
            return (
              <tr key={f.id} className={fl ? `flash-${fl}` : undefined}>
                <td className="num muted">{f.at}</td>
                <td>
                  <span className={`tag ${f.market.toLowerCase()}`}>{f.market}</span>
                </td>
                <td>
                  <span className={`tag ${sideClass(f.side)}`}>{sideText(f.side)}</span>
                </td>
                <td className="num r">{won(f.price)}</td>
                <td className="num r">{fq(f.qty)}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
    </>
  );
}

/**
 * 미체결·주문 내역·체결을 한 패널의 탭으로(T7-05). 미체결 탭이 기본이다 — 거래 중에 가장 자주 보는 것.
 * 주문 상태는 원장이 알려 준 그대로다. 나중 체결·취소가 방송으로 오면 그 줄이 깜빡인다.
 */
export function Activity({
  orders,
  rejects,
  fills,
  onCancel,
  notify,
  className,
}: {
  orders: OrderView[];
  rejects: LocalReject[];
  fills: Fill[];
  onCancel: (orderId: number) => Promise<{ ok: boolean; message: string }>;
  notify: Notify;
  className?: string;
}) {
  const [view, setView] = useState<View>("open");
  const [openId, setOpenId] = useState<number | null>(null);
  const [busy, setBusy] = useState<number | null>(null);
  const flash = useFlash(orders.map((o) => [String(o.orderId), o.filled + o.canceled] as [string, number]));

  const working = orders.filter((o) => !o.done);
  const shown = view === "open" ? working : orders;

  const cancel = async (o: OrderView) => {
    setBusy(o.orderId);
    try {
      const r = await onCancel(o.orderId);
      notify(
        r.ok ? "ok" : "error",
        r.ok ? "취소 완료" : "취소 실패",
        `${sideText(o.side)} ${won(o.price)}원 × ${fq(o.qty)}주 — ${r.message}`,
      );
    } finally {
      setBusy(null);
    }
  };

  const tabs: { id: View; label: string; n: number }[] = [
    { id: "open", label: "미체결", n: working.length },
    { id: "all", label: "주문 내역", n: orders.length + rejects.length },
    { id: "fills", label: "체결", n: fills.length },
  ];

  return (
    <Panel
      className={className}
      flush
      head={
        <div className="subtabs" role="tablist" aria-label="주문·체결">
          {tabs.map((t) => (
            <button key={t.id} role="tab" aria-selected={view === t.id} onClick={() => setView(t.id)}>
              {t.label}
              <span className="count num">{t.n}</span>
            </button>
          ))}
        </div>
      }
    >
      {view === "fills" ? (
        <FillTable fills={fills} />
      ) : shown.length === 0 && (view === "open" || rejects.length === 0) ? (
        <div className="empty">
          {view === "open"
            ? "미체결 주문이 없다. 지정가 주문이 바로 체결되지 않으면 여기 남는다."
            : "아직 낸 주문이 없다."}
        </div>
      ) : (
        <ul style={{ listStyle: "none", margin: 0, padding: 0 }}>
          {shown.map((o) => (
            <OrderRow
              key={o.orderId}
              o={o}
              open={openId === o.orderId}
              busy={busy === o.orderId}
              flash={flash(String(o.orderId))}
              onToggle={() => setOpenId(openId === o.orderId ? null : o.orderId)}
              onCancel={() => void cancel(o)}
            />
          ))}
          {view === "all" && rejects.map((r) => <RejectRow key={`r${r.clOrdId}`} r={r} />)}
        </ul>
      )}
    </Panel>
  );
}
