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
 *   <li>{@code POST /api/feed/record?on=true|false} — 받는 스냅샷을 파일로 남긴다(T8-06)
 * </ul>
 *
 * <p><b>녹화 경로를 요청에서 받지 않는다.</b> 파일 이름을 파라미터로 받으면 이 API를 부를 수
 * 있는 누구나 프로세스가 쓸 수 있는 아무 자리에나 파일을 만들 수 있다. 어디에 적을지는
 * 설정({@code minisor.feed.record-file})이 정하고, 요청은 켜고 끄기만 한다.
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
    private final FeedProperties props;

    public FeedController(
            LiveFeed live, TossFeedClient toss, FeedReplayer replay, FeedProperties props) {
        this.live = live;
        this.toss = toss;
        this.replay = replay;
        this.props = props;
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
        /*
         * **토스가 이미 붙기를 포기하고 있으면 재생으로 넘어간다.**
         *
         * 키가 있어도 허용 IP를 등록하지 않으면 토큰 발급이 403이고, 그 상태로는 토스가
         * 백오프를 돌 뿐 영영 붙지 않는다. 토스를 늘 먼저 고르면 그런 사람은 실시세 버튼을
         * 아무리 눌러도 아무 일이 일어나지 않는다 — 실제로 그렇게 됐다.
         *
         * 이때 토스를 **멈추고** 재생을 켠다. 세워 두지 않으면 나중에 뒤늦게 붙어 두 피드가
         * 같은 호가창에 서로 다른 스냅샷을 밀어 넣는다.
         */
        if (toss.isRunning() && live.status().error() != null && replay.usable()) {
            toss.stop();
            replay.start();
            return ResponseEntity.ok(live.status());
        }

        /*
         * 토스는 **붙어 봐야 안다**. 시작했다는 것과 붙었다는 것은 다르므로 202로 답하고,
         * 실제로 붙으면 `feed-mode` 방송이 화면을 바꾼다. 재생은 파일을 읽는 것이라
         * 시작한 순간 이미 실시세다.
         */
        if (toss.start()) {
            return ResponseEntity.accepted().body(live.status());
        }
        if (replay.start()) {
            return ResponseEntity.ok(live.status());
        }
        return ResponseEntity.status(HttpStatus.CONFLICT).body(live.status());
    }

    @PostMapping("/api/feed/record")
    public ResponseEntity<LiveFeed.Status> record(
            @RequestParam(defaultValue = "true") boolean on) {
        if (!on) {
            live.stopRecording();
            return ResponseEntity.ok(live.status());
        }
        if (!props.recording()) {
            /* 적을 자리가 설정돼 있지 않다. 어디에 적을지 요청이 정하지 않는다 */
            return ResponseEntity.status(HttpStatus.CONFLICT).body(live.status());
        }
        try {
            live.startRecording(Path.of(props.recordFile()));
            return ResponseEntity.ok(live.status());
        } catch (IOException | RuntimeException e) {
            return ResponseEntity.status(HttpStatus.INTERNAL_SERVER_ERROR).build();
        }
    }
}
