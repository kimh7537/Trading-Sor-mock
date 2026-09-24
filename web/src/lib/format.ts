/**
 * 지금 화면이 다루는 통화 (T10-03).
 *
 * **가격의 뜻이 통화마다 다르다** — 국내는 원 정수, 미국은 **센트** 정수다.
 * 원장이 소수점을 담을 수 없어 미국 가격을 센트로 세기 때문이고, 100으로 나눠
 * 달러로 보여 주는 것은 화면의 일이다.
 *
 * 모듈 변수를 쓰는 이유: 화면이 한 번에 한 종목만 다루고, `money()`가 9개 파일
 * 40여 곳에서 불린다. 통화를 인자로 넘기면 그 전부가 통화를 들고 다녀야 한다.
 * (엔진 쪽 전역 금지는 결정성 때문인데, 여기는 표기 계층이다.)
 */
let currency: "KRW" | "USD" = "KRW";

export function setCurrency(c: "KRW" | "USD") {
  currency = c;
}

export const currentCurrency = () => currency;

/** 통화 기호를 붙이지 않은 숫자. 미국이면 센트를 달러로 바꿔 소수 둘째 자리까지. */
export const money = (n: number) =>
  currency === "USD"
    ? (n / 100).toLocaleString("en-US", {
        minimumFractionDigits: 2,
        maximumFractionDigits: 2,
      })
    : n.toLocaleString("ko-KR");

/** 단위까지 붙인 것. 표가 아니라 문장 안에서 쓴다. */
export const moneyUnit = (n: number) =>
  currency === "USD" ? `$${money(n)}` : `${money(n)}원`;

export const qty = (n: number) => n.toLocaleString("ko-KR");

export const bp = (n: number) => `${n >= 0 ? "+" : ""}${n}bp`;

export const pct = (n: number) => `${(n * 100).toFixed(2)}%`;

/** 13:11:28 — ko-KR 형식("13시 11분 28초")은 표에서 폭이 넓고 흔들린다 */
export const time = (d = new Date()) =>
  d.toLocaleTimeString("en-GB", { hour12: false });
