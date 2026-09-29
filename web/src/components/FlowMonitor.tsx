import { useEffect, useRef, useState } from "react";
import type { ReactNode } from "react";
import type { OrderFlow } from "../lib/useTrading";
import { VENUE_UNKNOWN } from "../lib/types";
import type { WireFrame } from "../lib/types";
import { fetchWire } from "../lib/api";
import { WireArchitecture } from "./WireArchitecture";
import { WireLogTable } from "./WireLogTable";
import type { OrderView } from "../lib/types";
import { MARKET_AUTO, SIDE_BUY, marketName, orderTypeText, sideText } from "../lib/wire";
import { money, qty as fq } from "../lib/format";

/**
 * 통신 모니터 — **구조도 위에 방금 지나간 주문의 실측값을 얹는다** (점검).
 *
 * <p>구조도만 그리면 그림이고, 숫자만 나열하면 표다. 둘을 겹쳐야 "이 주문이 어느 층을
 * 지나 무엇을 주고받고 얼마나 걸렸나"가 한눈에 보인다.
 *
 * <p><b>여기 있는 숫자는 전부 잰 것이다.</b> HTTP 왕복은 브라우저가 `performance.now()`로,
 * 전문 왕복은 채널계가 `nanoTime()`으로 쟀다. 전문 이름·종별·바이트는 규격
 * (`@WireMessage`, 고정 길이)에서 나오고, 시장별 다리는 원장이 돌려준 상세 그대로다.
 *
 * <p><b>없는 것은 적지 않는다.</b> 원장 안에서 검증·SOR·매칭이 각각 얼마나 걸렸는지는
 * 모른다 — 재지 않는다. 매칭 엔진 안에서 시스템 시각을 읽는 것은 이 프로젝트가 금지한다
 * (같은 입력이 같은 출력을 내야 전략 비교가 성립한다). 그래서 원장 칸에는 <b>왕복 하나</b>만
 * 적고 그 안을 쪼개지 않는다. FEP와 별도 거래소 프로세스도 이 구성에서는 지나지 않으므로
 * (T6-03 "최소 연결") 흐리게 두고 그렇게 적는다 — 지나지 않는 홉을 그리면 이 화면이
 * 거짓말이 된다.
 *
 * <p><b>구성이 바뀌면 그림도 바뀐다</b>(T12-05). 원장을 `--exchange`로 띄우면 FEP와
 * 거래소 프로세스가 실제로 지나는 길이 되고, 그때는 또렷하게 그린다. 그것을 화면이
 * 짐작하지 않는다 — <b>원장이 `SYMBOL_ACK`으로 말해 준 값</b>만 믿는다. 화면 설정에
 * 적어 두면 원장을 그냥 띄웠는데 화면만 FEP를 지난다고 그리는 일이 생긴다.
 */
/** 전문 내역을 다시 읽는 간격. 주기 조회가 1초에 한 번 도므로 그 눈금에 맞춘다 */
const POLL_MS = 1000;

