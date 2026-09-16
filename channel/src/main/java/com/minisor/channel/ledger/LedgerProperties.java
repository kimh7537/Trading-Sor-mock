package com.minisor.channel.ledger;

import jakarta.validation.constraints.Max;
import jakarta.validation.constraints.Min;
import jakarta.validation.constraints.NotBlank;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.validation.annotation.Validated;

/**
 * 원장(C) 접속 설정.
 *
 * <p>코드에 박지 않는 이유는 원장이 <b>포트 0으로도 뜨기 때문</b>이다(T3-03).
 * 시험할 때마다 커널이 다른 포트를 주므로, 박아 두면 테스트에서 갈아끼울 수가
 * 없다.
 *
 * <p>{@code record}로 둔 것은 <b>뜬 뒤에 바뀌지 않는 값</b>이기 때문이다.
 * setter가 있으면 누군가 실행 중에 바꿀 수 있고, 그러면 "지금 어디에 붙어
 * 있는가"의 답이 시점에 따라 달라진다.
 *
 * <p>검증을 붙인 이유: 설정이 틀렸으면 <b>뜨는 순간 죽는 편이 낫다.</b>
 * 잘못된 포트로 뜬 채 첫 주문에서야 실패하면, 그때는 이미 사용자가 주문을
 * 낸 뒤다.
 *
 * <p>포트 0은 "아직 정하지 않았다"는 뜻으로 쓴다. 기본값이 0이므로 설정을
 * 빠뜨린 채 뜬 프로세스는 붙을 곳이 없다 — 엉뚱한 곳에 붙는 것보다 낫다.
 */
@Validated
@ConfigurationProperties(prefix = "minisor.ledger")
public record LedgerProperties(
        @NotBlank String host,
        @Min(0) @Max(65535) int port,
        @Min(1) int connectTimeoutMs,
        @Min(1) int readTimeoutMs) {

    /** 붙을 곳이 정해져 있는가. */
    public boolean isConfigured() {
        return port > 0;
    }
}
