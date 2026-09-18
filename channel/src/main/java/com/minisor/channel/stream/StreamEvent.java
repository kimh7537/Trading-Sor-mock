package com.minisor.channel.stream;

/** 화면으로 밀어 보내는 사건. */
public record StreamEvent(String kind, Object payload) {

    public static StreamEvent fill(Object p) {
        return new StreamEvent("fill", p);
    }

    /** 주문 하나의 요청과 원장의 답. */
    public static StreamEvent order(Object p) {
        return new StreamEvent("order", p);
    }

    /** 원장에서 다시 읽은 주문 상태가 바뀌었다(T7-03). payload는 주문 상세. */
    public static StreamEvent orderUpdate(Object p) {
        return new StreamEvent("order-update", p);
    }

    /** 잔고가 바뀌었다(T7-03). */
    public static StreamEvent balance(Object p) {
        return new StreamEvent("balance", p);
    }

    /**
     * 바깥 시장에서 일어난 체결(Phase 8). 내 주문의 체결({@link #fill})과 <b>다르다</b> —
     * 이쪽은 시장 전체의 거래이고 내 계좌와 무관하다.
     */
    public static StreamEvent trade(Object p) {
        return new StreamEvent("trade", p);
    }

    public static StreamEvent book(Object p) {
        return new StreamEvent("book", p);
    }

    /** 원장이 끊겼다. 화면이 조용히 멈추지 않게 알린다. */
    public static StreamEvent ledgerDown(String why) {
        return new StreamEvent("ledger-down", why);
    }

    public static StreamEvent ledgerUp() {
        return new StreamEvent("ledger-up", "연결됨");
    }
}
