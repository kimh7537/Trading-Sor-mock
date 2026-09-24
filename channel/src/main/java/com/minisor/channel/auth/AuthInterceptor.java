package com.minisor.channel.auth;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import java.io.IOException;
import org.springframework.stereotype.Component;
import org.springframework.web.servlet.HandlerInterceptor;

/**
 * 로그인하지 않은 요청을 거래 경로에서 막는다(T9-04).
 *
 * <p><b>컨트롤러마다 검사하지 않고 한 자리에서 막는다.</b> 경로가 늘 때 검사를 빠뜨리면
 * 그 경로만 조용히 열린다 — 빠뜨린 것은 테스트에도 잘 안 걸린다.
 *
 * <p>호가·차트·종목 검색은 막지 않는다. 로그인 전에도 시장을 볼 수 있어야 하고,
 * 그 경로들은 계좌를 만지지 않는다.
 */
@Component
public class AuthInterceptor implements HandlerInterceptor {

    @Override
    public boolean preHandle(HttpServletRequest request, HttpServletResponse response,
            Object handler) throws IOException {
        if ("OPTIONS".equalsIgnoreCase(request.getMethod())) {
            return true; /* 사전 요청(CORS)은 세션을 들고 오지 않는다 */
        }
        if (CurrentAccount.of(request) != null) {
            return true;
        }
        response.setStatus(HttpServletResponse.SC_UNAUTHORIZED);
        response.setContentType("application/json;charset=UTF-8");
        response.getWriter().write("{\"error\":\"로그인이 필요하다\"}");
        return false;
    }
}
