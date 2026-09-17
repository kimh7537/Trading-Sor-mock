package com.minisor.channel.api;

import com.minisor.channel.ledger.LedgerConnection;
import com.minisor.channel.ledger.LedgerConnectionPool;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.stream.StreamEvent;
import com.minisor.channel.stream.StreamHub;
import com.minisor.channel.wire.CancelAck;
import com.minisor.channel.wire.CancelReq;
import com.minisor.channel.wire.DetailAck;
import com.minisor.channel.wire.DetailReq;
import com.minisor.channel.wire.OrderAck;
import com.minisor.channel.wire.OrderReq;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

/**
 * 주문을 원장으로 보내고, 취소하고, 상태를 읽는다.
 *
 * <h2>답을 못 받은 주문을 단정하지 않는다</h2>
 *
 * 원장에 보냈는데 응답이 없으면 <b>처리됐는지 아닌지 알 수 없다.</b>
 * 성공으로 답하면 없는 주문을 있다고 하는 것이고, 실패로 답하면 있는 주문을
 * 없다고 하는 것이다. 둘 다 틀릴 수 있으므로 <b>모른다고 답한다</b>
 * ({@link OrderResponseDto.Outcome#IN_DOUBT}).
 *
 * <p>그리고 <b>자동으로 다시 보내지 않는다.</b> 재전송은 중복 주문을 만들 수
 * 있고, 그것이 이 계층이 저지를 수 있는 가장 비싼 실수다 — T3-14가 FEP
 * 쪽에서 내린 것과 같은 판단이다. 취소는 다르다 — 두 번 보내도 두 번째는 "잔량 없음"일 뿐이라
 * 안전하다.
 *
 * <h2>결과를 방송한다 (T6-04, T7-03)</h2>
 *
 * 원장의 답을 받으면 <b>주문 결과</b>를, 체결이 있었으면 시장별로 <b>체결</b>을 WebSocket 구독자
 * 모두에게 보낸다. 접수된 주문은 원장에서 상세를 곧바로 읽어 {@link OrderRegistry}에 적는다 —
 * 체결이 어느 시장에서 났는지는 상세에만 있다. 예전에 걸어 둔 주문이 <b>나중에</b> 체결되는 것은
 * {@link LedgerPoller}가 원장을 다시 읽어 알린다.
 *
 * <p>답을 못 받은 주문(IN_DOUBT)은 주문번호를 모르므로 목록에 넣지 못한다. "조회로 확인해야 한다"는
 * 원칙이지만, 주문을 낸 쪽 번호(cl_ord_id)로 찾는 조회가 전문에 먼저 생겨야 한다.
 */
@Service
public class OrderService {

    /** C {@code errors.h}의 ERR_NOT_FOUND. */
    public static final int ERR_NOT_FOUND = -9;

    /**
     * 취소 결과.
     *
     * @param order 취소 뒤 원장에서 다시 읽은 상태. 못 읽었거나 모르는 주문이면 null
     */
    public record CancelResult(long orderId, int reason, int status, int canceledQty, OrderView order) {}

    private final LedgerConnectionPool pool;
    private final LedgerGateway gateway;
    private final OrderRegistry registry;
    private final StreamHub hub;
    private final String account;

    /**
     * 전문에 실을 논리 시각. <b>시스템 시각을 읽지 않는다</b>(CLAUDE.md).
     * 채널계는 시각의 의미를 알 필요가 없고, 늘어나기만 하면 된다.
     */
    private final AtomicLong logicalClock = new AtomicLong(1);

    public OrderService(
            LedgerConnectionPool pool,
            LedgerGateway gateway,
            OrderRegistry registry,
            StreamHub hub,
            @Value("${minisor.account}") String account) {
        this.pool = pool;
        this.gateway = gateway;
        this.registry = registry;
        this.hub = hub;
        this.account = account;
    }

    public OrderResponseDto submit(OrderRequestDto req) {
        OrderResponseDto res = send(req);
        hub.broadcast(StreamEvent.order(Map.of("request", req, "result", res)));
        if (res.outcome() != OrderResponseDto.Outcome.ACCEPTED) {
            return res;
        }

        OrderView view = readView(req.account(), res.orderId(), req.type());
        if (view == null) {
            /*
             * 접수는 됐는데 상세를 못 읽었다. 목록에는 넣고(끝나지 않은 것으로 — 주기 작업이 다시
             * 읽는다), 체결은 접수 응답대로 알린다. 시장은 고른 값(255면 SOR)으로 둘 수밖에 없다.
             */
            registry.put(OrderView.fromAccepted(req, res));
            if (res.filledQty() > 0) {
                broadcastFill(res.clOrdId(), res.orderId(), req.side(), req.market(),
                        res.avgPrice(), res.filledQty());
            }
            return res;
        }
        registry.put(view);
        for (OrderView.LegView leg : view.legs()) {
            if (leg.filled() > 0) {
                broadcastFill(view.clOrdId(), view.orderId(), view.side(), leg.market(),
                        leg.avgPrice(), leg.filled());
            }
        }
        return res;
    }

