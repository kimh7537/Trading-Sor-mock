package com.minisor.channel.wire;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/** 전문 종별. 코드는 C의 {@code MSG_TYPE_LIST}와 같아야 한다. */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.TYPE)
public @interface WireMessage {

    /** 종별 코드. C의 X 매크로 목록과 같은 값. */
    int type();

    /** 사람이 읽을 이름. 어긋남을 찾을 때 쓴다. */
    String name();
}
