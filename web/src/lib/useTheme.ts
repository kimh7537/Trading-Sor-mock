import { useCallback, useEffect, useState } from "react";

export type Theme = "dark" | "light";

const KEY = "mock-sor.theme";

/**
 * 라이트/다크. **고른 적이 없으면 운영체제 설정을 따른다.**
 *
 * 값은 `<html data-theme>`에 실린다 — CSS 변수만 바뀌고 화면 코드는 테마를 모른다.
 * 고른 값은 브라우저에 남겨 다음에도 그대로 연다.
 */
export function useTheme(): [Theme, () => void] {
  const [theme, setTheme] = useState<Theme>(() => {
    try {
      const saved = localStorage.getItem(KEY);
      if (saved === "dark" || saved === "light") return saved;
    } catch {
      // 사생활 보호 모드 등에서 막힐 수 있다. 그때는 운영체제 설정을 쓴다
    }
    return window.matchMedia?.("(prefers-color-scheme: light)").matches ? "light" : "dark";
  });

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    try {
      localStorage.setItem(KEY, theme);
    } catch {
      // 남기지 못해도 이번 세션은 정상 동작한다
    }
  }, [theme]);

  const toggle = useCallback(() => setTheme((t) => (t === "dark" ? "light" : "dark")), []);
  return [theme, toggle];
}
