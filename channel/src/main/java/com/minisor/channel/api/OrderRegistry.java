package com.minisor.channel.api;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import org.springframework.stereotype.Component;

/**
 * 이 채널계가 원장에 낸 주문과 마지막으로 본 상태(T7-03), <b>계좌별</b>(T9-04).
 *
 * <p>원장은 "이 계좌의 주문 목록"을 한 번에 주지 않는다. 그래서 접수된 주문번호를 여기 적어 두고,
 * 화면을 새로고침해도 목록을 돌려줄 수 있게 한다. {@link LedgerPoller}는 끝나지 않은 주문만 다시 읽는다.
 *
 * <p><b>계좌마다 서랍을 따로 둔다.</b> 한 서랍에 모아 두면 목록을 돌려줄 때마다 걸러야 하고,
 * 한 번 빠뜨리면 남의 주문이 화면에 뜬다. 상한도 계좌마다 따로 세어, 한 사람이 많이 내도
 * 다른 사람 목록이 밀려 사라지지 않는다.
 *
 * <p>ponytail: 메모리에만 둔다. 채널계를 다시 띄우면 목록이 비고, 원장을 다시 띄우면 원장 쪽 주문이
 * 사라진다(원장 데몬도 메모리만 쓴다). 둘 다 데모의 수명 안에서는 충분하다. 오래 남겨야 하면
 * 원장에 "계좌의 주문 목록" 전문을 더하는 것이 먼저다.
 */
@Component
public class OrderRegistry {

    /** 한 계좌가 담는 주문 수의 상한. 넘으면 그 계좌에서 가장 오래된 것부터 버린다. */
    public static final int CAPACITY = 500;

    /** 계좌 -> (주문번호 -> 마지막으로 본 상태). */
    private final Map<String, Map<Long, OrderView>> byAccount = new LinkedHashMap<>();

    private Map<Long, OrderView> drawer(String account) {
        return byAccount.computeIfAbsent(account, k -> new LinkedHashMap<>());
    }

    /** 새 주문이면 뒤에 붙이고, 있던 주문이면 자리를 지킨 채 상태만 바꾼다. */
    public synchronized void put(String account, OrderView v) {
        Map<Long, OrderView> orders = drawer(account);
        orders.put(v.orderId(), v);
        while (orders.size() > CAPACITY) {
            Long oldest = orders.keySet().iterator().next();
            orders.remove(oldest);
        }
    }

    public synchronized Optional<OrderView> get(String account, long orderId) {
        return Optional.ofNullable(drawer(account).get(orderId));
    }

    public synchronized boolean contains(String account, long orderId) {
        return drawer(account).containsKey(orderId);
    }

    public synchronized List<OrderView> newestFirst(String account) {
        List<OrderView> out = new ArrayList<>(drawer(account).values());
        Collections.reverse(out);
        return out;
    }

    /** 계좌 하나의, 아직 살아 있는 수량이 있는 주문. */
    public synchronized List<OrderView> open(String account) {
        return drawer(account).values().stream().filter(v -> !v.done()).toList();
    }

    /** 주기 작업이 다시 읽어야 할 것 전부 — 어느 계좌의 주문인지 함께 준다. */
    public synchronized List<OpenOrder> openAll() {
        List<OpenOrder> out = new ArrayList<>();
        byAccount.forEach((account, orders) ->
                orders.values().stream()
                        .filter(v -> !v.done())
                        .forEach(v -> out.add(new OpenOrder(account, v))));
        return out;
    }

    /** 주문을 낸 계좌를 붙여 둔 것. 주기 작업이 누구에게 알릴지 알아야 한다. */
    public record OpenOrder(String account, OrderView view) {}

    /** 주문을 한 건이라도 낸 계좌들. */
    public synchronized List<String> accounts() {
        return List.copyOf(byAccount.keySet());
    }
}
