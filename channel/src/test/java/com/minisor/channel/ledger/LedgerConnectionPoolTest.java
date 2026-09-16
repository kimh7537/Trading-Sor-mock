package com.minisor.channel.ledger;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.minisor.channel.wire.OrderAck;
import com.minisor.channel.wire.OrderReq;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

/**
 * T4-03 완료 조건을 그대로 옮긴다.
 *
 * <ol>
 *   <li>왕복하고, 접속을 재사용한다
 *   <li>응답이 제때 안 오면 그 접속을 버린다
 *   <li>원장이 죽었으면 알린다
 *   <li>풀이 고갈되면 기다렸다 실패한다
 *   <li><b>동시 요청이 서로 응답을 바꿔 받지 않는다</b>
 * </ol>
 */
class LedgerConnectionPoolTest {

    private FakeLedger ledger;

    @BeforeEach
    void setUp() throws Exception {
        ledger = new FakeLedger();
    }

    @AfterEach
    void tearDown() throws Exception {
        ledger.close();
    }

    private LedgerProperties props(int connectMs, int readMs) {
        return new LedgerProperties("127.0.0.1", ledger.port(), connectMs, readMs);
    }

    private static OrderReq order(long clOrdId) {
        OrderReq m = new OrderReq();
        m.account = "123456789012";
        m.symbol = "005930";
        m.clOrdId = clOrdId;
        m.side = 1;
        m.type = 1;
        m.market = 1;
        m.price = 70000;
        m.qty = 10;
        return m;
    }

    @Test
    void roundTrip() {
        try (LedgerConnectionPool pool = new LedgerConnectionPool(props(2000, 2000))) {
            LedgerConnection c = pool.borrow();
            OrderAck ack = c.call(order(7), OrderAck.class, 1L);
            pool.release(c);

            assertThat(ack.clOrdId).isEqualTo(7);
            assertThat(ack.orderId).isEqualTo(100007);
            assertThat(ack.price).isEqualTo(70000);
        }
    }

    /** 접속을 요청마다 새로 만들지 않는다 — 그것이 풀을 두는 이유다. */
    @Test
    void reusesConnections() {
        try (LedgerConnectionPool pool = new LedgerConnectionPool(props(2000, 2000))) {
            for (int i = 0; i < 20; i++) {
                LedgerConnection c = pool.borrow();
                c.call(order(i), OrderAck.class, 1L);
                pool.release(c);
            }
            assertThat(ledger.requests()).isEqualTo(20);
            assertThat(ledger.connections()).isEqualTo(1); // 하나로 스무 번
            assertThat(pool.idleCount()).isEqualTo(1);
        }
    }

    /**
     * <b>응답이 제때 안 오면 그 접속을 버린다.</b> 되돌려 쓰면 다음 요청이
     * 앞 요청의 답을 받는다 — 주문 응답이 한 칸씩 밀린다.
     */
    @Test
    void discardsConnectionOnTimeout() {
        ledger.setSilent(true);

        try (LedgerConnectionPool pool = new LedgerConnectionPool(props(2000, 200))) {
            LedgerConnection c = pool.borrow();

            assertThatThrownBy(() -> c.call(order(1), OrderAck.class, 1L))
                    .isInstanceOf(LedgerException.class);

            assertThat(c.isBroken()).isTrue();

            /* 돌려줘도 풀에 들어가지 않는다. */
            pool.release(c);
            assertThat(pool.idleCount()).isZero();
            assertThat(pool.aliveCount()).isZero();
        }
    }

    /** 원장이 죽었으면 매달리지 않고 알린다. */
    @Test
    void reportsWhenLedgerIsDown() throws Exception {
        int deadPort = ledger.port();
        ledger.close();

        LedgerProperties cfg = new LedgerProperties("127.0.0.1", deadPort, 300, 300);
        try (LedgerConnectionPool pool = new LedgerConnectionPool(cfg)) {
            assertThatThrownBy(pool::borrow).isInstanceOf(LedgerException.class);
            assertThat(pool.aliveCount()).isZero(); // 실패한 접속을 세지 않는다
        }
    }

    /** 설정을 빠뜨린 채 뜬 프로세스는 붙으려 들기 전에 말해 준다. */
    @Test
    void refusesWhenUnconfigured() {
        LedgerProperties cfg = new LedgerProperties("127.0.0.1", 0, 1000, 1000);
        try (LedgerConnectionPool pool = new LedgerConnectionPool(cfg)) {
            assertThatThrownBy(pool::borrow)
                    .isInstanceOf(LedgerException.class)
                    .hasMessageContaining("설정");
        }
    }

    /** 풀이 고갈되면 <b>늘리지 않고</b> 기다렸다 실패한다. */
    @Test
    void failsWhenPoolExhausted() {
        try (LedgerConnectionPool pool =
                new LedgerConnectionPool(props(2000, 2000), 2, 150)) {
            LedgerConnection a = pool.borrow();
            LedgerConnection b = pool.borrow();
            assertThat(pool.aliveCount()).isEqualTo(2);

            long t0 = System.nanoTime();
            assertThatThrownBy(pool::borrow)
                    .isInstanceOf(LedgerException.class)
                    .hasMessageContaining("모자라");
            long waitedMs = (System.nanoTime() - t0) / 1_000_000;

            /* 곧바로 실패하지 않고 기다렸다. */
            assertThat(waitedMs).isGreaterThanOrEqualTo(100);
            /* 상한을 넘겨 만들지 않았다. */
            assertThat(pool.aliveCount()).isEqualTo(2);

            pool.release(a);
            pool.release(b);
            assertThat(pool.borrow()).isNotNull();
        }
    }

    /**
     * <b>동시 요청이 서로 응답을 바꿔 받지 않는다.</b>
     *
     * <p>이것이 풀에서 가장 조용한 위험이다. 한 접속을 두 스레드가 같이 쓰면
     * 응답이 뒤바뀌는데, 값이 그럴듯해서 한참 뒤 잔고가 안 맞을 때에야 드러난다.
     */
    @Test
    void concurrentCallsDoNotCrossAnswers() throws Exception {
        ledger.setDelayMs(5); // 겹칠 틈을 넓힌다

        ExecutorService pool2 = Executors.newFixedThreadPool(8);
        try (LedgerConnectionPool pool = new LedgerConnectionPool(props(2000, 2000), 4, 2000)) {

            List<Callable<Boolean>> jobs = new ArrayList<>();
            for (int i = 0; i < 64; i++) {
                final long id = 1000 + i;
                jobs.add(
                        () -> {
                            LedgerConnection c = pool.borrow();
                            try {
                                OrderAck ack = c.call(order(id), OrderAck.class, 1L);
                                /* 내가 보낸 번호의 답이어야 한다. */
                                return ack.clOrdId == id && ack.orderId == id + 100000;
                            } finally {
                                pool.release(c);
                            }
                        });
            }

            List<Future<Boolean>> rs = pool2.invokeAll(jobs, 30, TimeUnit.SECONDS);
            for (Future<Boolean> f : rs) {
                assertThat(f.get()).isTrue();
            }
            /* 접속은 상한을 넘지 않았다. */
            assertThat(ledger.connections()).isLessThanOrEqualTo(4);
        } finally {
            pool2.shutdownNow();
        }
    }
}
