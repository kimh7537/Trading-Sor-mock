package com.minisor.channel.feed;

import jakarta.validation.constraints.Max;
import jakarta.validation.constraints.Min;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.validation.annotation.Validated;

/**
 * 실시세 모드 설정 (T8-04).
 *
 * <p><b>기본은 꺼짐이다.</b> 켜면 바깥(토스증권 Open API)에 붙어 실호가를 받고, 그것을
 * {@code MSG_BOOK_FEED}로 원장 호가창에 심는다. 시뮬 모드는 이 설정과 무관하게 그대로 돈다 —
 * 두 모드가 같은 원장·같은 전문·같은 매칭 엔진을 쓴다.
 *
 * <p><b>키는 {@code .env}에서 읽는다.</b> {@code application.properties}가
 * {@code spring.config.import}로 저장소 루트의 {@code .env}를 끌어오고, 그 파일은
 * {@code .gitignore}에 있다. 값이 비어 있으면 {@link #usable()}이 거짓이라 붙지 않는다 —
 * 키 없이 뜬 채 403을 되풀이하는 것보다 아예 안 붙는 편이 낫다.
 *
 * <p><b>왜 시장이 하나인가.</b> 토스 오픈 API의 국내 호가는 통합 시세(KRX+NXT)만 준다.
 * 스펙 수준에서 확인된다 — {@code GET /api/v1/orderbook}의 파라미터는 {@code symbol} 하나뿐이고
 * 시장을 고르는 인자가 없다. 그래서 실시세는 한 시장 호가창에만 심는다. 두 시장·SOR 배분·전략
 * 비교는 시뮬 모드의 몫이다.
 *
 * @param enabled 실시세 수신을 켤 것인가. 기본 꺼짐
 * @param market 실호가를 심을 시장 번호(0 KRX, 1 NXT)
 * @param symbol 구독할 종목. 원장이 다루는 종목과 같아야 한다
 * @param wsUrl 실시간 구독 주소
 * @param baseUrl 토큰·호가 REST 주소
 * @param clientId OAuth client id. 비어 있으면 붙지 않는다
 * @param clientSecret OAuth client secret
 * @param recordFile 받은 스냅샷을 적어 둘 파일. 비어 있으면 적지 않는다(T8-06)
 * @param replayFile 재생할 파일. 비어 있으면 재생하지 않는다(T8-06)
 * @param replaySpeed 재생 배속. 1이면 실시간, 10이면 10배 빠르게
 */
@Validated
@ConfigurationProperties(prefix = "minisor.feed")
public record FeedProperties(
        boolean enabled,
        /*
         * 설정이 틀렸으면 뜨는 순간 죽는 편이 낫다. 모르는 시장 번호를 그대로 두면 원장이
         * 스냅샷을 조용히 버리고, 화면은 "실시세"라고 적힌 채 아무것도 움직이지 않는다.
         */
        @Min(0) @Max(1) int market,
        String symbol,
        String wsUrl,
        String baseUrl,
        String clientId,
        String clientSecret,
        String recordFile,
        String replayFile,
        double replaySpeed) {

    /** 붙을 수 있는가 — 켜져 있고 키가 있다. */
    public boolean usable() {
        return enabled && hasKeys() && notBlank(wsUrl);
    }

    /**
     * 바깥에 <b>물어볼</b> 수 있는가.
     *
     * <p>실시세 수신({@link #usable()})과 다르다. 종목을 찾고 그 종목의 현재가를 받는 일은
     * 실시세를 켜지 않아도 된다 — 시뮬 모드에서 종목을 바꿀 때도 그 종목의 가격대가 필요하다.
     */
    public boolean hasKeys() {
        return notBlank(clientId) && notBlank(clientSecret) && notBlank(baseUrl);
    }

    public boolean recording() {
        return notBlank(recordFile);
    }

    public boolean replaying() {
        return notBlank(replayFile);
    }

    private static boolean notBlank(String s) {
        return s != null && !s.isBlank();
    }
}
