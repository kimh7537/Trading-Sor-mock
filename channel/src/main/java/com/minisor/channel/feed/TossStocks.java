package com.minisor.channel.feed;

import java.io.IOException;
import java.math.BigDecimal;
import java.net.URI;
import java.net.URLEncoder;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;

/**
 * 종목 찾기 (T8-10). 토스증권의 종목 목록·현재가를 읽는다.
 *
 * <h2>검색은 이쪽에서 한다</h2>
 *
 * 토스 오픈 API에 <b>이름으로 찾는 엔드포인트가 없다.</b> 있는 것은 마켓별 전체 목록
 * ({@code GET /api/v1/stocks/all})뿐이다. 그래서 목록을 한 번 받아 두고 이름·코드로
 * 거른다. 목록은 하루에 한 번 바뀌는 값이라 {@link #LIST_TTL_MS} 동안 다시 받지 않는다.
 *
 * <h2>국내 보통주만 다룬다</h2>
 *
 * 이 프로젝트의 호가 단위·가격 제한폭은 한국 시장 규칙이고({@code docs/SPEC.md}) 가격은
 * <b>원 단위 정수</b>다. 달러로 매겨진 해외 종목을 그대로 넣으면 호가 단위가 맞지 않는다.
 * ETF·ETN을 빼는 이유는 다르다 — 없어도 되는 것을 늘리지 않는다.
 */
@Service
public class TossStocks {

    private static final Logger log = LoggerFactory.getLogger(TossStocks.class);
    private static final JsonMapper JSON = JsonMapper.builder().build();

    /** 종목 목록을 다시 받기까지. 상장·폐지는 하루 단위로 일어난다. */
    private static final long LIST_TTL_MS = 6 * 60 * 60 * 1000L;

    private static final int MAX_HITS = 20;

    /** 화면에 보이는 한 줄. */
    public record Stock(String symbol, String name, String market) {}

    private final FeedProperties props;
    private final TossTokenSource tokens;
    private final HttpClient http =
            HttpClient.newBuilder().connectTimeout(Duration.ofSeconds(5)).build();

    private volatile List<Stock> all = List.of();
    private volatile long loadedAt;

    public TossStocks(FeedProperties props, TossTokenSource tokens) {
        this.props = props;
        this.tokens = tokens;
    }

    /** 실시세를 켜지 않아도 종목은 찾을 수 있다 — 키만 있으면 된다. */
    public boolean usable() {
        return props.hasKeys();
    }

    /**
     * 이름이나 코드로 찾는다. 빈 검색어면 목록 앞쪽을 그대로 준다.
     *
     * <p>정확히 맞는 것, 코드로 시작하는 것, 이름으로 시작하는 것, 이름에 든 것 순으로 놓는다 —
     * "현대차"를 치면 현대차가 현대차증권보다 먼저 나오는 편이 낫다.
     */
    public List<Stock> search(String q) throws IOException, InterruptedException {
        List<Stock> list = list();
        String needle = q == null ? "" : q.trim().toUpperCase(Locale.ROOT);
        if (needle.isEmpty()) {
            return list.subList(0, Math.min(MAX_HITS, list.size()));
        }

        List<Stock> exact = new ArrayList<>();
        List<Stock> byCode = new ArrayList<>();
        List<Stock> byNameHead = new ArrayList<>();
        List<Stock> byNamePart = new ArrayList<>();
        for (Stock s : list) {
            String name = s.name().toUpperCase(Locale.ROOT);
            if (s.symbol().equals(needle) || name.equals(needle)) {
                /* "현대차"를 치면 현대차가 현대차증권보다 먼저다 */
                exact.add(s);
            } else if (s.symbol().startsWith(needle)) {
                byCode.add(s);
            } else if (name.startsWith(needle)) {
                byNameHead.add(s);
            } else if (name.contains(needle)) {
                byNamePart.add(s);
            }
        }

        List<Stock> out = new ArrayList<>(exact);
        out.addAll(byCode);
        out.addAll(byNameHead);
        out.addAll(byNamePart);
        return List.copyOf(out.subList(0, Math.min(MAX_HITS, out.size())));
    }

