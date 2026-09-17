package com.minisor.channel.wire;

import static org.assertj.core.api.Assertions.assertThat;

import java.io.IOException;
import java.lang.reflect.Field;
import java.lang.reflect.Modifier;
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
            Map.ofEntries(
                    Map.entry(OrderReq.class, "MSG_ORDER_REQ_LEN"),
                    Map.entry(OrderAck.class, "MSG_ORDER_ACK_LEN"),
                    Map.entry(CancelReq.class, "MSG_CANCEL_REQ_LEN"),
                    Map.entry(CancelAck.class, "MSG_CANCEL_ACK_LEN"),
                    Map.entry(FillNoti.class, "MSG_FILL_NOTI_LEN"),
                    Map.entry(QueryAck.class, "MSG_QUERY_ACK_LEN"),
                    Map.entry(BookReq.class, "MSG_BOOK_REQ_LEN"),
                    Map.entry(BookAck.class, "MSG_BOOK_ACK_LEN"),
                    Map.entry(DetailReq.class, "MSG_DETAIL_REQ_LEN"),
                    Map.entry(DetailAck.class, "MSG_DETAIL_ACK_LEN"),
                    Map.entry(BalanceReq.class, "MSG_BALANCE_REQ_LEN"),
                    Map.entry(BalanceAck.class, "MSG_BALANCE_ACK_LEN"));

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

    /**
     * <b>길이가 맞아도 값의 뜻이 틀리면 조용히 틀린다.</b>
     *
     * <p>위의 길이 대조만 있던 시절, 채널계는 매수를 1로 보냈고 C는 1을 매도로
     * 읽었다(T6-01). 바이트 배치는 완벽했으므로 길이 대조는 통과했다. 그래서
     * {@code types.h}의 열거값을 읽어 {@link WireEnums}와 <b>양쪽 방향으로</b>
     * 대조한다 — 자바에만 있는 값도, C에 새로 생겼는데 자바가 모르는 값도 잡는다.
     */
    @Test
    void javaEnumValuesMatchCHeader() throws IOException, IllegalAccessException {
        Map<String, Integer> c = readCEnums();
        assertThat(c).as("C 헤더에서 열거값을 읽지 못했다").isNotEmpty();
        /*
         * 자동 배분 시장값은 열거형이 아니라 msg.h의 #define이다(열거형에 넣으면 시장
         * 반복문이 없는 시장까지 돈다). 전문에 실리는 값이므로 같이 대조한다.
         */
        Integer auto = readCLengths().get("MSG_MARKET_AUTO");
        assertThat(auto).as("msg.h에 MSG_MARKET_AUTO가 없다").isNotNull();
        c.put("MSG_MARKET_AUTO", auto);

        Map<String, Integer> java = new HashMap<>();
        for (Field f : WireEnums.class.getDeclaredFields()) {
            int mod = f.getModifiers();
            if (Modifier.isStatic(mod) && Modifier.isPublic(mod) && f.getType() == int.class) {
                java.put(f.getName(), f.getInt(null));
            }
        }
        assertThat(java).as("WireEnums에 상수가 없다").isNotEmpty();

        java.forEach(
                (name, v) -> {
                    assertThat(c).as("C 헤더에 %s가 없다", name).containsKey(name);
                    assertThat(v).as("%s의 값이 C와 다르다", name).isEqualTo(c.get(name));
                });
        c.forEach(
                (name, v) ->
                        assertThat(java)
                                .as("C에 있는 %s를 자바가 모른다", name)
                                .containsKey(name));
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

    /**
     * 숫자와 이미 아는 상수의 덧셈·곱셈만 계산한다(곱셈이 먼저). 모르는 것이 섞이면 null.
     * 곱셈은 호가 10단 길이({@code MSG_BOOK_DEPTH * 4 * 4})에서 필요해졌다.
     */
    private static Integer eval(String expr, Map<String, Integer> known) {
        String e = expr.replace("(", " ").replace(")", " ").trim();
        int sum = 0;
        for (String term : e.split("\\+")) {
            int product = 1;
            for (String tok : term.split("\\*")) {
                String t = tok.trim();
                if (t.isEmpty()) {
                    return null;
                }
                if (t.matches("\\d+")) {
                    product *= Integer.parseInt(t);
                } else if (known.containsKey(t)) {
                    product *= known.get(t);
                } else {
                    return null;
                }
            }
            sum += product;
        }
        return sum;
    }

    private static final Pattern ENUM_BLOCK =
            Pattern.compile("typedef\\s+enum\\s*\\{([^}]*)\\}", Pattern.DOTALL);
    private static final Pattern COMMENT = Pattern.compile("/\\*.*?\\*/", Pattern.DOTALL);

    /**
     * {@code types.h}의 {@code typedef enum { ... }} 블록을 읽어 이름 → 값을 낸다.
     *
     * <p>C의 규칙대로 <b>값을 안 적은 항목은 앞 값 + 1</b>이다
     * ({@code ORDER_LIMIT = 0, ORDER_MARKET, ...}). 이걸 빼먹으면 첫 항목만
     * 맞고 나머지가 전부 0으로 읽혀, 틀린 자바 상수가 오히려 통과한다.
     */
    private static Map<String, Integer> readCEnums() throws IOException {
        Path h = findRepoFile("core/include/types.h");
        if (h == null) {
            return Map.of();
        }
        String text = COMMENT.matcher(Files.readString(h, StandardCharsets.UTF_8)).replaceAll(" ");

        Map<String, Integer> out = new HashMap<>();
        Matcher blocks = ENUM_BLOCK.matcher(text);
        while (blocks.find()) {
            int next = 0;
            for (String item : blocks.group(1).split(",")) {
                String t = item.trim();
                if (t.isEmpty()) {
                    continue;
                }
                int eq = t.indexOf('=');
                String name = (eq < 0 ? t : t.substring(0, eq)).trim();
                int value = (eq < 0) ? next : Integer.parseInt(t.substring(eq + 1).trim());
                out.put(name, value);
                next = value + 1;
            }
        }
        return out;
    }

    /** 저장소 어디서 실행되든 헤더를 찾는다. */
    private static Path findHeader() {
        return findRepoFile("core/include/msg.h");
    }

    private static Path findRepoFile(String rel) {
        Path p = Path.of("").toAbsolutePath();
        for (int i = 0; i < 5 && p != null; i++, p = p.getParent()) {
            Path h = p.resolve(rel);
            if (Files.exists(h)) {
                return h;
            }
        }
        return null;
    }
}
