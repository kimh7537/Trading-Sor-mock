import { useState } from "react";
import { login, signup, type Me } from "../lib/api";

/**
 * 로그인·회원가입 한 칸(T9-05).
 *
 * 사람마다 계좌가 하나씩 생기고, 주문·잔고·체결은 그 계좌 것만 보인다.
 * **계좌번호는 고르지 않는다** — 가입하면 채널계가 발급한다.
 */
export function LoginPanel({ onDone }: { onDone: (me: Me) => void }) {
  const [mode, setMode] = useState<"login" | "signup">("login");
  const [id, setId] = useState("");
  const [password, setPassword] = useState("");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const submit = async (e: React.FormEvent) => {
    e.preventDefault();
    setBusy(true);
    setError(null);
    try {
      onDone(await (mode === "login" ? login : signup)(id.trim(), password));
    } catch (err) {
      setError(err instanceof Error ? err.message : "요청이 거절됐다");
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="login-wrap">
      <form className="login" onSubmit={submit}>
        <div className="brand">
          <strong>mock-sor</strong>
          <span>복수시장 주문 집행 모의투자</span>
        </div>

        <div className="tabs">
          {(["login", "signup"] as const).map((m) => (
            <button
              key={m}
              type="button"
              className={mode === m ? "on" : ""}
              onClick={() => {
                setMode(m);
                setError(null);
              }}
            >
              {m === "login" ? "로그인" : "회원가입"}
            </button>
          ))}
        </div>

        <label>
          <span>아이디</span>
          <input
            value={id}
            onChange={(e) => setId(e.target.value)}
            autoComplete="username"
            placeholder="영문·숫자 3~20자"
            required
          />
        </label>

        <label>
          <span>비밀번호</span>
          <input
            type="password"
            value={password}
            onChange={(e) => setPassword(e.target.value)}
            autoComplete={mode === "login" ? "current-password" : "new-password"}
            placeholder="8자 이상"
            required
          />
        </label>

        {error && <p className="error">{error}</p>}

        <button type="submit" className="primary" disabled={busy || !id || !password}>
          {busy ? "보내는 중…" : mode === "login" ? "로그인" : "가입하고 시작"}
        </button>

        <p className="note">
          가입하면 모의 계좌가 하나 열린다. 시세는 실제지만 <b>주문은 이 원장 안에서만</b>{" "}
          체결된다 — 바깥으로 나가지 않는다.
        </p>
      </form>
    </div>
  );
}
