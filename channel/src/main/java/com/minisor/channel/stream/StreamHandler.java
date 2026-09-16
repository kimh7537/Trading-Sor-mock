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

    @Override
    public void afterConnectionEstablished(WebSocketSession session) {
        hub.add(session);
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        hub.remove(session);
    }
}
