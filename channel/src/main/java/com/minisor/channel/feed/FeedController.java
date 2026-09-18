package com.minisor.channel.feed;

import java.io.IOException;
import java.nio.file.Path;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/**
 * 시뮬 모드 ↔ 실시세 모드 (T8-05의 화면 토글이 부르는 곳).
 *
 * <ul>
 *   <li>{@code GET /api/feed} — 지금 어느 모드인가, 바꿀 수 있는가
 *   <li>{@code POST /api/feed/mode?mode=live|sim} — 바꾼다
 *   <li>{@code POST /api/feed/record?file=...} — 받는 스냅샷을 파일로 남긴다(T8-06)
 * </ul>
 *
 * <p>실시세로 바꿀 때 <b>토스 구독을 먼저</b> 시도하고, 설정이 없으면 녹화 파일 재생으로
 * 넘어간다. 장 마감·주말에 데모할 수 있게 하는 것이 재생의 목적이다.
 *
 * <p>둘 다 없으면 409로 거절하고 이유를 적어 보낸다 — 화면이 "켜졌다"고 표시해 놓고 아무 일도
 * 일어나지 않는 것이 제일 나쁘다.
 */
@RestController
public class FeedController {

    private final LiveFeed live;
    private final TossFeedClient toss;
    private final FeedReplayer replay;

    public FeedController(LiveFeed live, TossFeedClient toss, FeedReplayer replay) {
        this.live = live;
        this.toss = toss;
        this.replay = replay;
    }

    @GetMapping("/api/feed")
    public LiveFeed.Status status() {
        return live.status();
    }

    @PostMapping("/api/feed/mode")
    public ResponseEntity<LiveFeed.Status> mode(@RequestParam String mode) {
        if ("sim".equals(mode)) {
            toss.stop();
            replay.stop();
            live.enterSim();
            return ResponseEntity.ok(live.status());
        }
        if (!"live".equals(mode)) {
            return ResponseEntity.badRequest().build();
        }
        if (toss.start() || replay.start()) {
            return ResponseEntity.ok(live.status());
        }
        return ResponseEntity.status(HttpStatus.CONFLICT).body(live.status());
    }

    @PostMapping("/api/feed/record")
    public ResponseEntity<LiveFeed.Status> record(@RequestParam(required = false) String file) {
        if (file == null || file.isBlank()) {
            live.stopRecording();
            return ResponseEntity.ok(live.status());
        }
        try {
            live.startRecording(Path.of(file));
            return ResponseEntity.ok(live.status());
        } catch (IOException | RuntimeException e) {
            return ResponseEntity.badRequest().build();
        }
    }
}
