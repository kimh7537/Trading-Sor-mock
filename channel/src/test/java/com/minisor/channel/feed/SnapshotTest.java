package com.minisor.channel.feed;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.LocalTime;
import org.junit.jupiter.api.Test;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;

/**
 * T8-04 — 바깥 시세의 모양을 우리 말로 옮긴다.
 *
 * <p>스펙에서 확인한 것을 그대로 시험한다: 가격·잔량이 <b>문자열 decimal</b>이고, 단수가
 * 10단 고정이 아니며(예시가 3단·1단), 장 시작 전에는 {@code timestamp}가 {@code null}이다.
 */
class SnapshotTest {

    private static final JsonMapper JSON = JsonMapper.builder().build();

    private static JsonNode node(String json) {
        return JSON.readTree(json);
    }

    /** 문자열 가격·잔량이 정수가 된다. 부동소수로 들고 다니지 않는다. */
    @Test
    void stringDecimalsBecomeIntegers() {
        JsonNode data =
                node(
                        "{\"timestamp\":\"2026-09-18T09:00:01+09:00\",\"currency\":\"KRW\","
                                + "\"bids\":[{\"price\":\"69900\",\"volume\":\"12\"},"
                                + "{\"price\":\"69800\",\"volume\":\"5\"}],"
                                + "\"asks\":[{\"price\":\"70000.00\",\"volume\":\"7\"}]}");

        Snapshot s = Snapshot.fromToss("005930", data, 0);

        assertThat(s.symbol()).isEqualTo("005930");
        assertThat(s.bids()).hasSize(2);
        assertThat(s.bids().get(0)).containsExactly(69900, 12);
        assertThat(s.bids().get(1)).containsExactly(69800, 5);
        assertThat(s.asks()).hasSize(1);
        assertThat(s.asks().get(0)).containsExactly(70000, 7);
        assertThat(s.tsNanos()).isEqualTo(LocalTime.of(9, 0, 1).toNanoOfDay());
    }

    /** 장 시작 전에는 시각이 {@code null}이다 — 대체값을 쓰고 흐름을 멈추지 않는다. */
    @Test
    void nullTimestampFallsBack() {
        JsonNode data = node("{\"timestamp\":null,\"bids\":[],\"asks\":[]}");
        Snapshot s = Snapshot.fromToss("005930", data, 12345);
        assertThat(s.tsNanos()).isEqualTo(12345);
        assertThat(s.bids()).isEmpty();
        assertThat(s.asks()).isEmpty();
    }

    /** 0 잔량·빠진 필드·읽을 수 없는 숫자는 그 단만 버린다. */
    @Test
    void dropsUnusableLevels() {
        JsonNode data =
                node(
                        "{\"bids\":[{\"price\":\"69900\",\"volume\":\"0\"},"
                                + "{\"price\":\"69800\"},"
                                + "{\"price\":\"abc\",\"volume\":\"3\"},"
                                + "{\"price\":\"69700\",\"volume\":\"4\"}],"
                                + "\"asks\":[]}");
        Snapshot s = Snapshot.fromToss("005930", data, 1);
        assertThat(s.bids()).hasSize(1);
        assertThat(s.bids().get(0)).containsExactly(69700, 4);
    }

    /** 10단이 넘으면 전문을 만들 때 앞 10단만 싣는다. 스냅샷 자체는 자르지 않는다. */
    @Test
    void keepsAllLevelsAndTruncationHappensOnTheWire() {
        StringBuilder b = new StringBuilder("{\"bids\":[");
        for (int i = 0; i < 15; i++) {
            b.append(i > 0 ? "," : "")
                    .append("{\"price\":\"")
                    .append(70000 - i * 100)
                    .append("\",\"volume\":\"1\"}");
        }
        b.append("],\"asks\":[]}");

        Snapshot s = Snapshot.fromToss("005930", node(b.toString()), 1);
        assertThat(s.bids()).hasSize(15);

        int[] price = new int[10];
        int[] qty = new int[10];
        com.minisor.channel.wire.BookFeed.fill(price, qty, s.bids());
        assertThat(price[9]).isEqualTo(69100);
    }
}
