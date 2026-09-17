package com.minisor.channel.api;

import com.minisor.channel.ledger.LedgerConnection;
import com.minisor.channel.ledger.LedgerConnectionPool;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.stream.StreamHub;
import java.util.concurrent.atomic.AtomicLong;
import org.springframework.stereotype.Component;

/**
 * 원장에 요청 하나를 보내고 응답 하나를 받는다 — <b>조회·취소처럼 다시 보내도 안전한 요청</b>용(T7-03).
 *
 * <p>접속 빌리기·돌려주기와 원장 상태 알림(끊김·회복)을 한 곳에 모았다. 호가·상세·잔고·취소가 같은
 * 순서를 되풀이하던 것을 하나로 줄였다.
 *
 * <p>주문 접수는 이것을 쓰지 않는다({@link OrderService}). 주문은 "빌리기 전 실패"(확실히 안 나감)와
 * "보낸 뒤 실패"(모름)를 구별해 답해야 하는데, 이 게이트웨이는 둘을 같은 예외로 올린다.
 */
@Component
public class LedgerGateway {

    private final LedgerConnectionPool pool;
    private final StreamHub hub;

    /** 전문에 실을 논리 시각. 시스템 시각을 읽지 않는다(CLAUDE.md). */
    private final AtomicLong logicalClock = new AtomicLong(1);

    public LedgerGateway(LedgerConnectionPool pool, StreamHub hub) {
        this.pool = pool;
        this.hub = hub;
    }

    /** 원장이 답하지 않거나 붙지 못하면 {@link LedgerException}. */
    public <T> T call(Object request, Class<T> responseType) {
        LedgerConnection c;
        try {
            c = pool.borrow();
        } catch (LedgerException e) {
            hub.ledgerReachable(false, e.getMessage());
            throw e;
        }
        try {
            T res = c.call(request, responseType, logicalClock.getAndIncrement());
            hub.ledgerReachable(true, null);
            return res;
        } catch (LedgerException e) {
            hub.ledgerReachable(false, e.getMessage());
            throw e;
        } finally {
            pool.release(c);
        }
    }
}
