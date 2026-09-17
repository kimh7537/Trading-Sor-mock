package com.minisor.channel.api;

import static com.minisor.channel.wire.WireEnums.MARKET_KRX;
import static com.minisor.channel.wire.WireEnums.MARKET_NXT;

import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.wire.BookAck;
import com.minisor.channel.wire.BookReq;
import java.util.ArrayList;
import java.util.List;
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
 *
 * <p>호가 변화는 {@link LedgerPoller}가 읽어 밀어 보내기도 한다(T7-03). 이 API는 화면이 처음 뜰 때와
 * 방송이 끊겼을 때 쓴다.
 */
@RestController
public class BookController {

    public record Level(int price, int qty) {}

    public record BookDto(String symbol, int market, List<Level> bids, List<Level> asks) {

        static BookDto from(BookAck ack) {
            return new BookDto(
                    ack.symbol,
                    ack.market,
                    levels(ack.bidPrice, ack.bidQty),
                    levels(ack.askPrice, ack.askQty));
        }
    }

    private final LedgerGateway gateway;

    public BookController(LedgerGateway gateway) {
        this.gateway = gateway;
    }

    @GetMapping("/api/book")
    public ResponseEntity<BookDto> book(
            @RequestParam int market, @RequestParam(defaultValue = "005930") String symbol) {
        if ((market != MARKET_KRX && market != MARKET_NXT) || symbol.isEmpty() || symbol.length() > 8) {
            return ResponseEntity.badRequest().build();
        }
        try {
            return ResponseEntity.ok(BookDto.from(fetch(gateway, symbol, market)));
        } catch (LedgerException e) {
            /* 원장 끊김 알림은 게이트웨이가 했다 */
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        }
    }

    static BookAck fetch(LedgerGateway gateway, String symbol, int market) {
        BookReq req = new BookReq();
        req.symbol = symbol;
        req.market = market;
        return gateway.call(req, BookAck.class);
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
