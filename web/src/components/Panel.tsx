import type { ReactNode } from "react";

export function Panel({
  title,
  right,
  children,
  pad = true,
}: {
  title?: string;
  right?: ReactNode;
  children: ReactNode;
  pad?: boolean;
}) {
  return (
    <section
      style={{
        background: "var(--bg-panel)",
        border: "1px solid var(--line)",
        borderRadius: "var(--r-md)",
        display: "flex",
        flexDirection: "column",
        minHeight: 0,
        overflow: "hidden",
      }}
    >
      {title && (
        <header
          style={{
            display: "flex",
            alignItems: "center",
            justifyContent: "space-between",
            gap: "var(--s-3)",
            padding: "10px var(--s-4)",
            borderBottom: "1px solid var(--line-soft)",
            fontSize: 12,
            fontWeight: 600,
            letterSpacing: "0.04em",
            color: "var(--text-dim)",
            textTransform: "uppercase",
            flex: "0 0 auto",
          }}
        >
          <span>{title}</span>
          {right}
        </header>
      )}
      <div
        style={{
          padding: pad ? "var(--s-4)" : 0,
          overflow: "auto",
          minHeight: 0,
          flex: 1,
        }}
      >
        {children}
      </div>
    </section>
  );
}
