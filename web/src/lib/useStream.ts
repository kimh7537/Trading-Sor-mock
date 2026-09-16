import { useEffect, useRef, useState } from "react";

const WS_URL = import.meta.env.VITE_WS_URL ?? "ws://localhost:8080/ws/stream";

export type ConnState = "connecting" | "open" | "closed";

export interface StreamEvent {
  kind: string;
  payload: unknown;
}

/**
 * 채널계 구독. 끊기면 물러서며 다시 붙고, 그 상태를 돌려준다.
 * 화면이 조용히 멈추면 사용자는 "시장이 조용한 것"과 구분하지 못한다.
 */
export function useStream(onEvent: (e: StreamEvent) => void) {
  const [state, setState] = useState<ConnState>("connecting");
  const [attempt, setAttempt] = useState(0);
  const cb = useRef(onEvent);
  cb.current = onEvent;

  useEffect(() => {
    let ws: WebSocket | null = null;
    let timer: number | undefined;
    let closed = false;
    let backoff = 500;

    const connect = () => {
      setState("connecting");
      ws = new WebSocket(WS_URL);

      ws.onopen = () => {
        backoff = 500;
        setState("open");
      };
      ws.onmessage = (m) => {
        try {
          cb.current(JSON.parse(m.data) as StreamEvent);
        } catch {
          // 읽을 수 없는 전문은 버린다
        }
      };
      ws.onclose = () => {
        if (closed) return;
        setState("closed");
        setAttempt((n) => n + 1);
        timer = window.setTimeout(connect, backoff);
        backoff = Math.min(backoff * 2, 10_000); // 상한을 둔다
      };
      ws.onerror = () => ws?.close();
    };

    connect();
    return () => {
      closed = true;
      if (timer) window.clearTimeout(timer);
      ws?.close();
    };
  }, []);

  return { state, attempt };
}
