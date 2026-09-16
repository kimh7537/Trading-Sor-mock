package com.minisor.channel.ledger;

/** 원장과의 통신이 실패했다. */
public class LedgerException extends RuntimeException {

    private static final long serialVersionUID = 1L;

    public LedgerException(String message) {
        super(message);
    }

    public LedgerException(String message, Throwable cause) {
        super(message, cause);
    }
}
