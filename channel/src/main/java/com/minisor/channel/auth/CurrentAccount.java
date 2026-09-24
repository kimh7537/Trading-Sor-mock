package com.minisor.channel.auth;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpSession;

/**
 * 지금 요청을 낸 사람의 계좌번호(T9-04).
 *
 * <p><b>계좌번호는 요청 본문에서 받지 않는다.</b> 예전에는 화면이 주문에 계좌번호를
 * 적어 보냈고 채널계가 그대로 믿었다. 사용자가 여럿이 되는 순간 그것은 "아무 계좌번호나
 * 적으면 남의 계좌로 주문이 나간다"는 뜻이 된다. 계좌는 <b>세션에서만</b> 나온다.
 */
public final class CurrentAccount {

    private CurrentAccount() {}

    /** 로그인하지 않았으면 null. */
    public static String of(HttpServletRequest request) {
        HttpSession session = request.getSession(false);
        return session == null
                ? null
                : (String) session.getAttribute(AuthController.SESSION_ACCOUNT);
    }

    /** 로그인하지 않았으면 예외. 거래 경로는 이쪽을 쓴다. */
    public static String required(HttpServletRequest request) {
        String account = of(request);
        if (account == null) {
            throw new NotLoggedIn();
        }
        return account;
    }

    /** 401로 옮겨지는 표시. {@link AuthInterceptor}가 먼저 막지만 이중으로 둔다. */
    public static final class NotLoggedIn extends RuntimeException {
        public NotLoggedIn() {
            super("로그인이 필요하다");
        }
    }
}
