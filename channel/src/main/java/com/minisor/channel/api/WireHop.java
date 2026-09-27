package com.minisor.channel.api;

import com.minisor.channel.wire.WireCodec;
import com.minisor.channel.wire.WireHeader;

/**
 * 채널계 ↔ 원장 <b>전문 한 왕복</b>에 실제로 오간 것 (점검).
 *
 * <p>화면의 통신 모니터가 "무엇이 얼마나 오갔고 얼마나 걸렸나"를 적을 재료다. 구조도만
 * 그리면 그림일 뿐이고, 실제로 지나간 값이 얹혀야 모니터링이 된다.
 *
 * <p><b>전부 잰 값이거나 규격에서 나온 값이다.</b> 지어낸 숫자가 없다 —
 *
 * <ul>
 *   <li>{@code sent}·{@code got} — 전문 이름과 종별 코드. {@code @WireMessage}에서 온다
 *   <li>{@code sentBytes}·{@code gotBytes} — 헤더 + 바디의 <b>고정 길이</b>. 규격 그대로다
 *   <li>{@code seq} — 이 요청에 실어 보낸 순번
 *   <li>{@code micros} — 보내기 직전부터 응답을 다 읽을 때까지 잰 <b>벽시계</b> 시간
 * </ul>
 *
 * <p><b>원장 안에서 단계마다 얼마나 걸렸는지는 여기에 없다.</b> 재지 않기 때문이다.
 * 매칭 엔진 안에서 시스템 시각을 읽는 것은 이 프로젝트가 금지한다({@code CLAUDE.md}의
 * 결정성) — 같은 입력이 같은 출력을 내야 전략 비교가 성립한다. 그래서 {@code micros}는
 * 검증·SOR·매칭·응답까지를 <b>합쳐 잰 하나</b>이고, 화면도 그렇게 적어야 한다.
 *
 * @param micros 채널계가 잰 왕복 시간(마이크로초). 원장 내부 처리가 여기 포함된다
 */
public record WireHop(
        String sent,
        int sentType,
        int sentBytes,
        String got,
        int gotType,
        int gotBytes,
        long seq,
        long micros) {

    /** 보낸 전문과 받은 전문의 클래스로 한 왕복을 적는다. 길이는 규격에서 나온다. */
    public static WireHop of(Class<?> sent, Class<?> got, long seq, long nanos) {
        return new WireHop(
                WireCodec.typeName(sent),
                WireCodec.typeCode(sent),
                WireHeader.LENGTH + WireCodec.bodyLength(sent),
                WireCodec.typeName(got),
                WireCodec.typeCode(got),
                WireHeader.LENGTH + WireCodec.bodyLength(got),
                seq,
                /* 나노를 마이크로로 내린다. 0.5us를 0us로 적지 않게 반올림한다 */
                (nanos + 500) / 1000);
    }
}
