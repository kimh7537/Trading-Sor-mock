import type { Candle } from "../lib/types";
import { won, qty as fq } from "../lib/format";

const PAD_L = 4;
const PAD_R = 54; // 오른쪽 가격 눈금 자리
const VOL_RATIO = 0.22; // 아래 거래량이 쓰는 높이 비율

/** 국내 관행: 오른 봉 빨강, 내린 봉 파랑. 색만으로 뜻을 전하지 않는다 — 값도 함께 적는다 */
const UP = "var(--buy)";
const DOWN = "var(--sell)";

const fmtTime = (t: number, interval: "1m" | "1d") => {
  const d = new Date(t);
  return interval === "1d"
    ? `${d.getMonth() + 1}/${d.getDate()}`
    : `${String(d.getHours()).padStart(2, "0")}:${String(d.getMinutes()).padStart(2, "0")}`;
};

/**
 * 캔들 차트(OHLCV). 라이브러리를 쓰지 않는다 — 사각형과 선분이라 의존을 더할 일이 없다.
 *
 * <b>이것은 바깥 시장의 체결이다.</b> 토스에서 받은 실제 봉이고, 이 프로젝트 원장의 호가창과는
 * 별개다. 시뮬 모드에서 가상 참가자가 만든 체결은 여기 섞이지 않는다 — 섞으면 "실제 시장이
 * 이렇게 움직였다"와 "내 시뮬이 이렇게 움직였다"를 구분할 수 없다.
 */
export function CandleChart({
  candles,
  interval,
  height = 300,
}: {
  candles: Candle[];
  interval: "1m" | "1d";
  height?: number;
}) {
  if (candles.length === 0) {
    return <div className="empty">봉을 받지 못했다.</div>;
  }

  const W = 1000;
  const H = height;
  const volH = H * VOL_RATIO;
  const priceH = H - volH - 18; // 아래 시각 눈금 자리

  const hi = Math.max(...candles.map((c) => c.high));
  const lo = Math.min(...candles.map((c) => c.low));
  const pad = Math.max(1, (hi - lo) * 0.08);
  const top = hi + pad;
  const span = Math.max(1, top - (lo - pad));
  const maxVol = Math.max(1, ...candles.map((c) => c.volume));

  const plotW = W - PAD_L - PAD_R;
  const step = plotW / candles.length;
  const bodyW = Math.max(1, Math.min(14, step * 0.66));

  const x = (i: number) => PAD_L + step * (i + 0.5);
  const y = (p: number) => ((top - p) / span) * priceH;
  const volY = (v: number) => H - 18 - (v / maxVol) * volH;

  const last = candles[candles.length - 1];
  const first = candles[0];
  const up = last.close >= first.open;

  /* 가격 눈금 넷. 값도 글자로 적는다 */
  const ticks = [0, 1, 2, 3].map((k) => lo - pad + (span * k) / 3);
  /* 시각 눈금은 다섯 개면 읽힌다. 봉이 다섯 개보다 적으면 같은 봉이 거듭 뽑히므로 겹치는 것을 버린다 */
  const timeIdx = [
    ...new Set(
      [0, 1, 2, 3, 4].map((k) => Math.min(candles.length - 1, Math.round(((candles.length - 1) * k) / 4))),
    ),
  ];

  return (
    <svg
      className="candles"
      viewBox={`0 0 ${W} ${H}`}
      preserveAspectRatio="none"
      role="img"
      aria-label={
        `${interval === "1d" ? "일봉" : "1분봉"} ${candles.length}개. ` +
        `마지막 종가 ${won(last.close)}원, 고가 ${won(hi)}, 저가 ${won(lo)}, ` +
        `거래량 ${fq(last.volume)}주. 구간 ${up ? "상승" : "하락"}`
      }
    >
      {ticks.map((p) => (
        <g key={p}>
          <line x1={PAD_L} x2={W - PAD_R} y1={y(p)} y2={y(p)} stroke="var(--line-soft)" strokeWidth="1" />
          <text x={W - PAD_R + 4} y={y(p) + 3} fill="var(--text-faint)" fontSize="10">
            {won(Math.round(p))}
          </text>
        </g>
      ))}

      {candles.map((c, i) => {
        const rise = c.close >= c.open;
        const color = rise ? UP : DOWN;
        const bodyTop = y(Math.max(c.open, c.close));
        const bodyH = Math.max(1, y(Math.min(c.open, c.close)) - bodyTop);
        return (
          <g key={c.t}>
            <line
              x1={x(i)}
              x2={x(i)}
              y1={y(c.high)}
              y2={y(c.low)}
              stroke={color}
              strokeWidth="1"
            />
            <rect x={x(i) - bodyW / 2} y={bodyTop} width={bodyW} height={bodyH} fill={color} />
            <rect
              x={x(i) - bodyW / 2}
              y={volY(c.volume)}
              width={bodyW}
              height={H - 18 - volY(c.volume)}
              fill={color}
              opacity="0.45"
            />
          </g>
        );
      })}

      {timeIdx.map((i) => (
        <text
          key={i}
          x={x(i)}
          y={H - 4}
          fill="var(--text-faint)"
          fontSize="10"
          textAnchor="middle"
        >
          {fmtTime(candles[i].t, interval)}
        </text>
      ))}
    </svg>
  );
}
