package com.minisor.channel.wire;

import java.lang.annotation.ElementType;
import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Target;

/**
 * 전문 바디의 필드 하나.
 *
 * <p><b>순서를 이름이 아니라 {@link #order()}로 정한다.</b> 자바 리플렉션이
 * 돌려주는 필드 순서는 규격이 아니다 — JVM 판올림이나 컴파일러가 바꿔도
 * 되는 값이다. 그것에 기대면 어느 날 바이트 배치가 조용히 뒤바뀐다.
 */
@Retention(RetentionPolicy.RUNTIME)
@Target(ElementType.FIELD)
public @interface WireField {

    /** 바디 안의 차례. 1부터, 빠짐없이, 겹치지 않게. */
    int order();

    WireType type();

    /** {@link WireType#STR}일 때의 바이트 길이. 다른 타입에서는 무시한다. */
    int length() default 0;

    /**
     * 같은 타입이 몇 개 이어지는가. 1보다 크면 필드는 {@code int[]}이고 {@link WireType#I32}만
     * 받는다 — 호가 10단(T6-04)을 필드 40개로 풀어 쓰지 않으려고 더했다. 구조체 배열은
     * 받지 않는다. C 쪽도 가격·수량을 배열 넷으로 나눠 이 모양에 맞췄다.
     */
    int count() default 1;
}
