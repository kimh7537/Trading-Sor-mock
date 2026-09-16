package com.minisor.channel.stream;

import java.io.IOException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
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

    public void add(WebSocketSession s) {
        sessions.put(s.getId(), s);
    }

    public void remove(WebSocketSession s) {
        sessions.remove(s.getId());
    }

    public int subscriberCount() {
        return sessions.size();
    }

    public void broadcast(StreamEvent event) {
        String text = json.writeValueAsString(event);

        sessions.forEach(
                (id, s) -> {
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
                });
    }
}
