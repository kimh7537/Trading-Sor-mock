package com.minisor.channel.stream;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

@Configuration
@EnableWebSocket
public class StreamConfig implements WebSocketConfigurer {

    private final StreamHub hub;

    public StreamConfig(StreamHub hub) {
        this.hub = hub;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        /*
         * 악수(handshake) 때 HTTP 세션 속성을 들고 온다(T9-04). 그래야 이 접속이 누구
         * 것인지 알 수 있고, 잔고·내 주문을 그 사람에게만 보낼 수 있다. 이것이 없으면
         * WebSocket 쪽에는 로그인 정보가 전혀 닿지 않는다.
         */
        registry.addHandler(new StreamHandler(hub), "/ws/stream")
                .addInterceptors(new org.springframework.web.socket.server.support
                        .HttpSessionHandshakeInterceptor())
                .setAllowedOrigins("*");
    }
}
