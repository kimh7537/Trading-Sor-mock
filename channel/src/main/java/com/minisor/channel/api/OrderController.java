package com.minisor.channel.api;

import com.minisor.channel.auth.CurrentAccount;
import com.minisor.channel.ledger.LedgerException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.validation.Valid;
import java.util.List;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
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

    /** 이 채널계가 낸 주문과 마지막으로 본 상태, 최근 것부터(T7-03). */
    @GetMapping
    public List<OrderView> list(HttpServletRequest http) {
        return service.orders(CurrentAccount.required(http));
    }

    /** 주문 하나의 지금 상태를 원장에서 읽는다. 없거나 남의 주문이면 404. */
    @GetMapping("/{orderId}")
    public ResponseEntity<OrderView> detail(@PathVariable long orderId,
            HttpServletRequest http) {
        try {
            OrderView v = service.detail(CurrentAccount.required(http), orderId);
            return v == null ? ResponseEntity.notFound().build() : ResponseEntity.ok(v);
        } catch (LedgerException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        }
    }

    /**
     * 취소(T7-03).
     *
     * <ul>
     *   <li>200 취소됐다 — {@code canceledQty}만큼
     *   <li>409 이 채널계가 낸 주문인데 취소할 잔량이 없다(이미 체결·취소로 끝남)
     *   <li>404 모르는 주문
     *   <li>503 원장에 못 붙음. <b>취소는 다시 보내도 안전하다</b> — 두 번째는 "잔량 없음"일 뿐이다
     * </ul>
     */
    @DeleteMapping("/{orderId}")
    public ResponseEntity<OrderService.CancelResult> cancel(@PathVariable long orderId,
            HttpServletRequest http) {
        String account = CurrentAccount.required(http);
        OrderService.CancelResult r;
        try {
            r = service.cancel(account, orderId);
        } catch (LedgerException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        }
        if (r.reason() == 0) {
            return ResponseEntity.ok(r);
        }
        if (r.reason() == OrderService.ERR_NOT_FOUND) {
            return service.knows(account, orderId)
                    ? ResponseEntity.status(HttpStatus.CONFLICT).body(r)
                    : ResponseEntity.status(HttpStatus.NOT_FOUND).body(r);
        }
        return ResponseEntity.unprocessableEntity().body(r);
    }

    @PostMapping
    public ResponseEntity<OrderResponseDto> submit(@Valid @RequestBody OrderRequestDto req,
            HttpServletRequest http) {
        OrderResponseDto res;
        try {
            res = service.submit(CurrentAccount.required(http), req);
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
