package com.minisor.channel.api;

import com.minisor.channel.auth.CurrentAccount;
import com.minisor.channel.feed.SymbolState;
import com.minisor.channel.store.FillStore;
import com.minisor.channel.store.Portfolio;
import jakarta.servlet.http.HttpServletRequest;
import java.util.List;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/**
 * 거래 내역과 수익률 (T11-03).
 *
 * <p><b>컴퓨터를 꺼도 남는 것과 안 남는 것.</b> 체결 기록은 파일에 적히므로 전원과
 * 무관하게 남는다. 실현 손익도 그 기록에서 계산하므로 남는다. 다만 <b>평가 손익은
 * 마지막으로 본 시세 기준</b>이다 — 꺼져 있는 동안 시세를 받아 오는 프로세스가 없다.
 * 미체결 주문은 되살리지 않는다(그때 그 주문은 어느 시장에도 없었다).
 */
@RestController
public class PortfolioController {

    /** 화면이 그리는 데 필요한 것 전부. 금액은 정수 — 국내는 원, 미국은 센트다. */
    public record PortfolioDto(
            String account,
            String symbol,
            String currency,
            long qty,
            long avgCost,
            long cost,
            /** 지금 시세. 0이면 아직 호가를 본 적이 없다 */
            long lastPrice,
            long marketValue,
            long unrealized,
            long realized,
            long cash,
            long equity,
            long seedCash,
            double returnRate,
            int fills) {}

    private final Portfolio portfolio;
    private final FillStore fills;
    private final SymbolState symbols;
    private final LedgerGateway gateway;
    private final long seedKr;
    private final long seedUs;

    public PortfolioController(
            Portfolio portfolio,
            FillStore fills,
            SymbolState symbols,
            LedgerGateway gateway,
            @Value("${minisor.auth.signup-cash:100000000}") long seedKr,
            @Value("${minisor.auth.signup-cash-us:10000000}") long seedUs) {
        this.portfolio = portfolio;
        this.fills = fills;
        this.symbols = symbols;
        this.gateway = gateway;
        this.seedKr = seedKr;
        this.seedUs = seedUs;
    }

    @GetMapping("/api/portfolio")
    public PortfolioDto portfolio(HttpServletRequest http) {
        String account = CurrentAccount.required(http);
        SymbolState.Current now = symbols.current();
        long seed = now.us() ? seedUs : seedKr;

        Portfolio.Snapshot s = portfolio.of(account, now.code(), now.kind(), seed);
        long price = lastPrice(now.code());

        return new PortfolioDto(
                account,
                now.code(),
                now.currency(),
                s.qty(),
                s.avgCost(),
                s.cost(),
                price,
                s.marketValue(price),
                s.unrealized(price),
                s.realized(),
                s.cash(),
                s.equity(price),
                seed,
                s.returnRate(price, seed),
                s.fills());
    }

    /** 체결 내역, 최근 것부터. */
    @GetMapping("/api/history")
    public List<FillStore.Fill> history(
            @RequestParam(defaultValue = "100") int limit, HttpServletRequest http) {
        return fills.recent(CurrentAccount.required(http), limit);
    }

    /**
     * 평가에 쓸 시세 — 두 시장의 최우선 매수호가 중 높은 쪽.
     *
     * <p>팔면 받을 값이라 매수호가로 친다. 원장에 못 붙으면 0이고, 그때 평가 손익은
     * 0으로 둔다 — 모르는 값을 0원으로 치는 것보다 "모른다"가 낫다.
     */
    private long lastPrice(String symbol) {
        long best = 0;
        for (int market = 0; market <= 1; market++) {
            try {
                var ack = BookController.fetch(gateway, symbol, market);
                if (ack.bidPrice != null && ack.bidPrice.length > 0) {
                    best = Math.max(best, ack.bidPrice[0]);
                }
            } catch (RuntimeException e) {
                return 0;
            }
        }
        return best;
    }
}
