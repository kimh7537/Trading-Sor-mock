package com.minisor.channel.feed;

import java.util.List;
import java.util.Locale;
import org.springframework.stereotype.Component;

/**
 * 미국 종목 목록 (T10-02).
 *
 * <p><b>실시세가 없다.</b> 토스증권 Open API의 구독 토픽은 {@code orderbook:kr}·
 * {@code trade:kr}로 국내만 준다. 그래서 미국 종목은 <b>시작 가격만</b> 주고
 * 그다음은 가상 참가자가 호가창을 움직인다 — 시뮬 모드와 같은 방식이다.
 *
 * <p>여기 적힌 가격은 <b>호가창을 열 자리를 정하는 씨앗일 뿐 시세가 아니다.</b>
 * 대략적인 값이고 정확할 이유도 없다. 호가창은 이 값의 ±30%를 펼치므로 자릿수만
 * 맞으면 거래가 성립한다.
 *
 * <p>가격은 모두 <b>센트</b>다. {@code price_t}가 정수라 소수점을 담을 수 없어
 * 1센트를 1로 센다({@code core/include/tick_size.h}).
 */
@Component
public class UsStocks {

    /** 종목 하나. {@code seedCents}는 호가창을 열 기준가다. */
    public record Stock(String symbol, String name, int seedCents) {}

    private static final List<Stock> ALL = List.of(
            new Stock("AAPL", "Apple", 25500),
            new Stock("MSFT", "Microsoft", 51000),
            new Stock("NVDA", "NVIDIA", 18500),
            new Stock("AMZN", "Amazon", 22800),
            new Stock("GOOGL", "Alphabet", 24500),
            new Stock("META", "Meta Platforms", 73000),
            new Stock("TSLA", "Tesla", 42000));

    /** 미국 종목 코드로 보이는가. 국내는 숫자 6자리라 겹치지 않는다. */
    public static boolean looksUs(String code) {
        return code != null && code.matches("[A-Za-z]{1,8}");
    }

    public Stock find(String symbol) {
        if (symbol == null) {
            return null;
        }
        String up = symbol.toUpperCase(Locale.ROOT);
        return ALL.stream().filter(s -> s.symbol().equals(up)).findFirst().orElse(null);
    }

    /** 코드나 이름에 질의가 들어간 것. 대소문자를 가리지 않는다. */
    public List<Stock> search(String q) {
        if (q == null || q.isBlank()) {
            return List.of();
        }
        String needle = q.trim().toLowerCase(Locale.ROOT);
        return ALL.stream()
                .filter(s -> s.symbol().toLowerCase(Locale.ROOT).contains(needle)
                        || s.name().toLowerCase(Locale.ROOT).contains(needle))
                .toList();
    }

    public List<Stock> all() {
        return ALL;
    }
}
