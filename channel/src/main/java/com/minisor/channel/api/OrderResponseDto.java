package com.minisor.channel.api;

/**
 * 주문 응답.
 *
 * <p>{@code outcome}이 이 응답의 핵심이다 — <b>접수·거절만으로는 부족하다.</b>
 * 원장에 보냈는데 답을 못 받은 경우가 있고, 그것을 성공이나 실패로 단정하면
 * 안 된다(T3-14가 FEP 쪽에서 내린 것과 같은 판단).
 */
public record OrderResponseDto(
        Outcome outcome, long clOrdId, long orderId, int reason, String message) {

    public enum Outcome {
        /** 원장이 접수했다. */
        ACCEPTED,
        /** 원장이 거절했다. 사유가 있다. */
        REJECTED,
        /**
         * <b>모른다.</b> 보냈지만 답을 못 받았다 — 원장이 처리했는지 아닌지
         * 알 수 없다. 다시 보내면 중복 주문이 될 수 있으므로 <b>여기서
         * 자동으로 재시도하지 않는다.</b> 조회로 확인해야 한다.
         */
        IN_DOUBT
    }

    static OrderResponseDto accepted(long clOrdId, long orderId) {
        return new OrderResponseDto(Outcome.ACCEPTED, clOrdId, orderId, 0, "접수");
    }

    static OrderResponseDto rejected(long clOrdId, int reason, String message) {
        return new OrderResponseDto(Outcome.REJECTED, clOrdId, 0, reason, message);
    }

    static OrderResponseDto inDoubt(long clOrdId, String message) {
        return new OrderResponseDto(Outcome.IN_DOUBT, clOrdId, 0, 0, message);
    }
}
