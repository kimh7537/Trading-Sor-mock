package com.minisor.channel.api;

import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.stream.StreamEvent;
import com.minisor.channel.stream.StreamHub;
import com.minisor.channel.wire.BalanceAck;
import com.minisor.channel.wire.DetailAck;
import com.minisor.channel.wire.DetailReq;
import java.util.HashMap;
import java.util.Map;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

/**
 * 원장을 주기적으로 읽어 <b>바뀐 것만</b> 화면에 밀어 보낸다(T7-03).
 *
 * <h2>왜 필요한가</h2>
 *
 * 원장은 요청-응답만 한다. 그래서 채널계가 가만히 있으면 화면은 이런 것을 모른다.
 * <ul>
 *   <li>다른 주문 때문에 바뀐 <b>호가</b>
 *   <li>예전에 걸어 둔 주문이 <b>나중에 체결</b>된 것 — 그에 따른 <b>잔고</b> 변화
 * </ul>
 * T4-05의 완료 조건("체결 통보·호가 갱신을 화면에 밀어 보낸다")을 원장에 구독 접속을 새로 만들지 않고
 * 지키는 방법이다.
 *
 * <h2>무엇을 보내나</h2>
 * <ul>
 *   <li>{@code book} — 시장별 호가 10단이 바뀌었을 때
 *   <li>{@code balance} — 예수금·묶인 금액이 바뀌었을 때
 *   <li>{@code order-update} — 끝나지 않은 주문의 상태가 바뀌었을 때
 *   <li>{@code fill} — 그 변화 중 체결이 늘어난 몫. 시장별로, 가격은 <b>체결 금액 차이 / 수량 차이</b>
 * </ul>
 *
 * <p>ponytail: 1초마다 읽는다. 원장 접속이 1개라 주문과 같은 줄에 서지만, 원장의 처리는 마이크로초라
 * 주문을 눈에 띄게 늦추지 않는다. 끝나지 않은 주문이 수백 개로 늘면 한 번에 읽는 수를 나눠야 한다.
 */
@Component
public class LedgerPoller {

    private final LedgerGateway gateway;
    private final OrderRegistry registry;
    private final StreamHub hub;
    private final String account;
    private final String symbol;
    private final boolean enabled;

    private final Map<Integer, BookController.BookDto> lastBooks = new HashMap<>();
    private BalanceController.BalanceDto lastBalance;

    public LedgerPoller(
            LedgerGateway gateway,
            OrderRegistry registry,
            StreamHub hub,
            @Value("${minisor.account}") String account,
            @Value("${minisor.symbol:005930}") String symbol,
            @Value("${minisor.poller.enabled:true}") boolean enabled) {
        this.gateway = gateway;
        this.registry = registry;
        this.hub = hub;
        this.account = account;
        this.symbol = symbol;
        this.enabled = enabled;
    }

    @Scheduled(
            fixedDelayString = "${minisor.poller.interval-ms:1000}",
            initialDelayString = "${minisor.poller.interval-ms:1000}")
    void scheduled() {
        if (enabled) {
            tick();
        }
    }

    /** 한 바퀴. 원장이 답하지 않으면 이번 바퀴는 그만둔다 — 끊김 알림은 게이트웨이가 했다. */
    public synchronized void tick() {
        try {
            books();
            balance();
            orders();
        } catch (LedgerException e) {
            // 다음 바퀴에 다시 읽는다
        }
    }

    private void books() {
        for (int market = 0; market <= 1; market++) {
            BookController.BookDto now =
                    BookController.BookDto.from(BookController.fetch(gateway, symbol, market));
            if (!now.equals(lastBooks.get(market))) {
                lastBooks.put(market, now);
                hub.broadcast(StreamEvent.book(now));
            }
        }
    }

    private void balance() {
        BalanceAck ack = BalanceController.fetch(gateway, account);
        if (ack.reason != 0) {
            return;
        }
        BalanceController.BalanceDto now = BalanceController.BalanceDto.from(ack);
        if (!now.equals(lastBalance)) {
            lastBalance = now;
            hub.broadcast(StreamEvent.balance(now));
        }
    }

    private void orders() {
        for (OrderView before : registry.open()) {
            DetailReq req = new DetailReq();
            req.account = account;
            req.orderId = before.orderId();
            DetailAck d = gateway.call(req, DetailAck.class);
            if (d.reason != 0) {
                continue;
            }
            OrderView after = OrderView.from(d, before.type());
            if (after.equals(before)) {
                continue;
            }
            registry.put(after);
            hub.broadcast(StreamEvent.orderUpdate(after));
            broadcastNewFills(before, after);
        }
    }

    private void broadcastNewFills(OrderView before, OrderView after) {
        for (OrderView.LegView leg : after.legs()) {
            OrderView.LegView prev =
                    before.legs().stream()
                            .filter(l -> l.market() == leg.market())
                            .findFirst()
                            .orElse(new OrderView.LegView(leg.market(), 0, 0, 0, 0, 0));
            int qty = leg.filled() - prev.filled();
            if (qty <= 0) {
                continue;
            }
            int price = (int) ((leg.notional() - prev.notional()) / qty);
            hub.broadcast(
                    StreamEvent.fill(
                            Map.of(
                                    "clOrdId", after.clOrdId(),
                                    "orderId", after.orderId(),
                                    "side", after.side(),
                                    "market", leg.market(),
                                    "price", price,
                                    "qty", qty)));
        }
    }
}
