package com.minisor.channel.api;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.List;
import org.junit.jupiter.api.Test;

/** T7-03 — 목록 순서, 상태 바꾸기, 상한. */
class OrderRegistryTest {

    private static OrderView view(long id, int working) {
        return new OrderView(id, id, 0, 0, 0, 70000, 10, 10 - working, 0, working,
                70000L * (10 - working), 70000, working == 0 ? 2 : 0, working == 0, List.of());
    }

    @Test
    void newestFirstAndUpdateKeepsPlace() {
        OrderRegistry r = new OrderRegistry();
        r.put(view(1, 10));
        r.put(view(2, 10));
        r.put(view(3, 10));
        r.put(view(1, 0)); // 1번이 체결됐다 — 자리는 그대로, 상태만 바뀐다

        assertThat(r.newestFirst()).extracting(OrderView::orderId).containsExactly(3L, 2L, 1L);
        assertThat(r.get(1).orElseThrow().done()).isTrue();
        assertThat(r.open()).extracting(OrderView::orderId).containsExactly(2L, 3L);
        assertThat(r.contains(2)).isTrue();
        assertThat(r.contains(9)).isFalse();
    }

    @Test
    void dropsOldestBeyondCapacity() {
        OrderRegistry r = new OrderRegistry();
        for (long id = 1; id <= OrderRegistry.CAPACITY + 1; id++) {
            r.put(view(id, 10));
        }
        assertThat(r.newestFirst()).hasSize(OrderRegistry.CAPACITY);
        assertThat(r.contains(1)).isFalse();
        assertThat(r.contains(2)).isTrue();
        assertThat(r.newestFirst().get(0).orderId()).isEqualTo(OrderRegistry.CAPACITY + 1);
    }
}
