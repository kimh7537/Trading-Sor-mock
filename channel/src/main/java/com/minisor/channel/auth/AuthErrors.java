package com.minisor.channel.auth;

import java.util.Map;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;

/**
 * 로그인하지 않은 요청을 401로 옮긴다 (T11-04).
 *
 * <p>{@link AuthInterceptor}가 경로로 먼저 막지만, <b>새 경로를 등록에 빠뜨리면
 * 500이 나간다.</b> 실제로 그렇게 됐다 — {@code /api/portfolio}를 만들고 인터셉터
 * 목록에 넣지 않아, 로그인 안 한 요청이 "서버 오류"로 보였다.
 *
 * <p>두 겹으로 두는 이유가 그것이다. 인터셉터는 원장까지 가기 전에 막아 주고,
 * 이쪽은 <b>빠뜨렸을 때도 맞는 상태 코드</b>를 보장한다.
 */
@RestControllerAdvice
public class AuthErrors {

    @ExceptionHandler(CurrentAccount.NotLoggedIn.class)
    public ResponseEntity<Map<String, String>> notLoggedIn(CurrentAccount.NotLoggedIn e) {
        return ResponseEntity.status(HttpStatus.UNAUTHORIZED)
                .body(Map.of("error", e.getMessage()));
    }
}
