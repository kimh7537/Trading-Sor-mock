package com.minisor.channel.api;

import com.minisor.channel.ledger.LedgerVenue;
import java.util.List;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/**
 * 채널계 ↔ 원장 사이를 오간 전문 내역 (점검). {@code GET /api/wire}.
 *
 * <p>화면의 통신 모니터가 읽는다. 마지막으로 받은 번호를 {@code after}로 주면 그 뒤에
 * 적힌 것만 내려간다 — 매번 전부 내려보내면 몇 분 켜 두는 것만으로 응답이 수백 킬로바이트가
 * 된다.
 *
 * <p><b>계좌를 가리지 않는다.</b> 이 내역은 특정 사용자의 주문이 아니라 <b>이 채널계
 * 프로세스가 원장과 주고받은 것 전부</b>다 — 호가 조회와 주기 작업이 대부분이다. 다만
 * 주문 전문에는 계좌번호가 들어 있으므로 <b>로그인한 사람만</b> 읽게 막아 둔다
 * ({@code WebConfig}의 검사 목록). 혼자 쓰는 데모를 전제로 한 화면이다.
 */
@RestController
public class WireController {

    /** 한 번에 내려보낼 상한. 화면이 따라가지 못할 만큼 쌓였으면 최근 것만 준다. */
    private static final int MAX = 300;

    private final WireTap tap;
    private final LedgerVenue venue;

    public WireController(WireTap tap, LedgerVenue venue) {
        this.tap = tap;
        this.venue = venue;
    }

    /**
     * @param total 채널계가 뜬 뒤 지금까지 오간 전문 수. 들고 있는 것보다 클 수 있다
     * @param capacity 들고 있을 수 있는 수. 이보다 오래된 것은 버려졌다
     */
    /**
     * @param venue 매칭 엔진이 어디에 있는가 — -1 모름, 0 원장 프로세스 안,
     *     1 별도 거래소 프로세스(FEP 경유). 구조도가 지나는 홉만 또렷하게 그리는 데 쓴다
     */
    public record WireLog(
            long total, int capacity, int venue, List<WireTap.Frame> frames) {}

    @GetMapping("/api/wire")
    public WireLog wire(
            @RequestParam(defaultValue = "0") long after,
            @RequestParam(defaultValue = "300") int limit) {
        int n = Math.max(1, Math.min(MAX, limit));
        return new WireLog(
                tap.total(), WireTap.CAPACITY, venue.venue(), tap.since(after, n));
    }
}
