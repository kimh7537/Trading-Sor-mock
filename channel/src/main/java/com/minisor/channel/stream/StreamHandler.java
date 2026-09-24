package com.minisor.channel.stream;

import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

/** 구독 접속의 시작과 끝만 다룬다. 화면이 보내는 말은 받지 않는다. */
public class StreamHandler extends TextWebSocketHandler {

    private final StreamHub hub;

    public StreamHandler(StreamHub hub) {
        this.hub = hub;
    }

    /**
     * 악수 때 실려 온 HTTP 세션 속성에서 계좌를 꺼낸다(T9-04). 로그인하지 않고 붙었으면
     * null이고, 그 접속은 호가처럼 모두가 보는 것만 받는다.
     */
    @Override
    public void afterConnectionEstablished(WebSocketSession session) {
        Object account = session.getAttributes()
                .get(com.minisor.channel.auth.AuthController.SESSION_ACCOUNT);
        hub.add(session, account instanceof String s ? s : null);
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        hub.remove(session);
    }
}
