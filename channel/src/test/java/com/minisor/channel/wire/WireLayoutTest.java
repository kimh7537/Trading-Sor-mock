package com.minisor.channel.wire;

import static org.assertj.core.api.Assertions.assertThat;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import org.junit.jupiter.api.Test;

/**
 * <b>규격이 두 곳에 적혀 있다는 문제를 정면으로 다룬다.</b>
 *
 * <p>C의 {@code core/include/msg.h}와 이 패키지의 어노테이션이 같은 것을 두 번
 * 적는다. 어긋나면 컴파일도 되고 각자의 테스트도 통과하는데, <b>주고받을 때만
 * 조용히 틀린다</b> — 필드가 한 칸 밀린 주문이 그럴듯한 값으로 해석된다.
 *
 * <p>그래서 이 테스트는 <b>C 헤더를 직접 읽어</b> 길이 상수를 계산하고 자바
 * 선언과 대조한다. 누가 한쪽만 고치면 여기서 깨진다.
 *
 * <h2>이 대조가 잡지 못하는 것</h2>
 *
 * <b>필드 순서는 못 잡는다.</b> 길이의 합이 같으면서 순서만 다른 배치는 여기를
 * 통과한다(예: {@code price:i32 qty:i32}를 뒤집어도 합은 같다).
 *
 * <p>순서까지 잡으려면 C 쪽이 배치를 기계가 읽을 수 있는 형태로 내보내야 한다.
 * 아직 하지 않았고, <b>한계를 알고 쓰는 것과 모르고 쓰는 것은 다르므로</b>
 * 여기 적어 둔다.
 */
class WireLayoutTest {

    /** 자바 선언 -> C의 길이 상수 이름. */
    private static final Map<Class<?>, String> EXPECT =
            Map.of(
                    OrderReq.class, "MSG_ORDER_REQ_LEN",
                    OrderAck.class, "MSG_ORDER_ACK_LEN",
                    CancelReq.class, "MSG_CANCEL_REQ_LEN",
                    CancelAck.class, "MSG_CANCEL_ACK_LEN",
                    FillNoti.class, "MSG_FILL_NOTI_LEN",
                    QueryAck.class, "MSG_QUERY_ACK_LEN");

    @Test
    void javaLayoutMatchesCHeader() throws IOException {
        Map<String, Integer> c = readCLengths();

        /*
         * 헤더를 못 찾으면 통과시키지 않는다. "파일이 없어서 건너뜀"은
         * 대조를 안 한 것과 같은데, 통과로 보이면 훨씬 나쁘다.
         */
        assertThat(c).as("C 헤더에서 길이 상수를 읽지 못했다").isNotEmpty();

        EXPECT.forEach(
                (cls, name) -> {
                    assertThat(c)
                            .as("C 헤더에 %s가 없다", name)
                            .containsKey(name);
                    assertThat(WireCodec.bodyLength(cls))
                            .as("%s의 길이가 C와 다르다", cls.getSimpleName())
                            .isEqualTo(c.get(name));
                });
    }

    /** 헤더 길이도 C와 같아야 한다. */
    @Test
    void headerLengthMatches() {
        assertThat(WireHeader.LENGTH).isEqualTo(24);
        assertThat(WireHeader.MAGIC).isEqualTo(0x4D53);
    }

    /** 종별 코드가 겹치면 전문 하나를 둘로 해석하게 된다. */
    @Test
    void typeCodesAreDistinct() {
        List<Class<?>> all = List.copyOf(EXPECT.keySet());
        for (int i = 0; i < all.size(); i++) {
            for (int j = i + 1; j < all.size(); j++) {
                assertThat(WireCodec.typeCode(all.get(i)))
                        .isNotEqualTo(WireCodec.typeCode(all.get(j)));
            }
        }
    }

    // ------------------------------------------------------------------

    private static final Pattern DEFINE =
            Pattern.compile("#define\\s+(MSG_[A-Z_]+)\\s+(.+)");

    /**
     * {@code msg.h}의 {@code #define MSG_*_LEN (...)}을 읽어 값을 계산한다.
     *
     * <p>덧셈과 이미 정의된 상수만 나오므로 간단한 계산으로 충분하다. C 전처리기를
     * 흉내 내려 들면 그쪽이 또 하나의 규격이 된다.
     */
    private static Map<String, Integer> readCLengths() throws IOException {
        Path h = findHeader();
        if (h == null) {
            return Map.of();
        }

        Map<String, Integer> out = new HashMap<>();
        String text = Files.readString(h, StandardCharsets.UTF_8);
        // 줄 끝 역슬래시로 이어진 정의를 한 줄로 만든다.
        // 이스케이프를 쓰지 않는다 — 여러 층을 거치며 한 겹씩 벗겨진다.
        final String contBackslash = "" + (char) 92 + (char) 10;
        text = text.replace(contBackslash, " ");

        for (String line : text.split("" + (char) 10)) {
            Matcher m = DEFINE.matcher(line.trim());
            if (!m.find()) {
                continue;
            }
            Integer v = eval(m.group(2), out);
            if (v != null) {
                out.put(m.group(1), v);
            }
        }
        return out;
    }

    /** 숫자와 이미 아는 상수의 덧셈만 계산한다. 모르는 것이 섞이면 null. */
    private static Integer eval(String expr, Map<String, Integer> known) {
        String e = expr.replace("(", " ").replace(")", " ").trim();
        int sum = 0;
        for (String tok : e.split("\\+")) {
            String t = tok.trim();
            if (t.isEmpty()) {
                return null;
            }
            if (t.matches("\\d+")) {
                sum += Integer.parseInt(t);
            } else if (known.containsKey(t)) {
                sum += known.get(t);
            } else {
                return null;
            }
        }
        return sum;
    }

    /** 저장소 어디서 실행되든 헤더를 찾는다. */
    private static Path findHeader() {
        Path p = Path.of("").toAbsolutePath();
        for (int i = 0; i < 5 && p != null; i++, p = p.getParent()) {
            Path h = p.resolve("core/include/msg.h");
            if (Files.exists(h)) {
                return h;
            }
        }
        return null;
    }
}
