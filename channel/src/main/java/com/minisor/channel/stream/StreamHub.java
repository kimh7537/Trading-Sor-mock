package com.minisor.channel.stream;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicBoolean;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;

import tools.jackson.databind.ObjectMapper;

/**
 * 구독자 목록과 전파.
 *
 * <p>느린 구독자 하나가 전체를 막지 않게, 보내다 실패하면 <b>그 구독자만
 * 끊는다.</b>
 */
@Component
public class StreamHub {

    private final Map<String, WebSocketSession> sessions = new ConcurrentHashMap<>();
    private final ObjectMapper json = new ObjectMapper();

    /** 마지막으로 본 원장 상태. 처음엔 붙어 있다고 본다 — 첫 실패에서 알린다. */
    private final AtomicBoolean ledgerUp = new AtomicBoolean(true);

    private volatile String ledgerDownWhy = "";

    public void add(WebSocketSession s) {
        sessions.put(s.getId(), s);
        /* 이미 끊긴 뒤에 들어온 화면도 알아야 한다. 방송은 바뀌는 순간에만 가므로 따로 보낸다 */
        if (!ledgerUp.get()) {
            send(s.getId(), s, json.writeValueAsString(StreamEvent.ledgerDown(ledgerDownWhy)));
        }
    }

    /**
     * 원장과 주고받은 결과를 알린다. <b>상태가 바뀔 때만 방송한다</b> — 화면은 1초마다 호가를
     * 읽으므로 실패할 때마다 보내면 같은 알림이 초마다 쌓인다.
     *
     * <p>T4-05의 완료 조건("원장이 끊기면 화면에 보인다")은 처음부터 있었지만, 이것을
     * 부르는 곳이 없어 화면의 "원장 끊김" 표시는 한 번도 켜지지 않았다. 테스트가
     * {@link #broadcast}를 직접 불러 통과했기 때문이다(T6-10에서 발견).
     */
    public void ledgerReachable(boolean up, String why) {
        if (up) {
            if (ledgerUp.compareAndSet(false, true)) {
                broadcast(StreamEvent.ledgerUp());
            }
        } else {
            ledgerDownWhy = why == null ? "원인 미상" : why;
            if (ledgerUp.compareAndSet(true, false)) {
                broadcast(StreamEvent.ledgerDown(ledgerDownWhy));
            }
        }
    }

    public void remove(WebSocketSession s) {
        sessions.remove(s.getId());
    }

    public int subscriberCount() {
        return sessions.size();
    }

    public void broadcast(StreamEvent event) {
        String text = json.writeValueAsString(event);

        sessions.forEach((id, s) -> send(id, s, text));
    }

    private void send(String id, WebSocketSession s, String text) {
        try {
            if (!s.isOpen()) {
                sessions.remove(id);
                return;
            }
            synchronized (s) {
                s.sendMessage(new TextMessage(text));
            }
        } catch (IOException | IllegalStateException e) {
            /* 이 구독자만 끊는다. 나머지는 계속 받는다. */
            sessions.remove(id);
            try {
                s.close();
            } catch (IOException ignored) {
                // 닫다 나는 오류로 할 수 있는 일이 없다
            }
        }
    }
}
