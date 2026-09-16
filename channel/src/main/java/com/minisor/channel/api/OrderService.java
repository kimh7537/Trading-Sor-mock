package com.minisor.channel.api;

import com.minisor.channel.ledger.LedgerConnection;
import com.minisor.channel.ledger.LedgerConnectionPool;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.stream.StreamEvent;
import com.minisor.channel.stream.StreamHub;
import com.minisor.channel.wire.OrderAck;
import com.minisor.channel.wire.OrderReq;
import java.util.Map;
import java.util.concurrent.atomic.AtomicLong;
import org.springframework.stereotype.Service;

/**
 * 주문을 원장으로 보낸다.
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
 * 쪽에서 내린 것과 같은 판단이다.
 *
 * <h2>결과를 방송한다 (T6-04)</h2>
 *
 * 원장의 답을 받으면 <b>주문 결과</b>를, 체결이 있었으면 <b>체결</b>을 WebSocket 구독자
 * 모두에게 보낸다. 주문을 낸 화면 말고 다른 화면도 같은 것을 보게 한다.
 *
 * <p>ponytail: 원장은 요청-응답만 한다. 예전에 걸어 둔 주문이 <b>나중에</b> 체결된 것은
 * 여기서 방송하지 못한다. 원장은 주문번호 조회(MSG_QUERY_REQ)에 답하지만 채널계는 아직
 * 그것을 여는 API가 없다. 원장이 체결 통보를 밀어 보내게 하려면 원장 쪽에 구독 접속이
 * 따로 있어야 한다.
 *
 * <p>같은 이유로 아래의 "조회로 확인해야 한다"는 <b>원칙</b>이지 지금 화면에서 할 수 있는
 * 일이 아니다. 게다가 답을 못 받은 주문은 주문번호도 모르므로, 주문을 낸 쪽 번호(cl_ord_id)로
 * 찾는 조회가 전문에 먼저 생겨야 한다.
 */
@Service
public class OrderService {

    private final LedgerConnectionPool pool;
    private final StreamHub hub;

    /**
     * 전문에 실을 논리 시각. <b>시스템 시각을 읽지 않는다</b>(CLAUDE.md).
     * 채널계는 시각의 의미를 알 필요가 없고, 늘어나기만 하면 된다.
     */
    private final AtomicLong logicalClock = new AtomicLong(1);

    public OrderService(LedgerConnectionPool pool, StreamHub hub) {
        this.pool = pool;
        this.hub = hub;
    }

    public OrderResponseDto submit(OrderRequestDto req) {
        OrderResponseDto res = send(req);
        hub.broadcast(StreamEvent.order(Map.of("request", req, "result", res)));
        if (res.filledQty() > 0) {
            hub.broadcast(
                    StreamEvent.fill(
                            Map.of(
                                    "clOrdId", res.clOrdId(),
                                    "orderId", res.orderId(),
                                    "side", req.side(),
                                    "market", req.market(),
                                    "price", res.avgPrice(),
                                    "qty", res.filledQty())));
        }
        return res;
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
