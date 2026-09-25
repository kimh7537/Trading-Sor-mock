package com.minisor.channel.store;

import static org.assertj.core.api.Assertions.assertThat;

import com.minisor.channel.api.OrderRegistry;
import com.minisor.channel.api.OrderView;
import com.minisor.channel.ledger.FakeLedger;
import java.util.List;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;

/**
 * 점검 — <b>장부를 바꿀 때 미체결이 넘어가지 않는다.</b>
 *
 * <p>시세 모드나 종목이 바뀌면 그 주문이 걸려 있던 시장은 더 이상 같은 시장이 아니다.
 * 시뮬에서 건 주문이 그대로 남아 실시세 호가에 체결되면, 지어낸 판단이 실제 모의투자
 * 성적으로 넘어온다. 계좌를 다시 싣기 전에 살아 있는 주문을 먼저 정리해야 한다.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class AccountSeederTest {

    private static FakeLedger ledger;

    @DynamicPropertySource
    static void props(DynamicPropertyRegistry reg) throws Exception {
        ledger = new FakeLedger();
        reg.add("minisor.ledger.port", ledger::port);
        reg.add("minisor.poller.enabled", () -> false);
        reg.add("minisor.feed.enabled", () -> false);
    }

    private static final String A = "u00000000001";
    private static final String B = "u00000000002";

    @Autowired private AccountSeeder seeder;
    @Autowired private OrderRegistry registry;

    private static OrderView open(long id, int working) {
        return new OrderView(
                id, id, 0, 0, 0, 70000, 10, 10 - working, 0, working,
                70000L * (10 - working), 70000, working == 0 ? 2 : 0, working == 0, List.of());
    }

    @Test
    void cancelsMyOpenOrdersBeforeSeeding() {
        registry.put(A, open(7001, 10)); // 살아 있다
        registry.put(A, open(7002, 0)); // 이미 끝났다 — 취소할 것이 없다
        registry.put(B, open(7003, 10)); // 남의 것 — 건드리지 않는다

        seeder.seed(A);

        assertThat(ledger.canceledIds()).contains(7001L);
        assertThat(ledger.canceledIds()).doesNotContain(7002L, 7003L);
    }

    /** 계좌를 <b>그 장부의 값으로 맞춘다</b> — 보유 0도 0으로 실어 보낸다. */
    @Test
    void sendsThatBooksValuesEvenWhenFlat() {
        seeder.seed(B);

        assertThat(ledger.lastAccountOpen()).isNotNull();
        assertThat(ledger.lastAccountOpen().account).isEqualTo(B);
        assertThat(ledger.lastAccountOpen().posQty).isZero();
        assertThat(ledger.lastAccountOpen().posCost).isZero();
        assertThat(ledger.lastAccountOpen().cash).isEqualTo(seeder.seedCash());
    }
}
