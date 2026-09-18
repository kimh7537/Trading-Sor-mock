package com.minisor.channel.feed;

import tools.jackson.databind.JsonNode;
import java.math.BigDecimal;
import java.time.LocalTime;
import java.time.OffsetDateTime;
import java.util.ArrayList;
import java.util.List;

/**
 * 호가 스냅샷 하나 (T8-04). 바깥 시세의 모양을 우리 말로 옮긴 것.
 *
 * <p><b>가격도 잔량도 정수다.</b> 토스 스키마는 둘 다 {@code decimal} 문자열로 주지만
 * (국내 주식은 소수 호가가 없다) 우리 전문과 매칭 엔진은 정수를 쓴다. 부동소수로 들고
 * 다니면 체결 단가 비교에 반올림 오차가 섞인다(CLAUDE.md).
 *
 * <p><b>시각은 나노초다.</b> 원장이 시스템 시각을 읽지 않으므로 스냅샷이 자기 시각을 들고
 * 와야 한다. 장중 시각(09:00~15:30)을 그대로 쓰는 편이 엔진이 쓰는 논리 시각 눈금과 같다.
 *
 * @param symbol 종목코드
 * @param tsNanos 자정부터의 나노초. 시세에 시각이 없으면 호출부가 채운다
 * @param bids {가격, 잔량} 쌍. 높은 가격부터
 * @param asks {가격, 잔량} 쌍. 낮은 가격부터
 */
public record Snapshot(String symbol, long tsNanos, List<int[]> bids, List<int[]> asks) {

    /**
     * 토스 실시간 호가 메시지의 {@code data}를 읽는다.
     *
     * <pre>{@code
     * {"timestamp":"2026-09-18T09:00:00+09:00","currency":"KRW",
     *  "bids":[{"price":"69900","volume":"12"}],
     *  "asks":[{"price":"70000","volume":"7"}]}
     * }</pre>
     *
     * <p><b>단수를 가정하지 않는다.</b> 스키마에 {@code maxItems}가 없고 예시가 3단·1단이다.
     * 10단으로 맞추는 일은 전문을 만들 때 한다({@code BookFeed.fill}).
     *
     * <p>시각이 {@code null}이면(장 시작 전) {@code fallbackNanos}를 쓴다.
     */
    public static Snapshot fromToss(String symbol, JsonNode data, long fallbackNanos) {
        return new Snapshot(
                symbol,
                nanos(data.path("timestamp"), fallbackNanos),
                levels(data.path("bids")),
                levels(data.path("asks")));
    }

    private static long nanos(JsonNode ts, long fallback) {
        if (ts == null || ts.isNull() || !ts.isTextual() || ts.asText().isBlank()) {
            return fallback;
        }
        try {
            LocalTime t = OffsetDateTime.parse(ts.asText()).toLocalTime();
            return t.toNanoOfDay();
        } catch (RuntimeException e) {
            /* 모르는 모양의 시각이면 흐름을 멈추지 않고 대체값을 쓴다 */
            return fallback;
        }
    }

    private static List<int[]> levels(JsonNode arr) {
        List<int[]> out = new ArrayList<>();
        if (arr == null || !arr.isArray()) {
            return out;
        }
        for (JsonNode n : arr) {
            int price = intOf(n.path("price"));
            int qty = intOf(n.path("volume"));
            if (price > 0 && qty > 0) {
                out.add(new int[] {price, qty});
            }
        }
        return out;
    }

    /** {@code "70100"}도 {@code "70100.0"}도 {@code 70100}도 받는다. */
    private static int intOf(JsonNode n) {
        if (n == null || n.isNull() || n.isMissingNode()) {
            return 0;
        }
        try {
            return new BigDecimal(n.asText()).intValueExact();
        } catch (ArithmeticException | NumberFormatException e) {
            return 0;
        }
    }
}
