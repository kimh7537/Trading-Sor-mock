import { useEffect, useState } from "react";

export type Change = "up" | "down";

/** 깜빡임을 유지하는 시간. CSS `.flash-up`의 애니메이션 길이와 맞춘다 */
const HOLD_MS = 900;

type Marks = { map: ReadonlyMap<string, { dir: Change; seq: number }>; seq: number };

const parse = (signature: string) => {
  const m = new Map<string, number>();
  for (const part of signature ? signature.split("|") : []) {
    const i = part.lastIndexOf("=");
    m.set(part.slice(0, i), Number(part.slice(i + 1)));
  }
  return m;
};

/**
 * 지난번과 값이 달라진 항목을 알려 준다(T7-05) — 잔량·체결이 바뀐 줄을 눈에 띄게 하려고.
 *
 * 비어 있던 첫 값에서는 아무것도 표시하지 않는다(처음 뜰 때 화면 전체가 깜빡이면 뜻이 없다).
 * 표시는 잠깐 뒤 스스로 지운다. 같은 항목이 다시 바뀌면 애니메이션을 다시 틀도록
 * 부르는 쪽이 값을 React key에 넣는다.
 */
export function useFlash(entries: [string, number][]): (key: string) => Change | undefined {
  // 배열은 그릴 때마다 새로 만들어지므로 내용으로 비교한다. 키에는 "|"와 "="가 없다
  const signature = entries.map(([k, v]) => `${k}=${v}`).join("|");
  const [seen, setSeen] = useState(signature);
  const [marks, setMarks] = useState<Marks>({ map: new Map(), seq: 0 });

  // 입력이 바뀐 그리기에서 바로 상태를 고친다(React 문서의 "이전 값 기억" 방식)
  if (signature !== seen) {
    setSeen(signature);
    if (seen !== "") {
      const before = parse(seen);
      const seq = marks.seq + 1;
      const map = new Map(marks.map);
      let changed = false;
      for (const [k, v] of parse(signature)) {
        const p = before.get(k);
        if (p !== v) {
          map.set(k, { dir: p === undefined || v > p ? "up" : "down", seq });
          changed = true;
        }
      }
      if (changed) setMarks({ map, seq });
    }
  }

  useEffect(() => {
    if (marks.seq === 0) return;
    const n = marks.seq;
    // 정리 함수로 취소하지 않는다 — 곧바로 다음 변화가 와도 이번 표시는 제 시간만큼 남아야 한다
    window.setTimeout(() => {
      setMarks((m) => ({ ...m, map: new Map([...m.map].filter(([, x]) => x.seq > n)) }));
    }, HOLD_MS);
  }, [marks.seq]);

  return (k) => marks.map.get(k)?.dir;
}
