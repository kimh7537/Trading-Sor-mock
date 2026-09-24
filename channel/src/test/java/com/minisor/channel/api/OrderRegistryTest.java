package com.minisor.channel.api;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.List;
import org.junit.jupiter.api.Test;

/** T7-03 — 목록 순서, 상태 바꾸기, 상한. T9-04 — 계좌마다 서랍이 따로다. */
class OrderRegistryTest {

    private static OrderView view(long id, int working) {
        return new OrderView(id, id, 0, 0, 0, 70000, 10, 10 - working, 0, working,
                70000L * (10 - working), 70000, working == 0 ? 2 : 0, working == 0, List.of());
    }

    private static final String A = "u00000000001";
    private static final String B = "u00000000002";

    @Test
    void newestFirstAndUpdateKeepsPlace() {
        OrderRegistry r = new OrderRegistry();
        r.put(A, view(1, 10));
        r.put(A, view(2, 10));
        r.put(A, view(3, 10));
        r.put(A, view(1, 0)); // 1번이 체결됐다 — 자리는 그대로, 상태만 바뀐다

        assertThat(r.newestFirst(A)).extracting(OrderView::orderId).containsExactly(3L, 2L, 1L);
        assertThat(r.get(A, 1).orElseThrow().done()).isTrue();
        assertThat(r.open(A)).extracting(OrderView::orderId).containsExactly(2L, 3L);
        assertThat(r.contains(A, 2)).isTrue();
        assertThat(r.contains(A, 9)).isFalse();
    }

    /** 남의 주문이 내 목록에 섞이지 않는다 — 사용자가 여럿인 전제의 핵심이다(T9-04). */
    @Test
    void accountsDoNotSeeEachOther() {
        OrderRegistry r = new OrderRegistry();
        r.put(A, view(1, 10));
        r.put(B, view(2, 10));

        assertThat(r.newestFirst(A)).extracting(OrderView::orderId).containsExactly(1L);
        assertThat(r.newestFirst(B)).extracting(OrderView::orderId).containsExactly(2L);
        assertThat(r.contains(A, 2)).isFalse();
        assertThat(r.get(B, 1)).isEmpty();

        /* 주기 작업은 둘 다 읽어야 하고, 누구의 것인지 알아야 한다 */
        assertThat(r.openAll())
                .extracting(OrderRegistry.OpenOrder::account)
                .containsExactlyInAnyOrder(A, B);
    }

    @Test
    void dropsOldestBeyondCapacity() {
        OrderRegistry r = new OrderRegistry();
        for (long id = 1; id <= OrderRegistry.CAPACITY + 1; id++) {
            r.put(A, view(id, 10));
        }
        assertThat(r.newestFirst(A)).hasSize(OrderRegistry.CAPACITY);
        assertThat(r.contains(A, 1)).isFalse();
        assertThat(r.contains(A, 2)).isTrue();
        assertThat(r.newestFirst(A).get(0).orderId()).isEqualTo(OrderRegistry.CAPACITY + 1);

        /* 상한은 계좌마다 따로 센다 — 한 사람이 많이 내도 남의 목록이 밀리지 않는다 */
        r.put(B, view(7, 10));
        assertThat(r.newestFirst(B)).hasSize(1);
    }
}
