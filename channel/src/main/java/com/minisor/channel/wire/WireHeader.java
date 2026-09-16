package com.minisor.channel.wire;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * 24바이트 공통 헤더. 배치는 C의 {@code core/include/wire.h}와 같다.
 *
 * <pre>
 *   0  2  magic    0x4D53
 *   2  1  version
 *   3  1  type
 *   4  4  bodyLen
 *   8  8  seq
 *  16  8  ts        논리 시각(나노초)
 * </pre>
 *
 * <p><b>magic이나 version이 다르면 해석하지 않는다.</b> 필드 위치가 바뀌었을
 * 수 있고, 그대로 읽으면 엉뚱한 값을 그럴듯하게 돌려준다(T3-01의 판단 그대로).
 */
public record WireHeader(int version, int type, int bodyLen, long seq, long ts) {

    public static final int MAGIC = 0x4D53;
    public static final int VERSION = 1;
    public static final int LENGTH = 24;
    public static final int BODY_MAX = 65536;

    public byte[] encode() {
        ByteBuffer b = ByteBuffer.allocate(LENGTH).order(ByteOrder.BIG_ENDIAN);
        b.putShort((short) MAGIC);
        b.put((byte) version);
        b.put((byte) type);
        b.putInt(bodyLen);
        b.putLong(seq);
        b.putLong(ts);
        return b.array();
    }

    public static WireHeader decode(byte[] buf, int off) {
        if (buf == null || buf.length - off < LENGTH) {
            throw new WireException(
                    "헤더가 짧다: " + (buf == null ? 0 : buf.length - off));
        }
        ByteBuffer b = ByteBuffer.wrap(buf, off, LENGTH).order(ByteOrder.BIG_ENDIAN);

        int magic = Short.toUnsignedInt(b.getShort());
        if (magic != MAGIC) {
            throw new WireException(String.format("magic이 다르다: 0x%04X", magic));
        }
        int version = Byte.toUnsignedInt(b.get());
        if (version != VERSION) {
            throw new WireException("version이 다르다: " + version);
        }
        int type = Byte.toUnsignedInt(b.get());
        int bodyLen = b.getInt();
        if (bodyLen < 0 || bodyLen > BODY_MAX) {
            throw new WireException("body_len이 한도를 넘는다: " + bodyLen);
        }
        return new WireHeader(version, type, bodyLen, b.getLong(), b.getLong());
    }
}
