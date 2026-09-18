import type { FeedStatus } from "../lib/types";
import { marketName } from "../lib/wire";

/**
 * 시뮬 모드 ↔ 실시세 모드(T8-05).
 *
 * **지금 어느 모드인지 늘 보인다.** 그리고 실시세일 때는 "시세는 실제, 주문은 모의"를
 * 화면에 분명히 적는다 — 이 화면에서 낸 주문은 어느 모드에서도 내 원장에서 끝나고
 * 바깥으로 나가지 않는다. 그 사실을 숨기면 데모가 거짓말이 된다.
 *
 * 색만으로 뜻을 전하지 않는다. 글자와 `aria-pressed`로도 같은 것을 말한다.
 */
export function FeedMode({
  feed,
  onChange,
  busy,
}: {
  feed: FeedStatus | null;
  onChange: (mode: "sim" | "live") => void;
  busy: boolean;
}) {
  const live = feed?.mode === "live";
  const canLive = feed?.available ?? false;

  return (
    <div className="stack">
      <div className="mode-switch" role="group" aria-label="호가창을 움직이는 것">
        <button
          type="button"
          aria-pressed={!live}
          disabled={busy}
          onClick={() => onChange("sim")}
        >
          시뮬 (가상 참가자)
        </button>
        <button
          type="button"
          aria-pressed={live}
          disabled={busy || !canLive}
          title={canLive ? undefined : feed?.note}
          onClick={() => onChange("live")}
        >
          실시세 (바깥 시세)
        </button>
      </div>

      {live ? (
        <div className="alert-bar warn" role="status">
          <b>시세는 실제, 주문은 모의</b> — 호가는 바깥에서 받아 온 것이고,
          주문·체결·잔고는 전부 이 프로젝트의 원장 안에서만 일어난다. 바깥으로 주문이 나가지 않는다.
        </div>
      ) : (
        <p className="note">
          가상 참가자가 양 시장에 주문을 낸다. 두 시장·SOR 배분·전략 비교를 볼 수 있는 것은
          이 모드뿐이다.
        </p>
      )}

      <dl className="kv num">
        <div>
          <dt>지금</dt>
          <dd>{live ? `실시세 (${feed?.source})` : "시뮬"}</dd>
        </div>
        <div>
          <dt>심는 시장</dt>
          <dd>{live && feed ? `${marketName(feed.market)} (통합 시세)` : "KRX · NXT"}</dd>
        </div>
        <div>
          <dt>받은 스냅샷</dt>
          <dd>{feed?.applied ?? 0}건</dd>
        </div>
      </dl>

      {!canLive && (
        <p className="note">
          실시세로 바꿀 수 없다 — {feed?.note ?? "채널계 상태를 읽지 못했다"}.
        </p>
      )}

      {/*
        붙지 못한 이유를 그대로 보인다. 조용히 시뮬로 남아 있으면 사용자는 "켰는데 왜
        안 바뀌지"를 알 길이 없다. 실제로 허용 IP 미등록(403)으로 그렇게 됐다.
      */}
      {!live && feed?.error && (
        <div className="alert-bar danger" role="alert">
          <b>실시세에 붙지 못했다</b> — {feed.error}
        </div>
      )}
    </div>
  );
}
