package com.minisor.channel.api;

import com.minisor.channel.ledger.LedgerException;
import jakarta.validation.Valid;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

/**
 * 주문 REST 입구.
 *
 * <p>상태 코드를 뭉개지 않는다 — 부르는 쪽은 "고쳐서 다시"인지 "기다렸다 다시"인지
 * 알아야 한다.
 *
 * <ul>
 *   <li>400 형식 오류 — 원장까지 가지 않았다
 *   <li>422 원장이 거절 — 그대로 다시 보내도 또 거절된다
 *   <li>503 원장에 못 붙음 — 주문은 확실히 나가지 않았다
 *   <li>202 응답 없음 — <b>모른다.</b> 다시 보내면 중복 주문이 될 수 있어
 *       조회로 확인해야 한다
 * </ul>
 */
@RestController
@RequestMapping("/api/orders")
public class OrderController {

    private final OrderService service;

    public OrderController(OrderService service) {
        this.service = service;
    }

    @PostMapping
    public ResponseEntity<OrderResponseDto> submit(@Valid @RequestBody OrderRequestDto req) {
        OrderResponseDto res;
        try {
            res = service.submit(req);
        } catch (LedgerException e) {
            /*
             * 붙지 못했다 = **주문이 나가지 않은 것이 확실하다.** 모호하지
             * 않으므로 503으로 분명히 말한다.
             */
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE)
                    .body(OrderResponseDto.rejected(req.clOrdId(), -16, e.getMessage()));
        }

        return switch (res.outcome()) {
            case ACCEPTED -> ResponseEntity.ok(res);
            case REJECTED -> ResponseEntity.unprocessableEntity().body(res);
            case IN_DOUBT -> ResponseEntity.accepted().body(res);
        };
    }
}
