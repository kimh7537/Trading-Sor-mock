package com.minisor.channel.auth;

import com.minisor.channel.api.LedgerGateway;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.wire.AccountAck;
import com.minisor.channel.wire.AccountOpen;
import jakarta.servlet.http.HttpSession;
import jakarta.validation.Valid;
import jakarta.validation.constraints.NotBlank;
import java.util.Map;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

/**
 * 가입·로그인·로그아웃(T9-03).
 *
 * <p><b>로그인은 원장에 계좌를 여는 일까지 한다.</b> 원장은 메모리에만 있어서 다시 뜨면
 * 계좌가 사라진다. 가입할 때만 열면 그다음부터는 "없는 계좌"가 되어 주문이 전부
 * 거절된다. 개설 전문이 다시 불러도 되게 만들어져 있으므로(T9-01) 로그인마다 보낸다.
 *
 * <p><b>원장에 못 붙어도 로그인은 시킨다.</b> 화면은 로그인 상태에서 호가창을 볼 수
 * 있어야 하고, 주문은 어차피 그때 다시 503을 받는다. 계좌를 못 연 것은 응답의
 * {@code ledgerReady}로 알린다.
 */
@RestController
@RequestMapping("/api/auth")
public class AuthController {

    private static final Logger log = LoggerFactory.getLogger(AuthController.class);

    /** 로그인한 사람의 계좌번호가 담기는 세션 키. 다른 계층은 이 값만 본다. */
    public static final String SESSION_ACCOUNT = "minisor.account";

    /** 로그인한 사람의 아이디. 화면에 보여 줄 때만 쓴다. */
    public static final String SESSION_USER = "minisor.user";

    private final UserStore users;
    private final LedgerGateway gateway;
    private final long signupCash;

    public AuthController(
            UserStore users,
            LedgerGateway gateway,
            @Value("${minisor.auth.signup-cash:100000000}") long signupCash) {
        this.users = users;
        this.gateway = gateway;
        this.signupCash = signupCash;
    }

    public record Credentials(@NotBlank String id, @NotBlank String password) {}

    /** 로그인한 사람에 대해 화면이 알아야 하는 것. 해시와 소금은 절대 내보내지 않는다. */
    public record Me(String id, String account, long cash, long reserved, boolean ledgerReady) {}

    @PostMapping("/signup")
    public ResponseEntity<?> signup(@Valid @RequestBody Credentials body, HttpSession session) {
        User u;
        try {
            u = users.signup(body.id(), body.password());
        } catch (UserStore.SignupException e) {
            return ResponseEntity.status(HttpStatus.CONFLICT)
                    .body(Map.of("error", e.getMessage()));
        }
        return ResponseEntity.ok(enter(u, session));
    }

    @PostMapping("/login")
    public ResponseEntity<?> login(@Valid @RequestBody Credentials body, HttpSession session) {
        return users.login(body.id(), body.password())
                .<ResponseEntity<?>>map(u -> ResponseEntity.ok(enter(u, session)))
                .orElseGet(() -> ResponseEntity.status(HttpStatus.UNAUTHORIZED)
                        .body(Map.of("error", "아이디나 비밀번호가 맞지 않는다")));
    }

    @PostMapping("/logout")
    public ResponseEntity<Void> logout(HttpSession session) {
        session.invalidate();
        return ResponseEntity.noContent().build();
    }

    /** 지금 누구로 들어와 있나. 로그인하지 않았으면 401 — 화면이 로그인 창을 띄운다. */
    @GetMapping("/me")
    public ResponseEntity<Me> me(HttpSession session) {
        String id = (String) session.getAttribute(SESSION_USER);
        String account = (String) session.getAttribute(SESSION_ACCOUNT);
        if (id == null || account == null) {
            return ResponseEntity.status(HttpStatus.UNAUTHORIZED).build();
        }
        return ResponseEntity.ok(describe(id, account, openAccount(account)));
    }

    /** 세션에 자리를 잡고 원장 계좌를 연다. */
    private Me enter(User u, HttpSession session) {
        session.setAttribute(SESSION_USER, u.id());
        session.setAttribute(SESSION_ACCOUNT, u.account());
        return describe(u.id(), u.account(), openAccount(u.account()));
    }

    private Me describe(String id, String account, AccountAck ack) {
        return new Me(id, account,
                ack == null ? 0 : ack.cash,
                ack == null ? 0 : ack.reserved,
                ack != null);
    }

    /** 원장에 계좌를 연다(이미 있으면 잔고만 받는다). 못 붙으면 null. */
    private AccountAck openAccount(String account) {
        AccountOpen req = new AccountOpen();
        req.account = account;
        req.cash = signupCash;
        try {
            AccountAck ack = gateway.call(req, AccountAck.class);
            if (ack.code != 0) {
                log.warn("계좌 {} 를 열지 못했다: code={}", account, ack.code);
                return null;
            }
            return ack;
        } catch (LedgerException e) {
            log.warn("계좌 {} 개설 중 원장에 못 붙었다: {}", account, e.getMessage());
            return null;
        }
    }
}
