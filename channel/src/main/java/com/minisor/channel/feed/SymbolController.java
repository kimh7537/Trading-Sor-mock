package com.minisor.channel.feed;

import com.minisor.channel.ledger.LedgerException;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;
import java.io.IOException;
import java.util.List;
import java.util.Map;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.validation.annotation.Validated;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/**
 * 종목 찾기·전환 (T8-10).
 *
 * <ul>
 *   <li>{@code GET /api/stocks?q=삼성} — 국내 보통주를 이름·코드로 찾는다
 *   <li>{@code GET /api/symbol} — 지금 보고 있는 종목
 *   <li>{@code POST /api/symbol} — 바꾼다. <b>원장이 초기화된다</b>
 * </ul>
 *
 * <p>바꾸는 일은 상태를 바꾸므로 POST다. 실패 이유는 문장으로 돌려준다 — 화면이 그대로
 * 보여 줘야 사용자가 무엇을 해야 할지 안다.
 */
@RestController
@Validated
public class SymbolController {

    public record SwitchRequest(@NotBlank @Size(min = 1, max = 8) String symbol) {}

    private final SymbolService service;
    private final SymbolState state;

    public SymbolController(SymbolService service, SymbolState state) {
        this.service = service;
        this.state = state;
    }

    @GetMapping("/api/symbol")
    public SymbolState.Current current() {
        return state.current();
    }

    @GetMapping("/api/stocks")
    public ResponseEntity<Object> search(@RequestParam(defaultValue = "") String q) {
        /*
         * 실시세 설정이 없어도 막지 않는다(T10-02). 미국 종목은 내장 목록이라
         * 시세 없이도 고를 수 있다 — 국내만 빈 목록이 된다.
         */
        if (q.length() > 40) {
            return ResponseEntity.badRequest().body(Map.of("error", "검색어가 너무 길다"));
        }
        try {
            List<TossStocks.Stock> hits = service.search(q);
            return ResponseEntity.ok(Map.of("stocks", hits));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        } catch (IOException | RuntimeException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE)
                    .body(Map.of("error", "종목 목록을 받지 못했다: " + e.getMessage()));
        }
    }

    @PostMapping("/api/symbol")
    public ResponseEntity<Object> switchTo(@RequestBody SwitchRequest req) {
        try {
            return ResponseEntity.ok(service.switchTo(req.symbol()));
        } catch (SymbolService.SwitchFailed e) {
            return ResponseEntity.status(HttpStatus.CONFLICT).body(Map.of("error", e.getMessage()));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        } catch (LedgerException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE)
                    .body(Map.of("error", "원장에 닿지 못했다: " + e.getMessage()));
        } catch (IOException | RuntimeException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE)
                    .body(Map.of("error", "종목을 바꾸지 못했다: " + e.getMessage()));
        }
    }
}
