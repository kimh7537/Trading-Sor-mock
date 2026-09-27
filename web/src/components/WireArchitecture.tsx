import type { WireFrame } from "../lib/types";

/**
 * 계층 **구조도**를 인라인 SVG로 그린다 (점검).
 *
 * <p>박스와 화살표를 실제로 그려야 "무엇이 무엇과 이야기하는가"가 한눈에 보인다. 글머리표
 * 목록은 구조가 아니라 목차다.
 *
 * <p><b>선 위의 숫자는 살아 있다.</b> 지금까지 그 선으로 오간 전문 수·바이트·중앙값 시간을
 * 실제 기록에서 세어 얹는다. 지나간 선은 또렷하게, 한 번도 안 지나간 선은 흐리게 그린다.
 *
 * <p><b>지나지 않는 홉은 지나지 않는다고 그린다.</b> FEP와 별도 거래소 프로세스는 설계에는
 * 있지만 이 구성에서는 SOR·매칭 엔진이 원장 프로세스 안에서 돈다(T6-03 "최소 연결").
 * 흐리게 + 점선 + "지나지 않음" 글자까지 셋으로 말한다 — 색만으로 말하지 않는다.
 *
 * <p>viewBox로 그려 폭에 맞춰 줄어든다. 320px에서도 가로 스크롤이 생기지 않는다.
 */
export function WireArchitecture({
  frames,
  ledgerDown,
}: {
  frames: WireFrame[];
  ledgerDown: string | null;
}) {
  const n = frames.length;
  const bytes = frames.reduce((s, f) => s + f.sentBytes + f.gotBytes, 0);
  const mid = median(frames.map((f) => f.micros));
  const lost = frames.filter((f) => !f.ok).length;
  const live = n > 0;

  return (
    <figure className="arch">
      <svg viewBox="0 0 600 476" role="img" aria-labelledby="arch-title arch-desc">
        <title id="arch-title">계층 구조도</title>
        <desc id="arch-desc">
          화면과 채널계는 HTTP와 WebSocket으로, 채널계와 원장은 고정 길이 전문으로 통신한다.
          원장 프로세스 안에 SOR과 KRX·NXT 매칭 엔진이 있다. FEP와 별도 거래소 프로세스는 이
          구성에서 지나지 않는다.
        </desc>

        <Box x={150} y={10} w={300} h={52} title="화면 (React)" sub="주문창 · 호가 · 차트" lit={live} />

        <Arrow x={250} y1={62} y2={112} lit={live} />
        <EdgeLabel x={258} y={82} label="HTTP · JSON" note="주문 · 조회" lit={live} />
        <Arrow x={350} y1={112} y2={62} lit={live} />
        <EdgeLabel x={358} y={82} label="WebSocket" note="체결 · 잔고 방송" lit={live} />

        <Box
          x={150}
          y={112}
          w={300}
          h={58}
          title="채널계 (Java / Spring Boot)"
          sub="세션·검증 · 전문 조립 · 체결 기록"
          lit={live}
        />

        <Arrow x={300} y1={170} y2={224} lit={live} both />
        <EdgeLabel
          x={308}
          y={190}
          label="고정 길이 전문 · TCP"
          note={live ? `${n}건 · ${fmtBytes(bytes)} · 중앙 ${fmtUs(mid)}` : "아직 오간 것 없음"}
          lit={live}
          strong
        />

        <g>
          <rect
            className={`arch-proc${ledgerDown ? " bad" : live ? " lit" : ""}`}
            x={70}
            y={224}
            width={460}
            height={154}
            rx={10}
          />
          <text className="arch-proc-tag" x={82} y={242}>
            원장 프로세스 (C) — 아래 셋이 한 프로세스 안에 있다
          </text>

          <Box x={86} y={250} w={428} h={44} title="원장" sub="증거금·보유 검증 · 계좌 원장" lit={live} inner />
          <Arrow x={300} y1={294} y2={316} lit={live} />
          <Box
            x={86}
            y={316}
            w={428}
            h={44}
            title="SOR 엔진"
            sub="통합 호가창 · 최선집행 평가 · 라우팅"
            lit={live}
            inner
          />
        </g>

        <Arrow x={190} y1={378} y2={402} lit={live} />
        <Arrow x={410} y1={378} y2={402} lit={live} />
        <Box x={86} y={402} w={200} h={40} title="KRX 매칭 엔진" sub="가격·시간 우선" lit={live} inner />
        <Box x={314} y={402} w={200} h={40} title="NXT 매칭 엔진" sub="가격·시간 우선" lit={live} inner />

        <rect className="arch-skip" x={70} y={452} width={460} height={22} rx={6} />
        <text className="arch-skip-text" x={300} y={467} textAnchor="middle">
          FEP · 별도 거래소 프로세스 — 이 구성에서는 지나지 않음 (T3-15 테스트로만)
        </text>
      </svg>

      <figcaption className="arch-legend">
        <span>
          <b className="num">{n}</b>건 오감
        </span>
        <span>
          <b className="num">{fmtBytes(bytes)}</b> 주고받음
        </span>
        <span>
          왕복 중앙값 <b className="num">{fmtUs(mid)}</b>
        </span>
        {lost > 0 && (
          <span className="bad">
            답 못 받음 <b className="num">{lost}</b>건
          </span>
        )}
        {ledgerDown && <span className="bad">원장 끊김 — {ledgerDown}</span>}
      </figcaption>
    </figure>
  );
}

