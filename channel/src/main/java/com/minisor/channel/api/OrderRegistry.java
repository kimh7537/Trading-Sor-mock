package com.minisor.channel.api;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.springframework.stereotype.Component;

/**
 * 이 채널계가 원장에 낸 주문과 마지막으로 본 상태(T7-03).
 *
 * <p>원장은 "이 계좌의 주문 목록"을 한 번에 주지 않는다. 그래서 접수된 주문번호를 여기 적어 두고,
 * 화면을 새로고침해도 목록을 돌려줄 수 있게 한다. {@link LedgerPoller}는 끝나지 않은 주문만 다시 읽는다.
 *
 * <p>ponytail: 메모리에만 둔다. 채널계를 다시 띄우면 목록이 비고, 원장을 다시 띄우면 원장 쪽 주문이
 * 사라진다(원장 데몬도 메모리만 쓴다). 둘 다 데모의 수명 안에서는 충분하다. 오래 남겨야 하면
 * 원장에 "계좌의 주문 목록" 전문을 더하는 것이 먼저다.
 */
@Component
public class OrderRegistry {

    /** 담는 주문 수의 상한. 넘으면 가장 오래된 것부터 버린다. */
    public static final int CAPACITY = 500;

    private final Map<Long, OrderView> orders = new LinkedHashMap<>();

    /** 새 주문이면 뒤에 붙이고, 있던 주문이면 자리를 지킨 채 상태만 바꾼다. */
    public synchronized void put(OrderView v) {
        orders.put(v.orderId(), v);
        while (orders.size() > CAPACITY) {
            Long oldest = orders.keySet().iterator().next();
            orders.remove(oldest);
        }
    }

    public synchronized Optional<OrderView> get(long orderId) {
        return Optional.ofNullable(orders.get(orderId));
    }

    public synchronized boolean contains(long orderId) {
        return orders.containsKey(orderId);
    }

    public synchronized List<OrderView> newestFirst() {
        List<OrderView> out = new ArrayList<>(orders.values());
        Collections.reverse(out);
        return out;
    }

    /** 아직 살아 있는 수량이 있는 주문. 주기 작업이 다시 읽을 대상이다. */
    public synchronized List<OrderView> open() {
        return orders.values().stream().filter(v -> !v.done()).toList();
    }
}
