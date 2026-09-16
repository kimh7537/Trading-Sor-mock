package com.minisor.channel.wire;

/**
 * 전문 필드의 타입.
 *
 * <p>C 쪽(`core/include/wire.h`)이 쓰는 것과 같은 것만 둔다. 없는 타입을
 * 늘리면 두 쪽이 갈라진다.
 */
public enum WireType {
    U8(1),
    U64(8),
    I32(4),
    I64(8),
    /** 고정 길이 문자열. 길이는 {@link WireField#length()}가 정한다. */
    STR(-1);

    private final int size;

    WireType(int size) {
        this.size = size;
    }

    /** 고정 크기. STR이면 -1이고 필드 선언의 길이를 봐야 한다. */
    public int size() {
        return size;
    }
}