    /** 코드 하나의 정보. 목록에 없으면 null — 그때는 바꾸지 않는다. */
    public Stock find(String symbol) throws IOException, InterruptedException {
        for (Stock s : list()) {
            if (s.symbol().equals(symbol)) {
                return s;
            }
        }
        return null;
    }

    /**
     * 현재가(원). 받지 못하면 0.
     *
     * <p>장이 열리기 전에는 {@code lastPrice}가 없을 수 있다. 그때는 부르는 쪽이
     * 일봉의 종가로 물러선다({@link SymbolService}).
     */
    public int price(String symbol) throws IOException, InterruptedException {
        JsonNode arr = get("/api/v1/prices?symbols=" + enc(symbol)).path("result");
        for (JsonNode n : arr) {
            if (symbol.equals(n.path("symbol").asString())) {
                try {
                    return new BigDecimal(n.path("lastPrice").asString()).intValue();
                } catch (RuntimeException e) {
                    return 0;
                }
            }
        }
        return 0;
    }

    /** 국내 보통주 목록. 받아 둔 것이 아직 쓸 만하면 그대로 준다. */
    private List<Stock> list() throws IOException, InterruptedException {
        long now = System.currentTimeMillis();
        List<Stock> cached = all;
        if (!cached.isEmpty() && now - loadedAt < LIST_TTL_MS) {
            return cached;
        }

        List<Stock> out = new ArrayList<>();
        for (String market : new String[] {"KOSPI", "KOSDAQ"}) {
            JsonNode arr =
                    get(
                                    "/api/v1/stocks/all?market="
                                            + market
                                            + "&status=ACTIVE&securityType=STOCK&commonShare=true")
                            .path("result");
            for (JsonNode n : arr) {
                String symbol = n.path("symbol").asString();
                String name = n.path("name").asString();
                if (!symbol.isEmpty() && !name.isEmpty()) {
                    out.add(new Stock(symbol, name, market));
                }
            }
        }
        if (out.isEmpty()) {
            throw new IOException("종목 목록이 비어 있다");
        }

        all = List.copyOf(out);
        loadedAt = now;
        log.info("종목 목록 {}개를 받았다", out.size());
        return all;
    }

    /**
     * 한 번 받아 두면 여섯 시간을 쓰는 목록이라 <b>429는 기다렸다 다시 부른다.</b>
     *
     * <p>{@code /api/v1/stocks/all}은 하루 한 번 바뀌는 전체 목록이고 레이트리밋이 짜다.
     * 여기서 포기하면 검색창이 "받지 못했다"만 보여 주는데, 사용자가 할 수 있는 일은
     * 다시 누르는 것뿐이다 — 그 일을 이쪽에서 한다.
     */
    private JsonNode get(String path) throws IOException, InterruptedException {
        long wait = 0;
        for (int attempt = 0; ; attempt++) {
            if (wait > 0) {
                Thread.sleep(Math.min(wait, 5_000));
            }
            try {
                return getOnce(path);
            } catch (TossTokenSource.FeedBackoff b) {
                if (attempt >= 2) {
                    throw b;
                }
                wait = Math.max(b.waitMs(), 1_000);
                log.info("종목 조회 429 — {}ms 뒤 다시", wait);
            }
        }
    }

    private JsonNode getOnce(String path) throws IOException, InterruptedException {
        HttpRequest req =
                HttpRequest.newBuilder(URI.create(props.baseUrl() + path))
                        .header("Authorization", "Bearer " + tokens.token())
                        .header("Accept", "application/json")
                        .timeout(Duration.ofSeconds(15))
                        .GET()
                        .build();

        HttpResponse<byte[]> res = http.send(req, HttpResponse.BodyHandlers.ofByteArray());
        String body = TossTokenSource.text(res);
        if (res.statusCode() == 429) {
            throw new TossTokenSource.FeedBackoff(
                    TossTokenSource.retryAfterMs(res), "종목 조회 429");
        }
        if (res.statusCode() / 100 != 2) {
            /* 토큰이 죽었을 수 있다. 다음 호출이 새로 받게 한다 */
            tokens.invalidate();
            throw new IOException("종목 조회 실패 " + res.statusCode() + ": " + body);
        }
        return JSON.readTree(body);
    }

    private static String enc(String s) {
        return URLEncoder.encode(s, StandardCharsets.UTF_8);
    }
}
