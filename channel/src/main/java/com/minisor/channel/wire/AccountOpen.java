package com.minisor.channel.wire;

import static com.minisor.channel.wire.WireType.*;

/**
 * 계좌 개설 요청 (20바이트, T9-01).
 *
 * <p><b>다시 보내도 된다.</b> 이미 있는 계좌면 원장이 아무것도 바꾸지 않고 잔고만
 * 답한다. 원장은 메모리에만 있어서 껐다 켜면 계좌가 사라지는데, 채널계가 로그인마다
 * 이것을 보내 되살린다. 없는 계좌를 만들 때만 {@code cash}를 입금하므로 여러 번
 * 불러도 돈이 불어나지 않는다.
 */
@WireMessage(type = 24, name = "ACCTOPEN")
public final class AccountOpen {

    @WireField(order = 1, type = STR, length = 12)
    public String account;

    /** 처음 넣어 줄 돈. 이미 있는 계좌면 무시된다. */
    @WireField(order = 2, type = I64)
    public long cash;
}
