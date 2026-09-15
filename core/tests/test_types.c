/*
 * T1-02 기본 타입과 에러 코드.
 *
 * 타입의 폭·부호와 경계값 상수는 컴파일 타임에 _Static_assert로 확인하고,
 * err_str()만 런타임에서 본다.
 */
#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "errors.h"
#include "types.h"

/* 타입 폭이 바뀌면 전문 길이와 오버플로 가정이 함께 깨진다. */
_Static_assert(sizeof(price_t) == 4, "price_t는 32비트");
_Static_assert(sizeof(qty_t) == 4, "qty_t는 32비트");
_Static_assert(sizeof(order_id_t) == 8, "order_id_t는 64비트");
_Static_assert(sizeof(ts_t) == 8, "ts_t는 64비트");
_Static_assert((price_t)-1 < 0, "price_t는 부호 있는 타입");
_Static_assert((qty_t)-1 < 0, "qty_t는 부호 있는 타입");
_Static_assert((ts_t)-1 < 0, "ts_t는 부호 있는 타입");
_Static_assert((order_id_t)-1 > 0, "order_id_t는 부호 없는 타입");

/* 모든 에러 코드가 설명 문자열을 가진다. */
#define CHECK_ERR_STR(name, value, text)                    \
    do {                                                    \
        const char *s = err_str(name);                      \
        assert(s != NULL);                                  \
        assert(s[0] != '\0');                               \
        assert(strcmp(s, "알 수 없는 에러") != 0);          \
    } while (0);

/* 성공 0 / 실패 음수 규약. */
#define CHECK_ERR_SIGN(name, value, text)                   \
    assert((name) == ERR_OK ? (value) == 0 : (value) < 0);

int main(void)
{
    ERROR_CODE_LIST(CHECK_ERR_STR)
    ERROR_CODE_LIST(CHECK_ERR_SIGN)

    /* 정의되지 않은 코드도 NULL을 반환하지 않는다. */
    assert(err_str(-9999) != NULL);
    assert(err_str(12345) != NULL);
    assert(err_str(INT32_MIN) != NULL);

    /* 경계값 상수가 각 타입 범위 안에 있다. */
    assert(PRICE_MIN >= 1 && PRICE_MAX <= INT32_MAX);
    assert(QTY_MIN >= 1 && QTY_MAX <= INT32_MAX);
    assert(ORDER_ID_INVALID == 0);
    assert(TS_INVALID < 0);
    assert(MARKET_COUNT == 2);

    /* 열거형 값이 배열 첨자로 바로 쓰이므로 0부터 시작해야 한다. */
    assert(SIDE_BUY == 0 && SIDE_SELL == 1);
    assert(MARKET_KRX == 0 && MARKET_NXT == 1);
    assert(ORDER_LIMIT == 0);
    assert(STATUS_NEW == 0);

    return 0;
}
