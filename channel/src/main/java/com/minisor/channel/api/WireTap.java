package com.minisor.channel.api;

import com.minisor.channel.wire.WireCodec;
import com.minisor.channel.wire.WireHeader;
import java.time.LocalTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;
import org.springframework.stereotype.Component;

/**
 * 채널계 ↔ 원장 사이를 오간 <b>전문을 전부 적어 두는 곳</b> (점검).
 *
 * <p>화면의 통신 모니터가 "지금 무엇이 오가고 있나"를 보여 주려면 마지막 한 건이 아니라
 * <b>흐름 전체</b>가 필요하다. 주문만이 아니라 호가 조회·잔고·주문 상세·취소·종목 전환·
 * 가상 참가자 스위치·호가 스냅샷까지 같은 소켓으로 나간다.
 *
 * <p><b>필드 값까지 적는다.</b> 전문 이름과 길이만으로는 "고정 길이로 주고받는다"는 사실만
 * 보이고 무엇이 실렸는지는 안 보인다. {@link WireCodec#fields}가 선언을 그대로 되짚으므로
 * 전문이 바뀌면 이 기록도 같이 바뀐다 — 화면이 필드 이름을 따로 적어 두지 않는다.
 *
 * <h4>메모리에만 둔다</h4>
 *
 * <p>최근 {@link #CAPACITY}건만 들고 있고 채널계를 다시 띄우면 사라진다. 이것은 운영
 * 감사 로그가 아니라 <b>화면이 읽는 창</b>이다 — 남겨야 하는 기록(체결)은 이미 SQLite에
 * 따로 적는다. 상한이 없으면 오래 켜 둔 데모가 메모리를 먹는다.
 *
 * <p>여러 스레드가 동시에 적는다(주문 스레드·주기 작업·실시세 스레드). 통째로 잠근다 —
 * 전문 하나 적는 일이고 이 락이 원장 왕복보다 오래 걸릴 일이 없다.
 */
@Component
public class WireTap {

    /** 들고 있을 전문 수. 1초에 서너 건 나가므로 몇 분치다. */
    public static final int CAPACITY = 300;

    private static final DateTimeFormatter AT = DateTimeFormatter.ofPattern("HH:mm:ss.SSS");

    private final Deque<Frame> frames = new ArrayDeque<>();
    private final AtomicLong nextId = new AtomicLong(1);

    /**
     * 오간 전문 한 왕복.
     *
     * @param id 순번. 화면이 "이 뒤로 새로 온 것"만 받아 가는 데 쓴다
     * @param micros 채널계가 잰 왕복 시간. 원장 내부 처리가 여기 포함된다
     * @param ok 응답을 받았나. 거짓이면 보내고 못 받았다는 뜻이다
     */
    public record Frame(
            long id,
            String at,
            String sent,
            int sentType,
            int sentBytes,
            List<WireCodec.FieldView> sentFields,
            String got,
            int gotType,
            int gotBytes,
            List<WireCodec.FieldView> gotFields,
            long seq,
            long micros,
            boolean ok,
            /**
             * **실제로 오간 바이트 그대로**(헤더 + 바디)를 16진수로. 빈 문자열이면 없다.
             *
             * <p>필드 표가 "무슨 값이 실렸나"를 말한다면 이쪽은 "그래서 선에 무엇이
             * 흘렀나"다. 빅엔디언 고정 길이라는 규격이 눈에 보이는 유일한 자리다.
             */
            String sentHex,
            String gotHex,
            /** 헤더가 들고 있던 값. 바디 앞 {@code WireHeader.LENGTH}바이트를 푼 것이다 */
            Header sentHeader,
            Header gotHeader) {}

    /** 전문 머리. 버전·종별·바디 길이·순번·시각이 바디 앞에 붙는다. */
    public record Header(int version, int type, int bodyLen, long seq, long ts) {

        static Header of(byte[] frame) {
            if (frame == null || frame.length < WireHeader.LENGTH) {
                return null;
            }
            WireHeader h = WireHeader.decode(frame, 0);
            return new Header(h.version(), h.type(), h.bodyLen(), h.seq(), h.ts());
        }
    }

    /** 바이트를 16진수로. 너무 길면 자른다 — 화면이 읽을 창이지 덤프 파일이 아니다. */
    private static String hex(byte[] b) {
        if (b == null || b.length == 0) {
            return "";
        }
        int n = Math.min(b.length, MAX_HEX);
        StringBuilder sb = new StringBuilder(n * 2);
        for (int i = 0; i < n; i++) {
            sb.append(Character.forDigit((b[i] >> 4) & 0xf, 16));
            sb.append(Character.forDigit(b[i] & 0xf, 16));
        }
        return sb.toString();
    }

    /** 바이트를 적어 둘 상한. 가장 긴 전문(호가 응답)이 205바이트다. */
    private static final int MAX_HEX = 512;

    /** 바이트 없이 적는다(시험이나 바이트를 모르는 자리에서). */
    public void record(Object request, Object response, long seq, long nanos) {
        record(request, response, seq, nanos, null, null);
    }

    /**
     * 왕복 하나를 적는다. {@code response}가 null이면 답을 못 받은 것이다.
     *
     * @param sentFrame 실제로 소켓에 쓴 바이트(헤더 + 바디). 모르면 null
     * @param gotFrame 실제로 읽은 바이트. 못 받았으면 null
     */
    public void record(
            Object request, Object response, long seq, long nanos, byte[] sentFrame, byte[] gotFrame) {
        Class<?> reqCls = request.getClass();
        Frame f =
                new Frame(
                        nextId.getAndIncrement(),
                        LocalTime.now().format(AT),
                        WireCodec.typeName(reqCls),
                        WireCodec.typeCode(reqCls),
                        WireHeader.LENGTH + WireCodec.bodyLength(reqCls),
                        WireCodec.fields(request),
                        response == null ? "—" : WireCodec.typeName(response.getClass()),
                        response == null ? 0 : WireCodec.typeCode(response.getClass()),
                        response == null
                                ? 0
                                : WireHeader.LENGTH + WireCodec.bodyLength(response.getClass()),
                        response == null ? List.of() : WireCodec.fields(response),
                        seq,
                        /* 나노를 마이크로로. 0.5us를 0us로 적지 않게 반올림한다 */
                        (nanos + 500) / 1000,
                        response != null,
                        hex(sentFrame),
                        hex(gotFrame),
                        Header.of(sentFrame),
                        Header.of(gotFrame));
        synchronized (frames) {
            frames.addLast(f);
            while (frames.size() > CAPACITY) {
                frames.removeFirst();
            }
        }
    }

    /**
     * {@code after}보다 뒤에 적힌 것들. 0이면 들고 있는 전부.
     *
     * <p>화면이 마지막으로 받은 번호를 주고 그 뒤만 받아 간다 — 같은 것을 다시 내려보내면
     * 몇 분만 켜 둬도 응답이 수백 킬로바이트가 된다.
     */
    public List<Frame> since(long after, int limit) {
        List<Frame> out = new ArrayList<>();
        synchronized (frames) {
            for (Frame f : frames) {
                if (f.id() > after) {
                    out.add(f);
                }
            }
        }
        /* 너무 많으면 최근 것부터 남긴다 — 화면이 보는 것은 지금 흐름이다 */
        if (limit > 0 && out.size() > limit) {
            return List.copyOf(out.subList(out.size() - limit, out.size()));
        }
        return List.copyOf(out);
    }

    /** 지금까지 적은 수. 화면이 "몇 건이 오갔나"를 적는다. */
    public long total() {
        return nextId.get() - 1;
    }
}