export function FlowMonitor({
  flow,
  ledgerDown,
  wsState,
}: {
  flow: OrderFlow | null;
  ledgerDown: string | null;
  wsState: string;
}) {
  const [frames, setFrames] = useState<WireFrame[]>([]);
  const [total, setTotal] = useState(0);
  const [capacity, setCapacity] = useState(0);
  const [venue, setVenue] = useState(VENUE_UNKNOWN);
  const [err, setErr] = useState<string | null>(null);
  const lastId = useRef(0);

  /*
   * **받은 것만 이어 붙인다.** 매번 전부 받으면 몇 분 켜 두는 것만으로 응답이 수백
   * 킬로바이트가 되고, 펼쳐 둔 줄이 매초 닫힌다(목록이 통째로 갈리므로).
   */
  useEffect(() => {
    let alive = true;
    const tick = () => {
      fetchWire(lastId.current)
        .then((log) => {
          if (!alive) return;
          setErr(null);
          setTotal(log.total);
          setCapacity(log.capacity);
          setVenue(log.venue);
          if (log.frames.length > 0) {
            lastId.current = log.frames[log.frames.length - 1].id;
            setFrames((prev) => [...prev, ...log.frames].slice(-log.capacity));
          }
        })
        .catch((e: unknown) => {
          if (alive) setErr(e instanceof Error ? e.message : "전문 내역을 읽지 못했다");
        });
    };
    tick();
    const t = setInterval(tick, POLL_MS);
    return () => {
      alive = false;
      clearInterval(t);
    };
  }, []);

  return (
    <div className="flow">
      <WireArchitecture frames={frames} ledgerDown={ledgerDown} venue={venue} />

      <section className="flow-section">
        <h3>방금 낸 주문이 지나간 길</h3>
        {!flow && (
          <p className="note">
            아직 이 화면에서 주문을 내지 않았다. <b>거래 탭에서 한 건 내면</b> 그 주문이 지나간
            층과 각 구간에 걸린 시간이 여기 그대로 찍힌다.
          </p>
        )}
        <FlowDiagram flow={flow} ledgerDown={ledgerDown} wsState={wsState} />
        {flow && <FlowWire flow={flow} />}
      </section>

      <section className="flow-section">
        <h3>
          오간 전문 전체 내역{" "}
          <span className="muted num">
            누적 {total}건 · 최근 {capacity}건만 들고 있다
          </span>
        </h3>
        {err ? (
          <p className="note err-note" role="alert">
            <b>전문 내역을 읽지 못했다</b> — {err}
          </p>
        ) : (
          <WireLogTable frames={frames} />
        )}
      </section>

      <FlowLimits />
    </div>
  );
}

/** 마이크로초를 읽기 좋게. 1,000us를 넘으면 ms로. */
function us(v: number | null | undefined): string {
  if (v === null || v === undefined) return "—";
  if (v >= 1000) return `${(v / 1000).toFixed(2)}ms`;
  return `${Math.round(v)}µs`;
}

function ms(v: number | null | undefined): string {
  return v === null || v === undefined ? "—" : `${v.toFixed(2)}ms`;
}

/**
 * 계층 구조도. 지나간 층은 또렷하게, 이 구성에서 지나지 않는 층은 흐리게.
 *
 * 세로로 쌓는다 — 층이 다섯이라 가로로 벌리면 좁은 화면에서 바로 무너진다.
 */
