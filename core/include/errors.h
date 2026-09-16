#ifndef MINI_SOR_ERRORS_H
#define MINI_SOR_ERRORS_H

/*
 * 에러 코드. 성공은 0, 실패는 음수.
 *
 * 열거형과 설명 문자열을 한 목록(ERROR_CODE_LIST)에서 함께 만든다.
 * 코드를 추가하면 err_str()의 문자열도 같이 따라온다 — 둘을 따로 관리하다
 * 한쪽만 빠뜨리는 것을 막기 위한 것이다. 테스트도 이 목록을 순회한다.
 */

#define ERROR_CODE_LIST(X)                                                 \
    X(ERR_OK,             0,   "성공")                                     \
    X(ERR_INVALID_ARG,   -1,   "잘못된 인자")                              \
    X(ERR_NULL_PTR,      -2,   "널 포인터")                                \
    X(ERR_INVALID_PRICE, -3,   "가격이 유효 범위를 벗어남")                \
    X(ERR_INVALID_QTY,   -4,   "수량이 유효 범위를 벗어남")                \
    X(ERR_INVALID_TICK,  -5,   "호가 단위에 맞지 않는 가격")               \
    X(ERR_PRICE_LIMIT,   -6,   "가격 제한폭 초과")                         \
    X(ERR_MARKET_CLOSED, -7,   "해당 시장이 열려 있지 않음")               \
    X(ERR_NOT_SUPPORTED, -8,   "해당 시장이 지원하지 않는 주문 유형")      \
    X(ERR_NOT_FOUND,     -9,   "주문을 찾을 수 없음")                      \
    X(ERR_DUPLICATE,     -10,  "이미 존재하는 주문 식별자")                \
    X(ERR_POOL_EXHAUSTED,-11,  "사전 할당 풀 소진")                        \
    X(ERR_BOOK_FULL,     -12,  "호가창 용량 초과")                        \
    X(ERR_NO_LIQUIDITY,  -13,  "체결할 반대 호가가 없음")                  \
    X(ERR_NO_MARGIN,     -14,  "증거금이 모자람")                          \
    X(ERR_LIMIT_EXCEEDED,-15,  "주문 한도 초과")                          \
    X(ERR_IO,            -16,  "입출력 실패 (상대 끊김 포함)")

#define ERROR_ENUM_ENTRY(name, value, text) name = (value),

typedef enum {
    ERROR_CODE_LIST(ERROR_ENUM_ENTRY)
} error_code_t;

#undef ERROR_ENUM_ENTRY

/*
 * 에러 코드 설명을 반환한다.
 * 정의되지 않은 코드를 받아도 NULL을 반환하지 않는다 — 호출부가 로그 경로에서
 * 널 검사를 하지 않아도 되게 한다.
 */
const char *err_str(int code);

#endif /* MINI_SOR_ERRORS_H */
