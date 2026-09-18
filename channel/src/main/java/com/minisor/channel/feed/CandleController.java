package com.minisor.channel.feed;

import java.io.IOException;
import java.util.Set;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/**
 * 캔들 차트 — {@code GET /api/candles?interval=1m|1d&count=N}.
 *
 * <p>화면이 MTS처럼 1분봉·일봉을 그리는 데 쓴다. <b>바깥 시세</b>이고 이 프로젝트의 원장
 * 호가창과는 별개다 — 화면에도 그렇게 적는다.
 *
 * <p>토스 설정이 없으면 409. 조회는 아무것도 바꾸지 않으므로 다시 불러도 안전하다.
 */
@RestController
public class CandleController {

    /** 토스가 받는 값만 통과시킨다. 그 밖의 값은 400 — 바깥에 그대로 넘기지 않는다. */
    private static final Set<String> INTERVALS = Set.of("1m", "1d");

    private static final int MAX_COUNT = 200;

    private final TossCandles candles;

    public CandleController(TossCandles candles) {
        this.candles = candles;
    }

    @GetMapping("/api/candles")
    public ResponseEntity<TossCandles.Chart> chart(
            @RequestParam(defaultValue = "1m") String interval,
            @RequestParam(defaultValue = "120") int count) {

        if (!INTERVALS.contains(interval) || count < 1 || count > MAX_COUNT) {
            return ResponseEntity.badRequest().build();
        }
        if (!candles.usable()) {
            /* 키가 없으면 봉을 만들 길이 없다. 화면은 이때 중간가 선으로 되돌아간다 */
            return ResponseEntity.status(HttpStatus.CONFLICT).build();
        }
        try {
            return ResponseEntity.ok(candles.fetch(interval, count));
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        } catch (IOException | RuntimeException e) {
            return ResponseEntity.status(HttpStatus.SERVICE_UNAVAILABLE).build();
        }
    }
}
