import type { LegView, OrderView } from "../lib/types";
import { MARKET_AUTO, SIDE_BUY, marketName, orderTypeText, sideText } from "../lib/wire";
import { money, moneyUnit, qty as fq } from "../lib/format";

/**
 * 주문 해부 — **논리 주문 1건이 시장별 물리 주문 N건으로 갈려 체결되는 과정**을
 * 그 주문의 실제 데이터만으로 되짚는다.
 *
 * <p>복수시장 집행이 화면에서 블랙박스로 보이던 것이 문제였다. 주문 목록은 "100주 중
 * 100주 체결"까지만 말하고, 그 100주가 어느 시장에 몇 주씩 갈렸는지는 다리 목록을 눈으로
 * 더해 봐야 알 수 있었다. 이 프로젝트의 논지가 바로 그 분할이므로 한눈에 보여야 한다.
 *
 * <p><b>있는 값만 쓴다.</b> 주문 당시 양 시장 호가, SOR이 그 시장을 고른 이유, 엔진 처리
 * 시간·순서는 이 주문 데이터에 없다. 없는 것을 그럼직하게 지어내면 이 화면의 쓸모가 사라진다.
 * 체결 내역(`Fill`)도 쓰지 않는다 — 화면을 연 뒤 들어온 것만 쌓여서, 과거 주문을 "체결 0건"
 * 으로 보이게 만든다.
 *
 * <p><b>실행 순서를 흉내 내지 않는다.</b> 위에서 아래로 읽히는 것은 데이터의 구조(논리 →
 * 물리 → 결과)이지 시간축이 아니다. 그래서 애니메이션도 넣지 않는다.
 *
 * <p>금액은 원장이 센 정수를 그대로 쓴다. 평균 단가도 서버가 `체결금액 / 체결수량`으로
 * 내린 값(`avgPrice`)이고, 화면에서 실수로 다시 나누지 않는다 — 두 군데서 계산하면
 * 언젠가 1원이 어긋나고 그때 어느 쪽이 맞는지 알 수 없다(T7-07에서 실제로 겪었다).
 */
export function OrderDissect({ o }: { o: OrderView }) {
  const sor = o.market === MARKET_AUTO;
  const tone = o.side === SIDE_BUY ? "buy" : "sell";
  /* 나간 몫이 있는 다리만 온다 — 채널계가 보낸 수량 0인 시장은 싣지 않는다 */
  const legs = o.legs;
  const split = legs.length > 1;
  const shares = sharePercents(legs, o.qty);

  return (
    <section className="dissect" aria-label={`주문 ${o.orderId} 해부`}>
      {/* A. 논리 주문 — 사용자가 낸 한 건 */}
      <div className="dnode dnode-head">
        <span className="dstep">1 · 논리 주문</span>
        <div className="dline">
          <span className={`tag ${tone}`}>{sideText(o.side)}</span>
          <span className="tag muted">{orderTypeText(o.type)}</span>
          <span className={`tag ${sor ? "sor" : marketName(o.market).toLowerCase()}`}>
            {sor ? "SOR 자동" : marketName(o.market)}
          </span>
          <b className="num">{fq(o.qty)}주</b>
          <span className="dmuted num">지정 {money(o.price)}</span>
        </div>
      </div>

      {/* B. 갈린 자리. 다리가 둘 이상일 때만 "나눴다"고 적는다 */}
      <p className="dstep dsplit">
        {legs.length === 0
          ? "2 · 시장으로 나간 몫이 없다"
          : sor
            ? split
              ? `2 · SOR이 ${legs.length}개 시장으로 나눴다`
              : "2 · SOR이 한 시장을 골랐다"
            : "2 · 지정한 시장으로 그대로 나갔다"}
      </p>

      {/* C. 시장별 물리 주문과 그 결과 */}
      {legs.length > 0 && (
        <div className={`dlegs${split ? " branched" : ""}`}>
          {legs.map((l, i) => (
            <DissectLeg key={l.market} leg={l} total={o.qty} pct={shares[i]} tone={tone} />
          ))}
        </div>
      )}

      {/* D. 최종 집행 결과. 논리 주문이 들고 있는 값이 기준이다 */}
      <div className="dnode dnode-sum">
        <span className="dstep">3 · 최종 집행 결과</span>
        <dl className="dsum">
          <div>
            <dt>총 주문</dt>
            <dd className="num">{fq(o.qty)}주</dd>
          </div>
          <div>
            <dt>총 체결</dt>
            <dd className="num">{fq(o.filled)}주</dd>
          </div>
          <div>
            <dt>미체결</dt>
            <dd className="num">{fq(o.working)}주</dd>
          </div>
          <div>
            <dt>취소</dt>
            <dd className="num">{fq(o.canceled)}주</dd>
          </div>
          <div>
            <dt>총 체결금액</dt>
            <dd className="num">{o.filled > 0 ? money(o.notional) : "—"}</dd>
          </div>
          <div>
            <dt>평균 체결단가</dt>
            <dd className="num">{o.filled > 0 ? moneyUnit(o.avgPrice) : "—"}</dd>
          </div>
        </dl>
        <p className="dnote">
          {o.filled === 0
            ? o.canceled > 0
              ? "한 주도 체결되지 않고 취소됐다."
              : "아직 한 주도 체결되지 않았다."
            : o.filled < o.qty
              ? `주문한 ${fq(o.qty)}주 가운데 ${fq(o.filled)}주가 체결됐다.`
              : `주문한 ${fq(o.qty)}주가 전량 체결됐다.`}
          {split && o.filled > 0 && " 체결금액은 두 시장 몫을 합한 값이다."}
        </p>
      </div>
    </section>
  );
}