function FlowDiagram({
  flow,
  ledgerDown,
  wsState,
}: {
  flow: OrderFlow | null;
  ledgerDown: string | null;
  wsState: string;
}) {
  const hop = flow?.res.ledger ?? null;
  /* 채널계 몫 = 화면이 잰 전체 - 원장 왕복. 음수면 잰 구간이 어긋난 것이라 적지 않는다 */
  const channelUs =
    flow?.res.httpMs !== undefined && hop ? Math.max(0, flow.res.httpMs * 1000 - hop.micros) : null;

  return (
    <ol className="flow-stack">
      <FlowNode
        step="1"
        name="화면 (React)"
        detail="주문창 → fetch POST /api/orders"
        state={flow ? "지나감" : "대기"}
        metrics={
          flow
            ? [
                [
                  "보낸 것",
                  `${sideText(flow.req.side)} ${fq(flow.req.qty)}주 · ${money(flow.req.price)}`,
                ],
                [
                  "시장",
                  flow.req.market === MARKET_AUTO ? "SOR 자동" : marketName(flow.req.market),
                ],
                ["유형", orderTypeText(flow.req.type)],
              ]
            : []
        }
      />

      <FlowEdge
        label="HTTP · JSON"
        time={ms(flow?.res.httpMs)}
        note="브라우저가 잰 왕복 — 아래 전부가 이 안에 들어 있다"
        lit={!!flow}
      />

      <FlowNode
        step="2"
        name="채널계 (Spring Boot)"
        detail="세션에서 계좌 확인 → 형식 검증 → 전문 조립"
        state={ledgerDown ? "원장 끊김" : flow ? "지나감" : "대기"}
        bad={!!ledgerDown}
        metrics={
          flow
            ? [
                ["계좌", "세션에서 (본문에 없다)"],
                ["이 층이 쓴 시간", us(channelUs)],
                ["방송 연결", wsState],
              ]
            : []
        }
      />

      <FlowEdge
        label={hop ? `고정 길이 전문 · TCP · ${hop.sent}` : "고정 길이 전문 · TCP"}
        time={us(hop?.micros)}
        note={
          hop
            ? `${hop.sentBytes}B 보내고 ${hop.gotBytes}B 받았다 · seq ${hop.seq}`
            : "프로세스 경계는 여기 하나뿐이다"
        }
        lit={!!hop}
      />

      <FlowNode
        step="3"
        name="원장 (C)"
        detail="증거금·보유 검증 → SOR 계획 → 매칭 엔진"
        state={
          flow
            ? flow.res.outcome === "ACCEPTED"
              ? "접수"
              : flow.res.outcome === "REJECTED"
                ? "거절"
                : "모름"
            : "대기"
        }
        bad={flow?.res.outcome === "REJECTED"}
        metrics={
          flow
            ? [
                ["판정", flow.res.outcome === "ACCEPTED" ? "통과" : flow.res.message],
                ["체결", `${fq(flow.res.filledQty)}주`],
                ["안쪽 단계별 시간", "재지 않는다"],
              ]
            : []
        }
      >
        {flow?.view && <LegStrip view={flow.view} />}
      </FlowNode>

      <FlowEdge
        label="같은 프로세스 안 (함수 호출)"
        time="—"
        note="전문이 오가지 않는다"
        lit={!!flow}
        inner
      />

      <FlowNode
        step="4"
        name="매칭 엔진 KRX · NXT (C)"
        detail="가격·시간 우선으로 체결을 판정한다"
        state={flow?.view ? `다리 ${flow.view.legs.length}개` : "대기"}
        inner
        metrics={
          flow?.view
            ? flow.view.legs.map(
                (l) =>
                  [marketName(l.market), `보냄 ${fq(l.sent)} · 체결 ${fq(l.filled)}`] as [
                    string,
                    string,
                  ],
              )
            : []
        }
      />

      <FlowNode
        step="—"
        name="FEP · 별도 거래소 프로세스"
        detail="설계에는 있지만 이 구성에서는 지나지 않는다 (T6-03 «최소 연결»)"
        state="지나지 않음"
        skipped
        metrics={[["검증", "T3-15 통합 테스트에서만 돈다"]]}
      />
    </ol>
  );
}

function FlowNode({
  step,
  name,
  detail,
  state,
  metrics,
  children,
  bad = false,
  skipped = false,
  inner = false,
}: {
  step: string;
  name: string;
  detail: string;
  state: string;
  metrics: [string, string][];
  children?: ReactNode;
  bad?: boolean;
  skipped?: boolean;
  inner?: boolean;
}) {
  return (
    <li className={`flow-node${skipped ? " skipped" : ""}${inner ? " inner" : ""}`}>
      <div className="flow-node-head">
        <span className="flow-step">{step}</span>
        <b>{name}</b>
        <span className="grow" />
        <span className={`tag ${bad ? "danger" : skipped ? "muted" : "ok"}`}>{state}</span>
      </div>
      <p className="flow-detail">{detail}</p>
      {metrics.length > 0 && (
        <dl className="flow-metrics">
          {metrics.map(([k, v]) => (
            <div key={k}>
              <dt>{k}</dt>
              <dd className="num">{v}</dd>
            </div>
          ))}
        </dl>
      )}
      {children}
    </li>
  );
}

/** 층과 층 사이. 무엇을 타고 갔는지와 걸린 시간. */
function FlowEdge({
  label,
  time,
  note,
  lit,
  inner = false,
}: {
  label: string;
  time: string;
  note: string;
  lit: boolean;
  inner?: boolean;
}) {
  return (
    <li
      className={`flow-edge${lit ? " lit" : ""}${inner ? " inner" : ""}`}
      aria-label={`${label}, ${time}`}
    >
      <span className="flow-edge-label">{label}</span>
      <span className="flow-edge-time num">{time}</span>
      <span className="flow-edge-note">{note}</span>
    </li>
  );
}

