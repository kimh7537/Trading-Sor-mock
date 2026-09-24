import type { Market, Tick } from "../lib/types";
import {  money, moneyUnit, qty as fq } from "../lib/format";

const W = 560;
const H = 110;
const VOL_H = 24;
const PAD = 4;
/* 값의 위아래로 남기는 여백. 없으면 선이 그림 맨 끝에 붙어 변동이 커 보인다 */
const HEADROOM = 0.15;

const COLOR: Record<Market, string> = { KRX: "var(--krx)", NXT: "var(--nxt)" };
const MARKETS: Market[] = ["KRX", "NXT"];

const at = (t: Tick, m: Market) => (m === "KRX" ? t.krx : t.nxt);

/**
 * 가격과 거래량(T8-05). 라이브러리를 쓰지 않는다 — 선 둘과 막대 몇 개에 의존을 더할 일이 없다.
 *
 * <b>값을 지어내지 않는다.</b>
 * - 선: <b>시장마다</b> 최우선호가의 중간값. 원장 호가가 바뀔 때마다 한 점.
 *   두 시장을 합치지 않는 이유는 합치면 한쪽 최우선 매도와 다른 쪽 최우선 매수가 기준가에서
 *   맞물려 <b>선이 평평해지기</b> 때문이다. 나뉘어 있어야 두 시장의 가격 차이가 보인다
 * - 막대: 그 사이에 체결된 <b>내 주문 수량</b>. 시장 전체 거래량이 아니다 —
 *   원장이 체결 통보를 내 주문에 대해서만 주기 때문이다
 */
export function PriceChart({ ticks }: { ticks: Tick[] }) {
  const shown = MARKETS.filter((m) => ticks.some((t) => at(t, m) > 0));
  if (ticks.length < 2 || shown.length === 0) {
    return <div className="empty">호가가 두 번 바뀌면 그려진다…</div>;
  }

  const prices = ticks.flatMap((t) => shown.map((m) => at(t, m))).filter((p) => p > 0);
  const min = Math.min(...prices);
  const max = Math.max(...prices);
  const flat = max === min;
  // 값의 위아래에 여백을 둔다. 없으면 두 선이 그림의 맨 위·맨 아래에 붙어 버린다
  const pad = Math.max(1, (max - min) * HEADROOM);
  const lo = min - pad;
  const span = Math.max(1, max - min + pad * 2);
  const maxVol = Math.max(1, ...ticks.map((t) => t.vol));

  const x = (i: number) => PAD + (i / (ticks.length - 1)) * (W - PAD * 2);
  const plot = H - VOL_H - PAD * 2;
  // 값이 하나뿐이면 가운데에 긋는다. 그러지 않으면 바닥에 붙어 오해를 부른다
  const y = (p: number) => PAD + (flat ? plot / 2 : (1 - (p - lo) / span) * plot);

  /** 값이 없는 점(그 시장 호가가 아직 없다)에서는 선을 끊는다 */
  const path = (m: Market) => {
    let d = "";
    let pen = false;
    ticks.forEach((t, i) => {
      const v = at(t, m);
      if (v <= 0) {
        pen = false;
        return;
      }
      d += `${pen ? "L" : "M"}${x(i).toFixed(1)},${y(v).toFixed(1)} `;
      pen = true;
    });
    return d.trim();
  };

  const last = ticks[ticks.length - 1];
  const totalVol = ticks.reduce((s, t) => s + t.vol, 0);
  const label = shown
    .map((m) => `${m} ${at(last, m) ? money(at(last, m)) : "—"}원`)
    .join(", ");

  return (
    <div className="stack">
      <svg
        className="chart"
        viewBox={`0 0 ${W} ${H}`}
        preserveAspectRatio="none"
        role="img"
        aria-label={`중간가 ${label}. 구간 ${money(min)}~${moneyUnit(max)}. 내 체결 ${fq(totalVol)}주`}
      >
        {/*
          두 시장의 중간가가 같으면 선이 완전히 겹친다. 나중에 그린 것만 보여 한쪽이
          사라진 것처럼 읽히므로, 먼저 그리는 쪽을 굵게 둬서 테두리가 남게 한다.
        */}
        {shown.map((m, i) => (
          <path
            key={m}
            d={path(m)}
            fill="none"
            stroke={COLOR[m]}
            strokeWidth={i === 0 && shown.length > 1 ? 3.5 : 1.5}
          />
        ))}
        {ticks.map((t, i) =>
          t.vol > 0 ? (
            <rect
              key={t.t}
              x={x(i) - 1.5}
              width="3"
              y={H - PAD - (t.vol / maxVol) * VOL_H}
              height={(t.vol / maxVol) * VOL_H}
              fill="var(--ok)"
            />
          ) : null,
        )}
      </svg>

      <div className="split-legend num">
        {shown.map((m) => (
          <span key={m}>
            <span className="swatch" style={{ background: COLOR[m] }} />
            {m} 중간가 {at(last, m) ? `${moneyUnit(at(last, m))}` : "—"}
          </span>
        ))}
        <span>
          <span className="swatch" style={{ background: "var(--ok)" }} />내 체결 {fq(totalVol)}주
        </span>
        <span className="muted">
          구간 {money(min)}~{money(max)}
        </span>
      </div>

      {/*
        평평한 것이 고장으로 보이지 않게 이유를 적는다. 시뮬 모드의 가상 참가자는 기준가
        근처에만 주문을 내므로 최우선호가가 거의 고정이고, 바뀌는 것은 잔량이다.
      */}
      <p className="note">
        선 = 시장별 중간가 · 막대 = <b>내 주문의</b> 체결 수량(시장 전체 거래량이 아니다).
        {flat && (
          <>
            {" "}
            <b>지금은 변동 없음</b> — 바뀌는 것은 잔량이다(호가창의 ▲▼).
          </>
        )}
      </p>
    </div>
  );
}
