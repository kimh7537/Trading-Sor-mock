package com.minisor.channel.store;

import com.minisor.channel.api.LedgerGateway;
import com.minisor.channel.api.OrderRegistry;
import com.minisor.channel.api.OrderView;
import com.minisor.channel.auth.UserStore;
import com.minisor.channel.feed.LiveFeed;
import com.minisor.channel.feed.SymbolState;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.wire.AccountAck;
import com.minisor.channel.wire.AccountOpen;
import com.minisor.channel.wire.CancelAck;
import com.minisor.channel.wire.CancelReq;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

/**
 * 거래 기록을 되짚어 원장 계좌에 예수금·보유를 실어 준다 (점검).
 *
 * <p><b>왜 다시 실어야 하나.</b> 원장은 한 종목·한 장부만 들고 있다. 그런데 기록은
 * (종목 x 통화 x 시세 모드)로 나뉘어 있다. 그래서 <b>다음 셋 중 하나라도 바뀌면</b>
 * 원장이 들고 있는 값이 틀린 장부의 것이 된다.
 *
 * <ul>
 *   <li>로그인 — 원장이 다시 떠 계좌가 비었을 수 있다
 *   <li>시세 모드 전환 — 시뮬 장부와 실시세 장부는 예수금·보유가 다르다
 *   <li>종목 전환 — 원장이 새로 열리고 그 종목의 보유를 실어야 한다
 * </ul>
 *
 * <p>시세 모드와 종목은 <b>서버 전체가 하나</b>인데 계좌는 사람마다다. 그래서 그
 * 둘이 바뀌면 가입한 <b>모든 계좌</b>를 다시 싣는다({@link #seedAll()}).
 */
@Component
public class AccountSeeder {

    private static final Logger log = LoggerFactory.getLogger(AccountSeeder.class);

    private final Portfolio portfolio;
    private final UserStore users;
    private final SymbolState symbols;
    private final LiveFeed live;
    private final LedgerGateway gateway;
    private final OrderRegistry registry;
    private final long seedKr;
    private final long seedUs;

    public AccountSeeder(
            Portfolio portfolio,
            UserStore users,
            SymbolState symbols,
            /*
             * 늦게 받는다 — 순환이다. 장부가 바뀔 때 부르는 쪽이 LiveFeed이고,
             * 이쪽은 "지금 어느 장부인가"를 알아야 한다. 한쪽을 늦추면 끊긴다.
             */
            @org.springframework.context.annotation.Lazy LiveFeed live,
            LedgerGateway gateway,
            OrderRegistry registry,
            @Value("${minisor.auth.signup-cash:100000000}") long seedKr,
            @Value("${minisor.auth.signup-cash-us:10000000}") long seedUs) {
        this.portfolio = portfolio;
        this.users = users;
        this.symbols = symbols;
        this.live = live;
        this.gateway = gateway;
        this.registry = registry;
        this.seedKr = seedKr;
        this.seedUs = seedUs;
    }

    /** 이 통화에서 계좌를 열 때 넣어 주는 돈. */
    public long seedCash() {
        return symbols.us() ? seedUs : seedKr;
    }

    /**
     * 계좌 하나를 지금 장부(종목·통화·시세 모드)의 값으로 싣는다.
     *
     * @return 원장이 답한 계좌 상태. 못 붙으면 null
     */
    public AccountAck seed(String account) {
        cancelOpenOrders(account);

        SymbolState.Current now = symbols.current();
        Portfolio.Snapshot s =
                portfolio.of(account, now.code(), now.kind(), live.book(), seedCash());

        AccountOpen req = new AccountOpen();
        req.account = account;
        req.cash = s.cash();
        req.posQty = s.qty();
        req.posCost = s.cost();
        try {
            AccountAck ack = gateway.call(req, AccountAck.class);
            if (ack.code != 0) {
                log.warn("계좌 {} 를 싣지 못했다: code={}", account, ack.code);
                return null;
            }
            return ack;
        } catch (LedgerException e) {
            log.warn("계좌 {} 를 싣는 중 원장에 못 붙었다: {}", account, e.getMessage());
            return null;
        }
    }

    /**
     * 그 계좌의 살아 있는 주문을 전부 취소한다.
     *
     * <p><b>미체결은 장부를 넘어가지 않는다.</b> 시세 모드나 종목이 바뀌면 그 주문이
     * 걸려 있던 시장은 더 이상 같은 시장이 아니다 — 시뮬에서 건 주문이 실시세 호가에
     * 체결되면 지어낸 판단이 실제 성적으로 넘어온다. 서버가 꺼지면 미체결이 사라지는
     * 것과 같은 규칙이다.
     *
     * <p>예수금을 덮어쓰기 전에 해야 한다 — 묶인 돈이 남아 있으면 원장이
     * {@code reserved <= cash}를 지키려고 덮어쓰기를 거절한다.
     */
    private void cancelOpenOrders(String account) {
        for (OrderView v : registry.open(account)) {
            CancelReq req = new CancelReq();
            req.account = account;
            req.orderId = v.orderId();
            req.clOrdId = v.clOrdId();
            try {
                gateway.call(req, CancelAck.class);
            } catch (LedgerException e) {
                /* 종목이 바뀌면 원장이 새로 열려 그 주문 자체가 없다 — 그것도 정리된 것이다 */
                log.debug("미체결 {} 를 정리하지 못했다: {}", req.orderId, e.getMessage());
            }
        }
    }

    /**
     * 가입한 모든 계좌를 다시 싣는다.
     *
     * <p>시세 모드나 종목이 바뀐 뒤에 부른다 — 그 둘은 서버 전체가 하나라
     * <b>지금 접속하지 않은 사람의 계좌도</b> 틀린 장부를 들고 있게 된다.
     */
    public void seedAll() {
        int ok = 0;
        for (String account : users.accounts()) {
            if (seed(account) != null) {
                ok++;
            }
        }
        SymbolState.Current now = symbols.current();
        log.info("계좌 {}개를 {} 장부로 다시 실었다 ({} {})", ok,
                live.book() == 1 ? "실시세" : "시뮬", now.code(), now.currency());
    }
}
