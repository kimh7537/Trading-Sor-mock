package com.minisor.channel.api;

import static com.minisor.channel.wire.WireEnums.MARKET_KRX;
import static com.minisor.channel.wire.WireEnums.MARKET_NXT;

import com.minisor.channel.ledger.LedgerConnection;
import com.minisor.channel.ledger.LedgerConnectionPool;
import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.wire.BookAck;
import com.minisor.channel.wire.BookReq;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/**
 * 원장 안의 실제 호가창(T6-04). {@code GET /api/book?market=0|1}.
 *
 * <p>조회는 아무것도 바꾸지 않으므로 답을 못 받아도 모호하지 않다 — 주문과 달리
 * 202가 아니라 503이다. 다시 불러도 된다.
 */
@RestController
public class BookController {

    public record Level(int price, int qty) {}

    public record BookDto(String symbol, int market, List<Level> bids, List<Level> asks) {}

    private final LedgerConnectionPool pool;

    /** 전문에 실을 논리 시각. 시스템 시각을 읽지 않는다(OrderService와 같다). */
    private final AtomicLong logicalClock = new AtomicLong(1);

    public BookController(LedgerConnectionPool pool) {
        this.pool = pool;
    }

    @GetMapping("/api/book")
    public ResponseEntity<BookDto> book(
            @RequestParam int market, @RequestParam(defaultValue = "005930") String symbol) {
        if ((market != MARKET_KRX && market != MARKET_NXT) || symbol.isEmpty() || symbol.length() > 8) {
            return ResponseEntity.badRequest().build();
        }

        BookReq req = new BookReq();
        req.symbol = symbol;
        req.market = market;

        BookAck ack;
        LedgerConnection c = null;
        try {
            c = pool.borrow();
            ack = c.call(req, BookAck.class, logicalClock.getAndIncrement());
        } catch (LedgerException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        } finally {
            if (c != null) {
                pool.release(c);
            }
        }

        return ResponseEntity.ok(
                new BookDto(
                        ack.symbol,
                        ack.market,
                        levels(ack.bidPrice, ack.bidQty),
                        levels(ack.askPrice, ack.askQty)));
    }

    /** 없는 단(가격 0)은 싣지 않는다. 원장은 앞에서부터 채우므로 첫 0에서 끝난다. */
    private static List<Level> levels(int[] price, int[] qty) {
        List<Level> out = new ArrayList<>();
        for (int i = 0; i < price.length && price[i] != 0; i++) {
            out.add(new Level(price[i], qty[i]));
        }
        return out;
    }
}