/**
 * 다리별 비중을 **합이 정확히 100이 되게** 정수로 나눈다.
 *
 * <p>다리마다 따로 반올림하면 38% + 63% = 101%처럼 보인다 — 실제로 그렇게 나왔다.
 * 내림으로 자리를 잡고 남은 몫을 <b>나머지가 큰 다리부터</b> 한 씩 준다. SOR이 주문
 * 단수를 나눌 때 쓰는 최대 나머지 방식(Hare quota)과 같은 규칙이다.
 */
function sharePercents(legs: LegView[], total: number): number[] {
  const base = legs.reduce((sum, l) => sum + l.sent, 0) || Math.max(1, total);
  const exact = legs.map((l) => (l.sent * 100) / base);
  const out = exact.map((v) => Math.floor(v));
  let left = 100 - out.reduce((sum, v) => sum + v, 0);
  const order = exact
    .map((v, i) => ({ i, rem: v - Math.floor(v) }))
    .sort((a, b) => b.rem - a.rem);
  for (let k = 0; left > 0 && k < order.length; k++, left--) {
    out[order[k].i] += 1;
  }
  return out;
}

/**
 * 시장 하나로 나간 몫과 그 결과.
 *
 * <p>막대는 <b>이 주문 전체 수량</b>을 100으로 잡는다. 다리마다 자기 수량을 100으로 잡으면
 * 30주 다리와 70주 다리가 같은 길이로 보여 "나눴다"는 사실이 막대에서 사라진다.
 */
function DissectLeg({
  leg,
  total,
  pct,
  tone,
}: {
  leg: LegView;
  total: number;
  pct: number;
  tone: string;
}) {
  const m = marketName(leg.market);
  const base = Math.max(1, total);
  const working = leg.sent - leg.filled - leg.canceled;
  const share = `이 주문 ${fq(total)}주 가운데 ${fq(leg.sent)}주가 ${m}으로 나갔고 ${fq(leg.filled)}주가 체결됐다`;

  return (
    <div className="dleg">
      <div className="dline">
        <span className={`tag ${m.toLowerCase()}`}>{m}</span>
        <b className="num">{fq(leg.sent)}주 주문</b>
        <span className="grow" />
        <span className="dmuted num">{pct}%</span>
      </div>
      <div className="meter" role="img" aria-label={share} title={share}>
        <span style={{ width: `${(leg.filled / base) * 100}%`, background: `var(--${tone})` }} />
        <span style={{ width: `${(leg.canceled / base) * 100}%`, background: "var(--line-strong)" }} />
        <span style={{ width: `${(working / base) * 100}%`, background: "var(--line)" }} />
      </div>
      <dl className="dleg-nums">
        <div>
          <dt>체결</dt>
          <dd className="num">{fq(leg.filled)}주</dd>
        </div>
        <div>
          <dt>미체결</dt>
          <dd className="num">{fq(working)}주</dd>
        </div>
        <div>
          <dt>취소</dt>
          <dd className="num">{fq(leg.canceled)}주</dd>
        </div>
        <div>
          <dt>체결금액</dt>
          <dd className="num">{leg.filled > 0 ? money(leg.notional) : "—"}</dd>
        </div>
        <div>
          <dt>평균 단가</dt>
          <dd className="num">{leg.filled > 0 ? moneyUnit(leg.avgPrice) : "—"}</dd>
        </div>
      </dl>
    </div>
  );
}
