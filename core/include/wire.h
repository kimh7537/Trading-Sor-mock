#ifndef MINI_SOR_WIRE_H
#define MINI_SOR_WIRE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * 전문(message) 공통 헤더와 직렬화 유틸.
 *
 * **이 형식은 이 프로젝트의 자체 설계다.** 실제 증권사·거래소의 전문 규격을
 * 참고하지 않았고, 참고해서도 안 된다(CLAUDE.md).
 *
 * ===========================================================================
 * 구조체를 그대로 보내지 않는다
 * ===========================================================================
 *
 * `write(fd, &msg, sizeof(msg))`는 두 가지에 기대는 코드다.
 *
 *  1. **패딩** — 컴파일러가 필드 사이에 얼마를 넣는지는 ABI가 정한다. 같은 소스를
 *     다른 컴파일러로 빌드한 두 프로세스가 서로를 오해석한다
 *  2. **바이트 순서** — x86은 리틀엔디언이지만 그것에 기대는 순간 형식이 아니라
 *     플랫폼이 규격이 된다
 *
 * 둘 다 "지금 이 기계에서는 잘 된다"로 통과하고 나중에 조용히 깨진다. 그래서
 * 필드를 하나씩 바이트로 옮긴다. 느리지만 이 구간은 핫 패스가 아니다.
 *
 * ===========================================================================
 * 바이트 순서: 빅엔디언 (네트워크 바이트 순서)
 * ===========================================================================
 *
 * 리틀엔디언이 지금 기계에 더 빠르지만, 빅엔디언은 **덤프를 눈으로 읽을 수 있다.**
 * 전문을 16진수로 찍어 놓고 필드를 세는 일이 이 계층의 디버깅 대부분이다.
 * 0x00000064가 100인 것은 바로 보이지만 0x64000000은 한 번 더 생각해야 한다.
 *
 * ===========================================================================
 * 헤더 배치 (24바이트 고정)
 * ===========================================================================
 *
 *   오프셋  크기  필드       설명
 *   ------  ----  ---------  ------------------------------------------------
 *        0     2  magic      0x4D53 ("MS"). 스트림 동기가 깨진 것을 빨리 잡는다
 *        2     1  version    형식 판 번호. 다르면 해석하지 않고 거절한다
 *        3     1  type       전문 종별. 종별 목록은 T3-02가 채운다
 *        4     4  body_len   헤더 뒤에 이어지는 바디의 바이트 수
 *        8     8  seq        송신자 기준 시퀀스 번호. 갭 감지(T3-12)의 재료
 *       16     8  ts         논리 시각(나노초). 시스템 시각이 아니다
 *
 * **체크섬을 넣지 않는다.** TCP가 이미 세그먼트 단위로 체크섬을 하고, 이 구간에
 * TCP 말고 다른 전송은 없다. 같은 검사를 두 겹으로 두면 한쪽만 고치는 일이 생긴다.
 * magic은 체크섬이 아니라 **동기 깨짐을 빨리 잡는 표식**이다 — 조립이 어긋났다는 것을
 * 첫 전문에서 알아채기 위한 것이지, 어긋난 뒤 다시 맞추기 위한 것이 아니다.
 * TCP에서 재동기가 왜 위험한지는 `fep/include/framer.h`(T3-09)에 적었다.
 *
 * **길이를 헤더에 둔다.** 구분자(delimiter)를 쓰면 바디에 그 바이트가 나올 때마다
 * 이스케이프해야 하고, 이스케이프는 길이를 바꿔 버퍼 계산을 어렵게 만든다.
 * 길이 선행 방식은 부분 수신 처리(T3-09)도 단순하게 만든다 — 얼마를 더 받아야
 * 하는지 헤더만 읽으면 안다.
 */

#define WIRE_MAGIC 0x4D53u /* "MS" */
#define WIRE_VERSION 1u
#define WIRE_HEADER_LEN 24u

/*
 * 바디 길이 상한.
 *
 * 상한이 없으면 body_len에 0xFFFFFFFF가 실린 전문 하나가 수신 측에 4GB를 할당하게
 * 만든다. 가장 큰 전문(조회 응답)도 수 KB를 넘지 않으므로 64KB면 넉넉하다.
 */
#define WIRE_BODY_MAX 65536u

/* 전문 하나의 최대 크기. 수신 버퍼를 이 크기로 잡는다. */
#define WIRE_FRAME_MAX (WIRE_HEADER_LEN + WIRE_BODY_MAX)

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint32_t body_len;
    uint64_t seq;
    int64_t  ts;
} wire_header_t;

/* --- 정수 직렬화 (빅엔디언) --- */

/*
 * 경계 검사를 하지 않는다. 호출부가 자리를 확보한 뒤 부르는 내부 유틸이고,
 * 매 필드마다 검사하면 인코딩 함수가 검사 코드로 뒤덮인다.
 * 바깥에서 들어오는 버퍼는 wire_decode_header()가 먼저 길이를 본다.
 */
void wire_put_u8(uint8_t *p, uint8_t v);
void wire_put_u16(uint8_t *p, uint16_t v);
void wire_put_u32(uint8_t *p, uint32_t v);
void wire_put_u64(uint8_t *p, uint64_t v);
void wire_put_i32(uint8_t *p, int32_t v);
void wire_put_i64(uint8_t *p, int64_t v);

uint8_t  wire_get_u8(const uint8_t *p);
uint16_t wire_get_u16(const uint8_t *p);
uint32_t wire_get_u32(const uint8_t *p);
uint64_t wire_get_u64(const uint8_t *p);
int32_t  wire_get_i32(const uint8_t *p);
int64_t  wire_get_i64(const uint8_t *p);

/*
 * 고정 길이 문자열 필드. 남는 자리는 0으로 채우고, 원본이 길면 잘라 넣는다.
 * **널 종료를 보장하지 않는 대신 길이가 고정이다** — 전문은 길이가 규격이다.
 */
void wire_put_str(uint8_t *p, size_t field_len, const char *s);

/*
 * 고정 길이 문자열을 널 종료 문자열로 꺼낸다. out은 field_len + 1 바이트 이상.
 * 필드 안에 0이 있으면 거기서 끊는다.
 */
void wire_get_str(const uint8_t *p, size_t field_len, char *out);

/* --- 헤더 --- */

/*
 * 헤더를 buf에 쓴다. magic과 version은 여기서 채운다 — 호출부가 매번 채우면
 * 한 군데에서 빠뜨리는 일이 생긴다.
 *
 * 성공하면 쓴 바이트 수(WIRE_HEADER_LEN). cap이 모자라거나 body_len이 한도를
 * 넘으면 ERR_INVALID_ARG.
 */
int wire_encode_header(const wire_header_t *h, uint8_t *buf, size_t cap);

/*
 * 헤더를 읽는다. 성공하면 읽은 바이트 수(WIRE_HEADER_LEN).
 *
 * len이 헤더보다 짧으면 ERR_INVALID_ARG. 부분 수신 판단은 호출부가
 * len < WIRE_HEADER_LEN으로 먼저 한다(T3-09).
 * magic이 다르면 ERR_INVALID_ARG, version이 다르면 ERR_NOT_SUPPORTED,
 * body_len이 한도를 넘으면 ERR_INVALID_ARG.
 *
 * **version이 다를 때 해석을 시도하지 않는다.** 필드 위치가 바뀌었을 수 있고,
 * 그대로 읽으면 엉뚱한 값을 그럴듯하게 돌려준다.
 */
int wire_decode_header(const uint8_t *buf, size_t len, wire_header_t *out);

#endif /* MINI_SOR_WIRE_H */
