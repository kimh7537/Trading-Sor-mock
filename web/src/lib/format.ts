export const won = (n: number) => n.toLocaleString("ko-KR");

export const qty = (n: number) => n.toLocaleString("ko-KR");

export const bp = (n: number) => `${n >= 0 ? "+" : ""}${n}bp`;

export const pct = (n: number) => `${(n * 100).toFixed(2)}%`;

export const time = (d = new Date()) =>
  d.toLocaleTimeString("ko-KR", { hour12: false });
