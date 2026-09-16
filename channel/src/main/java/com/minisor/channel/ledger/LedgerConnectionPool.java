package com.minisor.channel.ledger;

import java.io.IOException;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Component;

/**
 * 원장 접속 풀.
 *
 * <h2>왜 풀인가</h2>
 *
 * 요청마다 새로 붙으면 <b>접속 비용이 주문 지연에 그대로 얹힌다.</b> TCP 3-way
 * 왕복은 같은 기계 안에서도 공짜가 아니고, 장이 바쁠 때 가장 비싸진다.
 *
 * <h2>풀이 비면 기다린다. 늘리지 않는다</h2>
 *
 * 접속을 무한정 늘리면 원장 쪽 워커(T3-04)가 감당할 수 있는 수를 넘어선다.
 * 그때 생기는 일은 "느려짐"이 아니라 <b>전부 함께 느려짐</b>이다 — 원장이
 * 접속마다 자원을 잡으므로 늘릴수록 모두가 손해다.
 *
 * <p>그래서 상한을 두고, 비면 <b>정해진 시간만 기다렸다 실패한다.</b>
 * 실패는 위로 올라가고, 무엇을 할지는 부르는 쪽이 정한다 — 이 프로젝트가
 * T3-10에서 큐가 찼을 때 한 것과 같은 판단이다.
 *
 * <h2>깨진 접속은 돌려받지 않는다</h2>
 *
 * 되돌리면 <b>다음 요청이 그것을 집는다.</b> 한 번 어긋난 스트림은 고쳐지지
 * 않으므로(T3-09의 판단) 버리고 새로 만든다.
 *
 * <h2>밀려오는 전문은 이 풀이 다루지 않는다</h2>
 *
 * 체결 통보는 요청 없이 원장이 밀어 보낸다. <b>요청-응답 접속에 그것이 섞이면
 * 응답이 한 칸씩 밀린다</b> — 주문 응답을 기다리는 자리에 남의 체결이 온다.
 *
 * <p>그래서 밀려오는 전문은 <b>별도의 구독 접속</b>이 받는다(T4-05). 이 풀은
 * 요청-응답만 다룬다. 한 접속에 둘을 섞으려면 요청마다 식별자를 달고 응답을
 * 짝지어야 하는데, 그것은 이 계층이 질 이유가 없는 복잡함이다.
 */
@Component
public class LedgerConnectionPool implements AutoCloseable {

    private final LedgerProperties cfg;
    private final int maxSize;
    private final long borrowTimeoutMs;

    private final BlockingQueue<LedgerConnection> idle;

    /** 지금 살아 있는 접속 수(빌려 간 것 + 쉬는 것). 상한을 지키는 값이다. */
    private final AtomicInteger alive = new AtomicInteger();

    private volatile boolean closed;

    /*
     * **Spring이 쓸 생성자를 명시한다.** 선언된 생성자가 둘이면 스프링은
     * 어느 쪽인지 고르지 못하고 기본 생성자를 찾다 실패한다 — 실제로 그
     * 오류로 컨텍스트가 뜨지 않았다. 아래 것은 시험용이라 표시하지 않는다.
     */
    /*
     * **접속은 1개다**(T6-03). 원장은 호가창을 하나로 지키려고 접속을 한 번에 하나씩
     * 끝까지 처리한다(`ledger_core.h`). 처음엔 8개로 두었는데, 원장이 첫 접속을 붙든
     * 동안 두 번째 접속은 받아지지도 않아 **동시 주문이 5초(읽기 제한) 멈췄다가
     * "확인 필요"로 떨어졌다.** 감사에서 찾았다.
     *
     * 접속이 하나면 요청이 풀 앞에서 줄을 선다. 매칭은 마이크로초라 줄은 짧다.
     * 동시 처리가 필요해지면 원장 쪽을 여러 접속을 받는 이벤트 루프(T3-08)로 바꾸고
     * 이 값을 올린다 — 풀만 키우면 같은 멈춤이 돌아온다.
     */
    @Autowired
    public LedgerConnectionPool(LedgerProperties cfg) {
        this(cfg, 1, 2000);
    }

    /*
     * 크기와 대기 시간을 직접 주는 생성자. **패키지 안에서만 보인다** —
     * 공개해 두면 Spring이 생성자를 둘 보고 어느 쪽을 쓸지 고르지 못한다
     * (실제로 그 오류로 컨텍스트가 뜨지 않았다). 시험이 값을 바꿔 보는 용도다.
     */
    LedgerConnectionPool(LedgerProperties cfg, int maxSize, long borrowTimeoutMs) {
        if (maxSize <= 0) {
            throw new IllegalArgumentException("maxSize는 1 이상이어야 한다");
        }
        this.cfg = cfg;
        this.maxSize = maxSize;
        this.borrowTimeoutMs = borrowTimeoutMs;
        this.idle = new ArrayBlockingQueue<>(maxSize);
    }

    /** 쉬는 접속 수. */
    public int idleCount() {
        return idle.size();
    }

    /** 살아 있는 접속 수(빌려 간 것 포함). */
    public int aliveCount() {
        return alive.get();
    }

    /**
     * 접속을 빌린다.
     *
     * <p>쉬는 것이 있으면 그것을, 없고 상한에 못 미쳤으면 새로 만든다.
     * 둘 다 안 되면 <b>정해진 시간만 기다렸다 실패한다.</b>
     */
    public LedgerConnection borrow() {
        if (closed) {
            throw new LedgerException("풀이 닫혔다");
        }
        if (!cfg.isConfigured()) {
            /*
             * 설정을 빠뜨린 채 뜬 프로세스다. 여기서 붙으려 들면 엉뚱한 곳에
             * 붙거나 한참 기다린 끝에 실패한다 — 먼저 말해 주는 편이 낫다.
             */
            throw new LedgerException("원장 접속 설정이 없다(minisor.ledger.port)");
        }

        LedgerConnection c = idle.poll();
        while (c != null) {
            if (!c.isBroken()) {
                return c;
            }
            /* 쉬는 동안 상대가 끊었다. 버리고 다음 것을 본다. */
            discard(c);
            c = idle.poll();
        }

        if (alive.get() < maxSize) {
            return create();
        }

        try {
            c = idle.poll(borrowTimeoutMs, TimeUnit.MILLISECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            throw new LedgerException("접속을 기다리다 중단됐다");
        }
        if (c == null) {
            throw new LedgerException(
                    "원장 접속이 모자라다(" + maxSize + "개가 모두 쓰이는 중)");
        }
        if (c.isBroken()) {
            discard(c);
            return create();
        }
        return c;
    }

    /**
     * 다 쓴 접속을 돌려준다.
     *
     * <p><b>깨진 것은 받지 않는다.</b> 돌려놓으면 다음 요청이 그것을 집는다.
     */
    public void release(LedgerConnection c) {
        if (c == null) {
            return;
        }
        if (closed || c.isBroken() || !idle.offer(c)) {
            discard(c);
        }
    }

    private LedgerConnection create() {
        alive.incrementAndGet();
        try {
            return new LedgerConnection(
                    cfg.host(), cfg.port(), cfg.connectTimeoutMs(), cfg.readTimeoutMs());
        } catch (IOException e) {
            alive.decrementAndGet();
            throw new LedgerException("원장에 붙을 수 없다: " + e, e);
        }
    }

    private void discard(LedgerConnection c) {
        c.close();
        alive.decrementAndGet();
    }

    @Override
    public void close() {
        closed = true;
        LedgerConnection c;
        while ((c = idle.poll()) != null) {
            discard(c);
        }
    }
}
