package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 가상 참가자 스위치 (1바이트, 점검).
 *
 * <p><b>실시세 모드에서는 꺼야 한다.</b> 원장이 틱을 건너뛰는 기준은 "스냅샷을 받은
 * 시장"인데({@code fed[]}), 그 표시는 <b>코어에 딸려 있어</b> 종목을 바꿔 코어를
 * 새로 만들면 지워진다. 장이 닫혀 새 스냅샷이 오지 않으면 다시 세워지지도 않아,
 * 실시세 모드인데 호가창이 혼자 걸어간다. 실제로 그렇게 됐다.
 *
 * <p>이 스위치는 <b>데몬이 들고 있어</b> 코어를 몇 번 갈아끼우든 살아남는다.
 */
@WireMessage(type = 26, name = "TICK_SET")
public final class TickSet {

    /** 0이면 가상 참가자를 멈춘다(실시세 모드). */
    @WireField(order = 1, type = U8)
    public int on;
}
