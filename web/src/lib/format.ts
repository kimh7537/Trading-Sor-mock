export const won = (n: number) => n.toLocaleString("ko-KR");

export const qty = (n: number) => n.toLocaleString("ko-KR");

export const bp = (n: number) => `${n >= 0 ? "+" : ""}${n}bp`;

export const pct = (n: number) => `${(n * 100).toFixed(2)}%`;

/** 13:11:28 — ko-KR 형식("13시 11분 28초")은 표에서 폭이 넓고 흔들린다 */
export const time = (d = new Date()) =>
  d.toLocaleTimeString("en-GB", { hour12: false });
