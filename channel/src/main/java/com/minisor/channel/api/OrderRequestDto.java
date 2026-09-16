package com.minisor.channel.api;

import jakarta.validation.constraints.Max;
import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;

/**
 * 주문 접수 요청.
 *
 * <p>형식만 봐도 아는 잘못은 여기서 끝낸다 — 원장까지 왕복한 뒤 거절되면 느리다.
 * 증거금·한도처럼 계좌 상태를 봐야 아는 것은 원장(T3-07)이 판단한다.
 */
public record OrderRequestDto(
        @NotBlank @Size(min = 12, max = 12) String account,
        @NotBlank @Size(min = 1, max = 8) String symbol,
        @Min(1) long clOrdId,
        @Min(1) @Max(2) int side,
        @Min(1) @Max(5) int type,
        @Min(1) @Max(2) int market,
        @Min(1) int price,
        @Min(1) int qty) {}
