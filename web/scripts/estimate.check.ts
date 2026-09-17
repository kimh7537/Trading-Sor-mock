/**
 * 화면 계산의 자체 점검(T7-05). 웹에는 시험 도구가 없어 Node로 바로 돌린다:
 *   npm run check
 * 틀리면 assert가 던지고 종료 코드가 0이 아니다.
 */
import assert from "node:assert/strict";
import { estimate } from "../src/lib/estimate.ts";
import { isValidTick, stepPrice, tickSize } from "../src/lib/wire.ts";

const lv = (price: number, qty: number) => ({ price, qty });
const books = {
  KRX: { market: "KRX" as const, bids: [lv(70000, 8), lv(69900, 5)], asks: [lv(70100, 10), lv(70200, 5)] },
  NXT: { market: "NXT" as const, bids: [lv(70000, 2), lv(69800, 9)], asks: [lv(70000, 3), lv(70100, 4)] },
};

// SOR 매수: NXT 70,000 3주 → 70,100은 KRX 먼저 10주 → NXT 2주
let e = estimate(books, ["KRX", "NXT"], true, 70100, 15, false);
assert.deepEqual(e, { fill: 15, notional: 3 * 70000 + 12 * 70100, avg: 70080, rest: 0, byMarket: { KRX: 10, NXT: 5 } });

// 한 시장만: 지정가 안쪽 호가가 모자라면 나머지
e = estimate(books, ["KRX"], true, 70100, 15, false);
assert.equal(e.fill, 10);
assert.equal(e.rest, 5);

// FOK: 전량이 안 되면 0
e = estimate(books, ["KRX"], true, 70100, 15, true);
assert.equal(e.fill, 0);
assert.equal(e.rest, 15);

// 매도는 높은 매수호가부터, 지정가 아래로는 안 간다
e = estimate(books, ["KRX", "NXT"], false, 69900, 20, false);
assert.deepEqual(e.byMarket, { KRX: 13, NXT: 2 });
assert.equal(e.rest, 5);

// 호가 단위 — C core/src/tick_size.c와 같은 표
assert.equal(tickSize(1999), 1);
assert.equal(tickSize(2000), 5);
assert.equal(tickSize(70000), 100);
assert.equal(isValidTick(70050), false);
assert.equal(stepPrice(70000, 1), 70100);
assert.equal(stepPrice(50000, -1), 49950); // 구간 경계에서는 아래 구간 단위
assert.equal(stepPrice(4995, 1), 5000);
assert.equal(stepPrice(70050, 1), 70100); // 단위에 안 맞으면 먼저 맞춘다
assert.equal(stepPrice(70050, -1), 70000);

console.log("estimate.check: 통과");
