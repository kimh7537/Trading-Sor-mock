import type { Toast, Tone } from "../lib/useToasts";

/** 모양으로도 구별한다 — 색만으로 뜻을 전하지 않는다 */
const ICON: Record<Tone, string> = { ok: "✓", info: "i", warn: "!", error: "✕" };

/** 주문 결과·체결·취소 알림. 화면 읽기 프로그램에도 읽힌다(aria-live) */
export function Toasts({ toasts, onDismiss }: { toasts: Toast[]; onDismiss: (id: number) => void }) {
  return (
    <ol className="toasts" role="status" aria-live="polite" aria-label="알림">
      {toasts.map((t) => (
        <li key={t.id} className={`toast ${t.tone}`}>
          <span className="icon" aria-hidden="true">
            {ICON[t.tone]}
          </span>
          <div>
            <b>{t.title}</b>
            {t.body && <p>{t.body}</p>}
          </div>
          <button className="close" onClick={() => onDismiss(t.id)} aria-label="알림 닫기">
            ×
          </button>
        </li>
      ))}
    </ol>
  );
}
