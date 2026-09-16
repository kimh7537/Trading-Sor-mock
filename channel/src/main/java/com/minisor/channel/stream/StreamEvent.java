package com.minisor.channel.stream;

/** 화면으로 밀어 보내는 사건. */
public record StreamEvent(String kind, Object payload) {

    public static StreamEvent fill(Object p) {
        return new StreamEvent("fill", p);
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
