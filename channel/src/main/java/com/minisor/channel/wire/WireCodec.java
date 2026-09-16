package com.minisor.channel.wire;

import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

/**
 * 어노테이션 선언만 보고 전문 바디를 만들고 읽는다.
 *
 * <p>종별마다 손으로 쓴 직렬화 코드를 두지 않는 이유는 단순하다 — <b>손으로 쓴
 * 코드는 종별이 늘 때마다 빠뜨릴 자리가 하나씩 는다.</b> C 쪽이 X 매크로 하나로
 * 종별·이름·길이를 함께 만든 것과 같은 생각이다.
 *
 * <h2>규격이 두 곳에 적힌다</h2>
 *
 * C의 {@code msg.h}와 이 패키지의 선언이 <b>같은 것을 두 번 적는다.</b>
 * 어긋나면 컴파일도 되고 테스트도 통과하는데 <b>주고받을 때만 조용히 틀린다</b> —
 * 필드가 한 칸 밀린 주문이 그럴듯한 값으로 해석된다.
 *
 * <p>그래서 {@code WireLayoutTest}가 <b>C 헤더를 직접 읽어 길이를 대조한다.</b>
 * 다만 그 대조가 잡는 것은 <b>길이</b>이지 <b>필드 순서</b>가 아니다. 순서까지
 * 잡으려면 C 쪽이 배치를 기계가 읽을 수 있는 형태로 내보내야 하고, 그것은 아직
 * 하지 않았다. <b>한계를 알고 쓰는 것과 모르고 쓰는 것은 다르다.</b>
 */
public final class WireCodec {

    private WireCodec() {}

    /** 클래스마다 한 번만 훑는다. 전문마다 리플렉션을 다시 돌 이유가 없다. */
    private static final Map<Class<?>, List<Slot>> LAYOUTS = new ConcurrentHashMap<>();

    private record Slot(Field field, WireField spec, int size) {}

    /**
     * 선언을 읽어 배치를 만든다.
     *
     * <p><b>잘못된 선언은 여기서 죽는다.</b> 전문을 주고받다 실패하면 그때는
     * 이미 늦다 — 상대는 이미 무언가를 받았다.
     */
    private static List<Slot> layoutOf(Class<?> cls) {
        return LAYOUTS.computeIfAbsent(cls, c -> {
            if (c.getAnnotation(WireMessage.class) == null) {
                throw new WireException("@WireMessage가 없다: " + c.getName());
            }

            List<Slot> slots = new ArrayList<>();
            for (Field f : c.getDeclaredFields()) {
                WireField w = f.getAnnotation(WireField.class);
                if (w == null) {
                    continue;
                }
                f.setAccessible(true);

                int size = w.type() == WireType.STR ? w.length() : w.type().size();
                if (size <= 0) {
                    throw new WireException(
                            "길이가 잘못됐다: " + c.getSimpleName() + "." + f.getName());
                }
                slots.add(new Slot(f, w, size));
            }
            if (slots.isEmpty()) {
                throw new WireException("@WireField가 하나도 없다: " + c.getName());
            }

            slots.sort(Comparator.comparingInt(s -> s.spec().order()));

            /*
             * 차례가 1부터 빠짐없이 이어져야 한다. 빠지거나 겹치면 바이트가
             * 밀리는데, 그 증상은 "가끔 이상한 주문"으로만 보인다.
             */
            for (int i = 0; i < slots.size(); i++) {
                int want = i + 1;
                if (slots.get(i).spec().order() != want) {
                    throw new WireException(
                            c.getSimpleName() + ": order가 " + want + "에서 어긋난다");
                }
            }
            return List.copyOf(slots);
        });
    }

    /** 그 종별의 바디 길이. 선언에서 계산한다. */
    public static int bodyLength(Class<?> cls) {
        int n = 0;
        for (Slot s : layoutOf(cls)) {
            n += s.size();
        }
        return n;
    }

    public static int typeCode(Class<?> cls) {
        WireMessage m = cls.getAnnotation(WireMessage.class);
        if (m == null) {
            throw new WireException("@WireMessage가 없다: " + cls.getName());
        }
        return m.type();
    }

    /** 바디만 만든다. 헤더는 호출부가 붙인다 — seq와 ts는 세션의 상태다. */
    public static byte[] encodeBody(Object msg) {
        List<Slot> slots = layoutOf(msg.getClass());
        ByteBuffer b =
                ByteBuffer.allocate(bodyLength(msg.getClass()))
                        .order(ByteOrder.BIG_ENDIAN);

        try {
            for (Slot s : slots) {
                Object v = s.field().get(msg);
                switch (s.spec().type()) {
                    case U8 -> b.put((byte) ((Number) v).intValue());
                    case I32 -> b.putInt(((Number) v).intValue());
                    case U64, I64 -> b.putLong(((Number) v).longValue());
                    case STR -> putStr(b, (String) v, s.size());
                }
            }
        } catch (IllegalAccessException e) {
            throw new WireException("필드를 읽을 수 없다: " + e.getMessage());
        }
        return b.array();
    }

    /**
     * 바디를 읽는다.
     *
     * <p><b>길이가 규격과 정확히 같아야 한다.</b> 짧으면 필드가 모자라고, 길면
     * 규격이 다른 상대다(T3-02와 같은 판단).
     */
    public static <T> T decodeBody(Class<T> cls, byte[] body, int off, int len) {
        int want = bodyLength(cls);
        if (len != want) {
            throw new WireException(
                    cls.getSimpleName() + ": 길이가 " + want + "이어야 하는데 " + len);
        }

        ByteBuffer b = ByteBuffer.wrap(body, off, len).order(ByteOrder.BIG_ENDIAN);
        try {
            T out = cls.getDeclaredConstructor().newInstance();
            for (Slot s : layoutOf(cls)) {
                Field f = s.field();
                switch (s.spec().type()) {
                    case U8 -> setInt(f, out, Byte.toUnsignedInt(b.get()));
                    case I32 -> setInt(f, out, b.getInt());
                    case U64, I64 -> f.setLong(out, b.getLong());
                    case STR -> f.set(out, getStr(b, s.size()));
                }
            }
            return out;
        } catch (ReflectiveOperationException e) {
            throw new WireException("만들 수 없다: " + cls.getName() + " — " + e);
        }
    }

    private static void setInt(Field f, Object target, int v)
            throws IllegalAccessException {
        if (f.getType() == int.class) {
            f.setInt(target, v);
        } else {
            f.setLong(target, v);
        }
    }

    /**
     * 고정 길이 문자열. 남는 자리는 0으로 채우고 원본이 길면 자른다.
     * C의 {@code wire_put_str}과 같다 — <b>전문은 길이가 규격이다.</b>
     */
    private static void putStr(ByteBuffer b, String s, int len) {
        byte[] raw = s == null ? new byte[0] : s.getBytes(StandardCharsets.US_ASCII);
        int n = Math.min(raw.length, len);
        b.put(raw, 0, n);
        for (int i = n; i < len; i++) {
            b.put((byte) 0);
        }
    }

    /** 필드 안에 0이 있으면 거기서 끊는다. C의 {@code wire_get_str}과 같다. */
    private static String getStr(ByteBuffer b, int len) {
        byte[] raw = new byte[len];
        b.get(raw);
        int n = 0;
        while (n < len && raw[n] != 0) {
            n++;
        }
        return new String(raw, 0, n, StandardCharsets.US_ASCII);
    }
}
