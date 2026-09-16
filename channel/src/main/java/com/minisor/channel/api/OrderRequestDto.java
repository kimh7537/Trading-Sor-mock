package com.minisor.channel.api;

import static com.minisor.channel.wire.WireEnums.*;

import jakarta.validation.constraints.AssertTrue;
import jakarta.validation.constraints.Max;
import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;

/**
 * 주문 접수 요청.
 *
 * <p>형식만 봐도 아는 잘못은 여기서 끝낸다 — 원장까지 왕복한 뒤 거절되면 느리다.
 * 증거금·한도처럼 계좌 상태를 봐야 아는 것은 원장(T3-07)이 판단한다.
 *
 * <p><b>범위를 숫자로 적지 않고 {@link com.minisor.channel.wire.WireEnums}의 상수로
 * 적는다.</b> 처음에 {@code @Min(1) @Max(2)}로 적었다가 C(0부터 셈)와 한 칸씩
 * 어긋났다(T6-01). 상수는 C 헤더와 테스트로 대조되므로 여기가 다시 따로 놀 수 없다.
 */
public record OrderRequestDto(
        @NotBlank @Size(min = 12, max = 12) String account,
        @NotBlank @Size(min = 1, max = 8) String symbol,
        @Min(1) long clOrdId,
        @Min(SIDE_BUY) @Max(SIDE_SELL) int side,
        @Min(ORDER_LIMIT) @Max(ORDER_MIDPOINT) int type,
        int market,
        @Min(1) int price,
        @Min(1) int qty) {

    /**
     * 시장은 KRX·NXT, 또는 원장이 정하는 자동(SOR, 255). 연속 구간이 아니라
     * {@code @Min/@Max}로 적을 수 없다.
     */
    @AssertTrue(message = "market은 0(KRX), 1(NXT), 255(자동)만 된다")
    public boolean isMarketKnown() {
        return market == MARKET_KRX || market == MARKET_NXT || market == MSG_MARKET_AUTO;
    }
}
