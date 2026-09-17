package com.minisor.channel.api;

import com.minisor.channel.ledger.LedgerException;
import com.minisor.channel.wire.BalanceAck;
import com.minisor.channel.wire.BalanceReq;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RestController;

/** 데모 계좌의 잔고(T7-03). {@code GET /api/balance}. */
@RestController
public class BalanceController {

    /**
     * @param available 주문에 쓸 수 있는 돈 = 예수금 - 묶인 금액
     */
    public record BalanceDto(String account, long cash, long reserved, long available) {

        static BalanceDto from(BalanceAck ack) {
            return new BalanceDto(ack.account, ack.cash, ack.reserved, ack.cash - ack.reserved);
        }
    }

    private final LedgerGateway gateway;
    private final String account;

    public BalanceController(LedgerGateway gateway, @Value("${minisor.account}") String account) {
        this.gateway = gateway;
        this.account = account;
    }

    @GetMapping("/api/balance")
    public ResponseEntity<BalanceDto> balance() {
        try {
            BalanceAck ack = fetch(gateway, account);
            return ack.reason == 0
                    ? ResponseEntity.ok(BalanceDto.from(ack))
                    : ResponseEntity.notFound().build();
        } catch (LedgerException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        }
    }

    static BalanceAck fetch(LedgerGateway gateway, String account) {
        BalanceReq req = new BalanceReq();
        req.account = account;
        return gateway.call(req, BalanceAck.class);
    }
}