function Box({
  x,
  y,
  w,
  h,
  title,
  sub,
  lit,
  inner = false,
}: {
  x: number;
  y: number;
  w: number;
  h: number;
  title: string;
  sub: string;
  lit: boolean;
  inner?: boolean;
}) {
  return (
    <g>
      <rect
        className={`arch-box${lit ? " lit" : ""}${inner ? " inner" : ""}`}
        x={x}
        y={y}
        width={w}
        height={h}
        rx={8}
      />
      <text className="arch-title" x={x + w / 2} y={y + 22} textAnchor="middle">
        {title}
      </text>
      <text className="arch-sub" x={x + w / 2} y={y + 37} textAnchor="middle">
        {sub}
      </text>
    </g>
  );
}

/** 세로 화살표. `both`면 양쪽에 머리를 단다(요청과 응답이 같은 선을 쓴다). */
function Arrow({
  x,
  y1,
  y2,
  lit,
  both = false,
}: {
  x: number;
  y1: number;
  y2: number;
  lit: boolean;
  both?: boolean;
}) {
  const head = 5;
  const dir = y2 > y1 ? 1 : -1;
  return (
    <g className={`arch-arrow${lit ? " lit" : ""}`}>
      <line x1={x} y1={y1} x2={x} y2={y2 - dir * head} />
      <polygon points={`${x},${y2} ${x - 4},${y2 - dir * head} ${x + 4},${y2 - dir * head}`} />
      {both && (
        <polygon points={`${x},${y1} ${x - 4},${y1 + dir * head} ${x + 4},${y1 + dir * head}`} />
      )}
    </g>
  );
}

function EdgeLabel({
  x,
  y,
  label,
  note,
  lit,
  strong = false,
}: {
  x: number;
  y: number;
  label: string;
  note: string;
  lit: boolean;
  strong?: boolean;
}) {
  return (
    <g>
      <text className={`arch-edge${lit ? " lit" : ""}${strong ? " strong" : ""}`} x={x} y={y}>
        {label}
      </text>
      <text className="arch-edge-note" x={x} y={y + 13}>
        {note}
      </text>
    </g>
  );
}

function median(xs: number[]): number | null {
  if (xs.length === 0) return null;
  const s = [...xs].sort((a, b) => a - b);
  return s[Math.floor(s.length / 2)];
}

function fmtUs(v: number | null): string {
  if (v === null) return "—";
  return v >= 1000 ? `${(v / 1000).toFixed(2)}ms` : `${v}µs`;
}

function fmtBytes(v: number): string {
  return v >= 1024 ? `${(v / 1024).toFixed(1)}KB` : `${v}B`;
}
