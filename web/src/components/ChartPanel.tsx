import { useEffect, useState } from "react";
import type { Candle, FeedStatus, Tick } from "../lib/types";
import { fetchCandles } from "../lib/api";
import { CandleChart } from "./CandleChart";
import { PriceChart } from "./PriceChart";
import { won, qty as fq } from "../lib/format";

type Interval = "1m" | "1d";

const INTERVALS: { id: Interval; label: string }[] = [
  { id: "1m", label: "1분" },
  { id: "1d", label: "1일" },
];

/** 봉을 다시 받는 간격. 1분봉은 20초, 일봉은 5분 — 채널계 캐시와 같은 눈금이다. */
const REFRESH_MS: Record<Interval, number> = { "1m": 20_000, "1d": 300_000 };

/**
 * 차트 칸.
 *
 * <b>봉은 바깥 시장의 체결</b>이고 호가창(내 원장)과는 별개다. 토스 설정이 없으면 봉을 받을
 * 길이 없으므로 <b>내 호가창의 중간가 선</b>으로 되돌아간다 — 빈 화면을 보여 주는 대신
 * 무엇을 그리고 있는지 밝힌다.
 */
export function ChartPanel({ ticks, feed }: { ticks: Tick[]; feed: FeedStatus | null }) {
  const [interval, setInterval] = useState<Interval>("1m");
  const [candles, setCandles] = useState<Candle[] | null>(null);
  const [state, setState] = useState<"loading" | "ok" | "none">("loading");

  /*
   * 봉은 두 모드가 **서로 다른 데이터**다.
   *
   * 실시세에서는 토스가 준 바깥 시장의 봉이고, 시뮬에서는 채널계가 내 원장의 체결을 1분씩
   * 묶은 봉이다(가상 참가자끼리도 실제로 체결이 난다). 섞으면 위 차트와 아래 호가가 따로
   * 논다 — 같은 시장의 두 모습이라고 읽히기 때문이다. 고르는 일은 채널계가 한다.
   *
   * 시뮬에는 일봉이 없다. 하루치를 모으려면 하루를 돌려야 하고 원장을 다시 띄우면 처음부터다.
   */
  const live = feed?.mode === "live";
  /* 실시세를 끄면 일봉이 없다. 고른 단위가 무엇이든 시뮬에서는 1분봉을 본다 */
  const shown: Interval = live ? interval : "1m";

  /*
   * 봉을 받아 오고, 단위마다 정해진 간격으로 다시 받는다.
   *
   * "받는 중"은 여기서 세우지 않는다 — 단위를 바꾼 **그 클릭**이 세운다. 상태를 바꾸는 것은
   * 응답이 돌아온 뒤(`then`)뿐이다. 칸을 떠난 뒤 늦게 온 응답은 `alive`로 버린다.
   */
  useEffect(() => {
    let alive = true;
    const run = () => {
      fetchCandles(shown)
        .then((got) => {
          if (!alive) return;
          setCandles(got?.candles ?? null);
          setState(got ? "ok" : "none");
        })
        .catch(() => {
          if (alive) setState("none");
        });
    };
    run();
    const t = window.setInterval(run, REFRESH_MS[shown]);
    return () => {
      alive = false;
      window.clearInterval(t);
    };
  }, [shown]);

  const pick = (iv: Interval) => {
    if (iv === interval) return;
    setState("loading");
    setCandles(null);
    setInterval(iv);
  };

  const last = candles && candles.length > 0 ? candles[candles.length - 1] : null;
  const prev = candles && candles.length > 1 ? candles[candles.length - 2] : null;
  const diff = last && prev ? last.close - prev.close : 0;

  return (
    <div className="chart-panel">
      <div className="chart-head">
        {live ? (
          <div className="seg-iv" role="group" aria-label="봉 단위">
            {INTERVALS.map((iv) => (
              <button
                key={iv.id}
                type="button"
                aria-pressed={interval === iv.id}
                onClick={() => pick(iv.id)}
              >
                {iv.label}
              </button>
            ))}
          </div>
        ) : (
          <span className="muted" style={{ fontSize: 12 }}>
            시뮬 · 1분봉
          </span>
        )}

        {last && (
          <div className="chart-ohlc num">
            <span>시 {won(last.open)}</span>
            <span>고 {won(last.high)}</span>
            <span>저 {won(last.low)}</span>
            <span>
              종 <b style={{ color: diff >= 0 ? "var(--buy)" : "var(--sell)" }}>{won(last.close)}</b>
              {prev && (
                <span style={{ color: diff >= 0 ? "var(--buy)" : "var(--sell)" }}>
                  {" "}
                  {diff >= 0 ? "▲" : "▼"} {won(Math.abs(diff))}
                </span>
              )}
            </span>
            <span>거래량 {fq(last.volume)}</span>
          </div>
        )}
      </div>

      {state === "ok" && candles && candles.length > 1 ? (
        <CandleChart candles={candles} interval={shown} />
      ) : state === "loading" ? (
        <div className="empty">봉을 받는 중…</div>
      ) : (
        <PriceChart ticks={ticks} />
      )}

      <p className="note">
        {state === "ok" && live ? (
          <>
            <b>바깥 시장의 봉</b>이다(토스증권 {shown === "1d" ? "일봉" : "1분봉"}). 아래 호가창은
            이 봉과 같은 시세를 심은 것이고, 주문·체결·잔고는 이 프로젝트의 원장에서만 일어난다.
          </>
        ) : state === "ok" ? (
          <>
            <b>이 원장에서 실제로 난 체결</b>을 1분씩 묶은 봉이다. 가상 참가자끼리의 거래도
            거래량에 들어간다 — 바깥 시장과는 무관한, 이 시뮬만의 시세다.
          </>
        ) : live ? (
          <>
            봉을 받지 못해 <b>내 호가창의 중간가</b>를 그린다
            {feed?.error ? ` — ${feed.error}` : ""}.
          </>
        ) : (
          <>
            봉을 만들 만큼 체결이 쌓이지 않아 <b>내 호가창의 중간가</b>를 그린다. 1분쯤 지나면
            봉으로 바뀐다.
          </>
        )}
      </p>
    </div>
  );
}
