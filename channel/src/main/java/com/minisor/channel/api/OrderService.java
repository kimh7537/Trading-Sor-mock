package com.minisor.channel.api;

import com.minisor.channel.ledger.LedgerConnection;
import com.minisor.channel.ledger.LedgerConnectionPool;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.wire.OrderAck;
import com.minisor.channel.wire.OrderReq;
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
 */
@Service
public class OrderService {

    private final LedgerConnectionPool pool;

    /**
     * 전문에 실을 논리 시각. <b>시스템 시각을 읽지 않는다</b>(CLAUDE.md).
     * 채널계는 시각의 의미를 알 필요가 없고, 늘어나기만 하면 된다.
     */
    private final AtomicLong logicalClock = new AtomicLong(1);

    public OrderService(LedgerConnectionPool pool) {
        this.pool = pool;
    }

    public OrderResponseDto submit(OrderRequestDto req) {
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
            throw e;
        }

        try {
            OrderAck ack = c.call(m, OrderAck.class, logicalClock.getAndIncrement());
            pool.release(c);

            if (ack.reason != 0) {
                return OrderResponseDto.rejected(
                        ack.clOrdId, ack.reason, "원장이 거절했다");
            }
            return OrderResponseDto.accepted(ack.clOrdId, ack.orderId);

        } catch (LedgerException e) {
            /*
             * **보낸 뒤에 실패했다.** 닿았는지 모른다. 접속은 버리고
             * 모른다고 답한다.
             */
            pool.release(c);
            return OrderResponseDto.inDoubt(
                    req.clOrdId(), "원장 응답을 받지 못했다. 조회로 확인해야 한다");
        }
    }
}