/** 원장 안에서 주문이 갈린 모양. 막대 길이는 이 주문 전체 수량 기준이다. */
function LegStrip({ view }: { view: OrderView }) {
  const base = Math.max(1, view.qty);
  const tone = view.side === SIDE_BUY ? "buy" : "sell";
  if (view.legs.length === 0) return null;
  return (
    <div className="flow-legs">
      {view.legs.map((l) => {
        const m = marketName(l.market);
        return (
          <div key={l.market} className="flow-leg">
            <span className={`tag ${m.toLowerCase()}`}>{m}</span>
            <div
              className="meter"
              role="img"
              aria-label={`${m}으로 ${fq(l.sent)}주가 나가 ${fq(l.filled)}주가 체결됐다`}
            >
              <span
                style={{ width: `${(l.filled / base) * 100}%`, background: `var(--${tone})` }}
              />
              <span
                style={{
                  width: `${((l.sent - l.filled - l.canceled) / base) * 100}%`,
                  background: "var(--line)",
                }}
              />
            </div>
            <span className="num">
              {fq(l.sent)} → {fq(l.filled)}
            </span>
          </div>
        );
      })}
    </div>
  );
}

/** 오간 전문을 바이트 단위로 적는다. 고정 길이 규격이 눈에 보이는 자리다. */
function FlowWire({ flow }: { flow: OrderFlow }) {
  const hop = flow.res.ledger;
  if (!hop) {
    return (
      <p className="note">
        <b>원장까지 가지 못했다</b> — {flow.res.message}. 오간 전문이 없어 적을 것이 없다.
      </p>
    );
  }
  const rows: [string, string, number, number][] = [
    ["보냄", hop.sent, hop.sentType, hop.sentBytes],
    ["받음", hop.got, hop.gotType, hop.gotBytes],
  ];
  return (
    <div className="flow-wire">
      <h3>오간 전문 — {flow.at}</h3>
      <table>
        <thead>
          <tr>
            <th scope="col">방향</th>
            <th scope="col">전문</th>
            <th scope="col" className="r">
              종별
            </th>
            <th scope="col" className="r">
              길이
            </th>
          </tr>
        </thead>
        <tbody>
          {rows.map(([dir, name, type, bytes]) => (
            <tr key={dir}>
              <td>{dir}</td>
              <td>
                <b>{name}</b>
              </td>
              <td className="num r">{type}</td>
              <td className="num r">{bytes}B</td>
            </tr>
          ))}
        </tbody>
      </table>
      <p className="note">
        길이는 <b>고정</b>이다 — 같은 종별이면 언제나 같은 바이트 수이고, 받는 쪽은 길이가
        규격과 다르면 접속을 끊는다. 그 앞에 헤더가 붙는다. seq {hop.seq}는 이 요청에 실어
        보낸 순번이고, 접수 뒤 주문 상세를 다시 읽는 데 {ms(flow.detailMs)}가 더 들었다 —
        전문 한 왕복이 더 있었다는 뜻이다.
      </p>
    </div>
  );
}

/** 이 화면이 말하지 못하는 것. 적어 두지 않으면 없는 것을 있는 줄로 읽는다. */
function FlowLimits() {
  return (
    <div className="flow-limits">
      <h3>이 화면이 말하지 못하는 것</h3>
      <ul>
        <li>
          <b>원장 안의 단계별 시간</b> — 검증·SOR·매칭이 각각 얼마나 걸렸는지 재지 않는다.
          매칭 엔진이 시스템 시각을 읽으면 같은 입력이 같은 출력을 내지 않게 되고, 그러면
          전략 비교가 무너진다. 원장 칸의 시간은 <b>왕복 하나</b>다.
        </li>
        <li>
          <b>FEP와 별도 거래소 프로세스를 지나는 경로</b> — 코드는 있지만 지금 구성에서는
          SOR과 매칭 엔진이 원장 프로세스 안에서 돈다. 프로세스 경계는 화면↔채널계,
          채널계↔원장 둘뿐이다.
        </li>
        <li>
          <b>시간의 분포</b> — 여기 보이는 것은 방금 낸 <b>한 건</b>이다. p50·p99는 벤치마크
          (<code>bench/bench_pipeline</code>)가 재고 <code>bench/results/</code>에 있다.
        </li>
      </ul>
    </div>
  );
}