    /** 이 채널계가 낸 주문, 최근 것부터. */
    public List<OrderView> orders() {
        return registry.newestFirst();
    }

    public boolean knows(long orderId) {
        return registry.contains(orderId);
    }

    /**
     * 원장에서 다시 읽는다. 없거나 남의 주문이면 null. 원장에 못 붙으면 {@link LedgerException}.
     * 목록({@link OrderRegistry})은 고치지 않는다 — 그것은 {@link LedgerPoller}의 일이다.
     */
    public OrderView detail(long orderId) {
        int type = registry.get(orderId).map(OrderView::type).orElse(0);
        DetailAck d = fetchDetail(account, orderId);
        return d.reason == 0 ? OrderView.from(d, type) : null;
    }

    /** 살아 있는 물리 주문을 모두 취소한다. 원장에 못 붙으면 {@link LedgerException}. */
    public CancelResult cancel(long orderId) {
        CancelReq r = new CancelReq();
        r.account = account;
        r.orderId = orderId;
        r.clOrdId = registry.get(orderId).map(OrderView::clOrdId).orElse(0L);
        CancelAck ack = gateway.call(r, CancelAck.class);

        OrderView view = null;
        if (registry.contains(orderId)) {
            view = readView(account, orderId, registry.get(orderId).map(OrderView::type).orElse(0));
            if (view != null) {
                registry.put(view);
                hub.broadcast(StreamEvent.orderUpdate(view));
            }
        }
        return new CancelResult(orderId, ack.reason, ack.status, ack.canceledQty, view);
    }

    private DetailAck fetchDetail(String acct, long orderId) {
        DetailReq req = new DetailReq();
        req.account = acct;
        req.orderId = orderId;
        return gateway.call(req, DetailAck.class);
    }

    /** 상세를 읽어 화면용으로. 못 읽거나 없으면 null — 부르는 쪽의 본래 결과를 바꾸지 않는다. */
    private OrderView readView(String acct, long orderId, int type) {
        try {
            DetailAck d = fetchDetail(acct, orderId);
            return d.reason == 0 ? OrderView.from(d, type) : null;
        } catch (LedgerException e) {
            return null;
        }
    }

    private void broadcastFill(long clOrdId, long orderId, int side, int market, int price, int qty) {
        hub.broadcast(
                StreamEvent.fill(
                        Map.of(
                                "clOrdId", clOrdId,
                                "orderId", orderId,
                                "side", side,
                                "market", market,
                                "price", price,
                                "qty", qty)));
    }

    private OrderResponseDto send(OrderRequestDto req) {
        OrderReq m = new OrderReq();
        m.account = req.account();
        m.symbol = req.symbol();
        m.clOrdId = req.clOrdId();
        m.side = req.side();
        m.type = req.type();
        m.market = req.market();
        m.price = req.price();
        m.qty = req.qty();

        LedgerConnection c;
        try {
            c = pool.borrow();
        } catch (LedgerException e) {
            /*
             * **아직 아무것도 보내지 않았다.** 원장이 죽었거나 풀이 모자란
             * 것이고, 주문은 확실히 나가지 않았다 — 모호하지 않다.
             */
            hub.ledgerReachable(false, e.getMessage());
            throw e;
        }

        try {
            OrderAck ack = c.call(m, OrderAck.class, logicalClock.getAndIncrement());
            pool.release(c);
            hub.ledgerReachable(true, null);

            if (ack.reason != 0) {
                return OrderResponseDto.rejected(
                        ack.clOrdId, ack.reason, "원장이 거절했다");
            }
            return OrderResponseDto.accepted(
                    ack.clOrdId, ack.orderId, ack.status, ack.filledQty, ack.price);

        } catch (LedgerException e) {
            /*
             * **보낸 뒤에 실패했다.** 닿았는지 모른다. 접속은 버리고
             * 모른다고 답한다.
             */
            pool.release(c);
            hub.ledgerReachable(false, e.getMessage());
            return OrderResponseDto.inDoubt(
                    req.clOrdId(), "원장 응답을 받지 못했다. 조회로 확인해야 한다");
        }
    }
}
