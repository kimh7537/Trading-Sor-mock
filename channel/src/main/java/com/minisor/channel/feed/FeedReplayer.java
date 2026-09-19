package com.minisor.channel.feed;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

/**
 * 녹화한 장을 다시 재생한다 (T8-06).
 *
 * <h2>왜 필요한가</h2>
 *
 * 실시세는 <b>장중에만</b> 움직인다. 마감 뒤나 주말에는 실시세 모드를 켜도 호가창이 멈춰
 * 있어 아무것도 보여 줄 수 없다. 한 번 받아 적어 둔 장을 다시 틀면 언제든 같은 화면이 나온다.
 *
 * <h2>같은 파일은 같은 결과를 낸다</h2>
 *
 * 재생은 파일에 적힌 <b>순서 그대로</b> {@link LiveFeed#apply}를 부른다. 배속은 부르는
 * <b>간격</b>만 바꾸고 순서와 내용은 바꾸지 않는다. 원장 쪽이 시스템 시각을 읽지 않으므로
 * (스냅샷이 자기 시각을 들고 온다) 같은 파일은 늘 같은 호가창을 만든다.
 *
 * <p>간격은 스냅샷 시각의 차이를 배속으로 나눈 값이다. 시각이 거꾸로 가거나 너무 벌어진 줄
 * (장 시작 전 {@code null} 시각 등)은 상한을 둬서 재생이 몇 분씩 멈추지 않게 한다.
 */
@Component
public class FeedReplayer implements AutoCloseable {

    private static final Logger log = LoggerFactory.getLogger(FeedReplayer.class);

    /** 한 줄과 다음 줄 사이에 기다리는 시간의 상한. */
    private static final long MAX_GAP_MS = 2000;

    private final FeedProperties props;
    private final LiveFeed live;
    private final AtomicBoolean running = new AtomicBoolean();
    private volatile Thread thread;

    public FeedReplayer(FeedProperties props, LiveFeed live) {
        this.props = props;
        this.live = live;
    }

    public boolean usable() {
        return props.replaying() && Files.isReadable(Path.of(props.replayFile()));
    }

    public boolean isRunning() {
        return running.get();
    }

    /** 설정된 파일을 재생한다. 파일이 없으면 거짓. */
    public synchronized boolean start() {
        if (!usable() || !running.compareAndSet(false, true)) {
            return running.get();
        }
        /*
         * 모드를 **여기서** 바꾼다. 재생 스레드 안에서 바꾸면 켜 달라는 요청에 아직
         * "시뮬"이라고 답하게 된다 — 화면이 방금 누른 것과 다른 상태를 본다.
         */
        live.enterLive("replay");
        Thread t = new Thread(() -> run(Path.of(props.replayFile()), speed()), "feed-replay");
        t.setDaemon(true);
        thread = t;
        t.start();
        return true;
    }

    public synchronized void stop() {
        running.set(false);
        Thread t = thread;
        if (t != null) {
            t.interrupt();
        }
    }

    @Override
    public void close() {
        stop();
    }

    private double speed() {
        double s = props.replaySpeed();
        return s > 0 ? s : 1.0;
    }

    private void run(Path file, double speed) {
        try {
            List<Snapshot> all = FeedFile.read(file);
            log.info("리플레이: {} 건 ({}배속)", all.size(), speed);

            long prev = -1;
            for (Snapshot s : all) {
                if (!running.get()) {
                    break;
                }
                long waitMs = prev < 0 ? 0 : gapMs(prev, s.tsNanos(), speed);
                if (waitMs > 0 && !sleep(waitMs)) {
                    break;
                }
                prev = s.tsNanos();
                live.apply(s);
            }
        } catch (IOException e) {
            log.warn("리플레이 파일을 읽지 못했다: {}", e.toString());
        } catch (RuntimeException e) {
            log.warn("리플레이 중단: {}", e.toString());
        } finally {
            /*
             * **끝났다고 말한다.** 조용히 시뮬로 돌아가면, 화면은 방금 누른 "실시세"가 왜
             * 도로 시뮬이 됐는지 알 수 없다. 사용자가 시뮬 버튼으로 세운 것(`stop()`)은
             * 스스로 끝난 것과 다르므로 그때는 적지 않는다.
             */
            if (running.compareAndSet(true, false)) {
                live.noteError("녹화 재생이 끝났다 — 시뮬로 돌아간다");
            }
            live.enterSim();
        }
    }

    /** 두 스냅샷 사이의 기다림. 거꾸로 가거나 너무 벌어진 줄은 상한으로 접는다. */
    static long gapMs(long prevNanos, long nowNanos, double speed) {
        long deltaMs = (nowNanos - prevNanos) / 1_000_000L;
        if (deltaMs <= 0) {
            return 0;
        }
        long scaled = (long) (deltaMs / speed);
        return Math.min(scaled, MAX_GAP_MS);
    }

    private boolean sleep(long ms) {
        try {
            TimeUnit.MILLISECONDS.sleep(ms);
            return running.get();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }
}
