package com.minisor.channel.wire;

/** 전문을 해석할 수 없다. */
public class WireException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    public WireException(String message) {
        super(message);
    }
}
