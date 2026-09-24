package com.minisor.channel.feed;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

/**
 * 지금 다루는 종목 (T8-10).
 *
 * <p>종목은 <b>한 번에 하나다.</b> 원장 호가창이 하나이고, 그 호가창은 만들 때 정한
 * 기준가 ±30%만 펼쳐 두기 때문이다. 종목을 바꾸는 일은 원장을 새로 여는 일이라
 * ({@link SymbolService}) 가볍지 않다 — 그래서 "지금 무엇을 보고 있나"를 한 곳에 둔다.
 *
 * <p>설정({@code minisor.symbol})이 처음 값이고, 그 뒤로는 사용자가 고른 것이다.
 */
@Service
public class SymbolState {

    /**
     * 지금 종목. {@code refPrice}가 0이면 아직 바깥에서 값을 받은 적이 없다.
     *
     * <p>{@code kind}는 0=국내, 1=미국이다(T10-02). <b>가격의 뜻이 여기서 갈린다</b> —
     * 국내는 원, 미국은 센트다. 화면이 100으로 나눠 달러로 보여 준다.
     */
    public record Current(String code, String name, int refPrice, int kind) {
        public static final int KR = 0;
        public static final int US = 1;

        public boolean us() {
            return kind == US;
        }

        /** 화면과 API가 쓰는 통화 코드. */
        public String currency() {
            return us() ? "USD" : "KRW";
        }
    }

    private volatile Current now;

    public SymbolState(
            @Value("${minisor.symbol:005930}") String code,
            @Value("${minisor.symbol-name:삼성전자}") String name) {
        this.now = new Current(code, name, 0, Current.KR);
    }

    public String code() {
        return now.code();
    }

    public Current current() {
        return now;
    }

    public void set(String code, String name, int refPrice, int kind) {
        this.now = new Current(code, name, refPrice, kind);
    }

    /** 종류를 바꾸지 않고 값만 고칠 때. */
    public void set(String code, String name, int refPrice) {
        set(code, name, refPrice, now.kind());
    }

    public boolean us() {
        return now.us();
    }
}
