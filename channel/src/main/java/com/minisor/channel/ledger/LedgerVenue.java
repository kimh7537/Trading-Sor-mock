package com.minisor.channel.ledger;

import org.springframework.stereotype.Component;

/**
 * 원장의 매칭 엔진이 <b>어디에 있는가</b> (T12-05).
 *
 * <p>기본 구성에서는 SOR과 매칭 엔진이 원장 프로세스 <b>안에서</b> 돈다(T6-03 "최소
 * 연결"). {@code ledgerd --exchange}를 주면 거래소가 별도 프로세스가 되고 그 사이를
 * FEP가 잇는다.
 *
 * <h4>왜 채널계가 이것을 들고 있나</h4>
 *
 * <p>화면의 통신 흐름 구조도가 <b>지나는 홉만 또렷하게</b> 그린다. 그 원칙을 지키려면
 * 화면이 지금 어느 구성인지 알아야 하는데, 화면도 채널계도 원장 안을 볼 수 없다.
 * 그래서 <b>원장이 직접 말해 준다</b> — {@code SYMBOL_ACK}의 마지막 바이트다.
 *
 * <p>설정 파일에 적어 두지 않는 이유는 그러면 <b>진실이 두 군데</b>가 되기 때문이다.
 * 원장을 {@code --exchange} 없이 띄우고 설정만 켜 두면 화면이 지나지 않는 길을
 * 지난다고 그린다.
 *
 * <p>모르는 상태({@link #UNKNOWN})를 따로 둔다. 아직 한 번도 못 물어본 것과 "안에서
 * 돈다"는 다르다 — 모르는 것을 0으로 치면 그것도 지어낸 값이다.
 */
@Component
public class LedgerVenue {

    public static final int UNKNOWN = -1;
    /** 원장 프로세스 안. {@code msg.h}의 {@code MSG_VENUE_LOCAL} */
    public static final int LOCAL = 0;
    /** 별도 거래소 프로세스. {@code msg.h}의 {@code MSG_VENUE_REMOTE} */
    public static final int REMOTE = 1;

    private volatile int venue = UNKNOWN;

    /** 원장이 답한 값을 그대로 적는다. {@code SYMBOL_ACK}을 받은 자리에서 부른다. */
    public void set(int value) {
        venue = (value == REMOTE) ? REMOTE : LOCAL;
    }

    public int venue() {
        return venue;
    }

    public boolean remote() {
        return venue == REMOTE;
    }
}
