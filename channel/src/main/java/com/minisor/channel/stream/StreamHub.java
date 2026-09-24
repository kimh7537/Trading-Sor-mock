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

    /**
     * 접속 하나가 어느 계좌의 것인지(T9-04). 로그인하지 않고 붙은 접속은 여기에 없고,
     * 호가처럼 <b>모두가 보는 것</b>만 받는다.
     */
    private final Map<String, String> accountOf = new ConcurrentHashMap<>();

    public void add(WebSocketSession s, String account) {
        if (account != null) {
            accountOf.put(s.getId(), account);
        }
        add(s);
    }

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
        accountOf.remove(s.getId());
    }

    /**
     * 그 계좌로 로그인한 접속에만 보낸다(T9-04).
     *
     * <p>잔고·내 주문·내 체결은 <b>방송하면 안 된다.</b> 한 사람의 잔고가 모든 화면에
     * 뜨는 일은 사용자가 하나일 때는 티가 나지 않지만, 둘이 되는 순간 남의 돈이 보인다.
     *
     * <p>같은 사람이 창을 여럿 열었으면 그 창들 모두에 간다.
     */
    public void sendTo(String account, StreamEvent event) {
        if (account == null) {
            return;
        }
        String text = json.writeValueAsString(event);
        accountOf.forEach((id, owner) -> {
            if (account.equals(owner)) {
                WebSocketSession s = sessions.get(id);
                if (s != null) {
                    send(id, s, text);
                }
            }
        });
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
