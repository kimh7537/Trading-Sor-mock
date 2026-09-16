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
        registry.addHandler(new StreamHandler(hub), "/ws/stream").setAllowedOrigins("*");
    }
}
