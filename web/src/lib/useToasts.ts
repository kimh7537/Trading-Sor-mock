import { useCallback, useRef, useState } from "react";

export type Tone = "ok" | "info" | "warn" | "error";

export interface Toast {
  id: number;
  tone: Tone;
  title: string;
  body?: string;
}

/** 알림이 떠 있는 시간 */
const LIFE_MS = 5000;
/** 한 번에 보이는 알림 수. 넘치면 오래된 것부터 내린다 */
const MAX_SHOWN = 4;

export type Notify = (tone: Tone, title: string, body?: string) => void;

export function useToasts() {
  const [toasts, setToasts] = useState<Toast[]>([]);
  const seq = useRef(0);

  const dismiss = useCallback((id: number) => {
    setToasts((ts) => ts.filter((t) => t.id !== id));
  }, []);

  const notify = useCallback<Notify>(
    (tone, title, body) => {
      const id = ++seq.current;
      setToasts((ts) => [...ts, { id, tone, title, body }].slice(-MAX_SHOWN));
      window.setTimeout(() => dismiss(id), LIFE_MS);
    },
    [dismiss],
  );

  return { toasts, notify, dismiss };
}
