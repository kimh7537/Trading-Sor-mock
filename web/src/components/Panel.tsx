import type { ReactNode } from "react";

/** 제목 줄 + 스크롤되는 본문. `head`를 주면 제목 대신 그것을 제목 줄에 놓는다(예: 탭) */
export function Panel({
  title,
  sub,
  head,
  children,
  flush = false,
  className = "",
}: {
  title?: string;
  sub?: ReactNode;
  head?: ReactNode;
  children: ReactNode;
  flush?: boolean;
  className?: string;
}) {
  return (
    <section className={`panel ${className}`} aria-label={title}>
      <header className="panel-head">
        {head ?? <h2>{title}</h2>}
        {sub && <span className="sub">{sub}</span>}
      </header>
      <div className={flush ? "panel-body flush" : "panel-body"}>{children}</div>
    </section>
  );
}
