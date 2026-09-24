package com.minisor.channel.auth;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.InterceptorRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;

/**
 * 로그인 검사를 걸 경로(T9-04).
 *
 * <p><b>막는 곳을 여기 한 군데에 적는다.</b> 계좌를 만지는 경로만 막고 시장을 보는
 * 경로는 열어 둔다 — 로그인하지 않아도 호가창과 차트는 보여야 한다.
 */
@Configuration
public class WebConfig implements WebMvcConfigurer {

    private final AuthInterceptor auth;

    public WebConfig(AuthInterceptor auth) {
        this.auth = auth;
    }

    @Override
    public void addInterceptors(InterceptorRegistry registry) {
        registry.addInterceptor(auth)
                .addPathPatterns("/api/orders/**", "/api/balance", "/api/portfolio",
                        "/api/history");
    }
}
