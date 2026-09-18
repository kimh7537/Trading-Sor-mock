import type { Tick } from "../lib/types";
import { won, qty as fq } from "../lib/format";

const W = 560;
const H = 150;
const VOL_H = 34;
const PAD = 4;

/**
 * 가격과 거래량(T8-05). 라이브러리를 쓰지 않는다 — 선 하나와 막대 몇 개에 의존을 더할 일이 없다.
 *
 * <b>값을 지어내지 않는다.</b>
 * - 선: 두 시장을 합친 최우선호가의 <b>중간값</b>. 원장 호가가 바뀔 때마다 한 점
 * - 막대: 그 사이에 체결된 <b>내 주문 수량</b>. 시장 전체 거래량이 아니다 —
 *   원장이 체결 통보를 내 주문에 대해서만 주기 때문이다. 그렇게 적는다
 */
export function PriceChart({ ticks }: { ticks: Tick[] }) {
  if (ticks.length < 2) {
    return <div className="empty">호가가 두 번 바뀌면 그려진다…</div>;
  }

  const prices = ticks.map((t) => t.mid).filter((p) => p > 0);
  const lo = Math.min(...prices);
  const hi = Math.max(...prices);
  const span = Math.max(1, hi - lo);
  const maxVol = Math.max(1, ...ticks.map((t) => t.vol));

  const x = (i: number) => PAD + (i / (ticks.length - 1)) * (W - PAD * 2);
  const y = (p: number) => PAD + (1 - (p - lo) / span) * (H - VOL_H - PAD * 2);

  const line = ticks
    .map((t, i) => `${i === 0 ? "M" : "L"}${x(i).toFixed(1)},${y(t.mid).toFixed(1)}`)
    .join(" ");

  const last = ticks[ticks.length - 1];
  const first = ticks[0];
  const up = last.mid >= first.mid;
  const totalVol = ticks.reduce((s, t) => s + t.vol, 0);

  return (
    <div className="stack">
      <svg
        className="chart"
        viewBox={`0 0 ${W} ${H}`}
        preserveAspectRatio="none"
        role="img"
        aria-label={`중간가 ${won(first.mid)}원에서 ${won(last.mid)}원으로 ${up ? "상승" : "하락"}. 내 체결 ${fq(totalVol)}주`}
      >
        <path d={line} fill="none" stroke={up ? "var(--buy)" : "var(--sell)"} strokeWidth="1.5" />
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

      <dl className="kv num">
        <div>
          <dt>중간가</dt>
          <dd>{won(last.mid)}원</dd>
        </div>
        <div>
          <dt>구간 고저</dt>
          <dd>
            {won(hi)} / {won(lo)}
          </dd>
        </div>
        <div>
          <dt>내 체결</dt>
          <dd>{fq(totalVol)}주</dd>
        </div>
      </dl>

      <p className="note">
        선은 두 시장을 합친 최우선호가의 중간값이다. 막대는 <b>내 주문의 체결 수량</b>이고
        시장 전체 거래량이 아니다 — 원장은 내 주문에 대해서만 체결을 통보한다.
      </p>
    </div>
  );
}
