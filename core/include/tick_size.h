#ifndef MINI_SOR_TICK_SIZE_H
#define MINI_SOR_TICK_SIZE_H

#include <stdbool.h>

#include "types.h"

/*
 * 호가 단위 (docs/SPEC.md 3.2)
 *
 * 네 함수 모두 유효 가격 범위를 [PRICE_MIN, PRICE_MAX]로 본다.
 * 범위 밖 입력은 외부 입력이므로 assert가 아니라 값으로 거른다 —
 * tick_size_in()과 round_to_tick_in()은 0을, is_valid_tick_in()은 false를 반환한다.
 * 0은 어떤 가격대에서도 정상적인 호가 단위가 아니므로 실패 표시로 쓸 수 있다.
 *
 * ===========================================================================
 * 표가 둘이다 (T10-01)
 * ===========================================================================
 *
 * 국내와 미국은 호가 단위 규칙이 다르다. 미국 종목을 국내 표로 다루면
 * $191.23(19,123센트)이 "50원 단위" 구간에 걸려 **정상 가격이 거절된다.**
 *
 * **미국 가격은 센트 정수로 다룬다.** `price_t`가 정수라 소수점을 담을 수 없고,
 * 센트로 세면 1센트가 곧 1이 되어 나머지 계산이 그대로 성립한다. 화면이
 * 100으로 나눠 달러로 보여 준다.
 *
 * 1센트 고정은 미국 규칙(SEC Rule 612)의 **$1 이상 구간**이다. $1 미만은
 * 0.01센트까지 쪼개지는데, 그 가격대 종목을 다룰 생각이 없어 넣지 않았다.
 *
 * 표를 고르는 값은 **호가창이 들고 다닌다.** 전역에 두면 종목을 바꾸는 동안
 * 새 호가창과 옛 호가창이 잠시 함께 있을 때 한쪽이 남의 표를 쓰게 된다.
 */
typedef enum {
    TICK_TABLE_KRX = 0, /* 국내. 원 단위, 가격대별 1~1000원 */
    TICK_TABLE_US = 1   /* 미국. 센트 단위, 1센트 고정 */
} tick_table_t;

/* 해당 가격의 호가 단위. 범위 밖이면 0. */
price_t tick_size_in(tick_table_t table, price_t price);

/* 호가 단위에 맞는 가격인가. 범위 밖이면 false. */
bool is_valid_tick_in(tick_table_t table, price_t price);

/* 호가 단위로 올림(up=true) 또는 내림. 이미 맞으면 그대로. 범위 밖이면 0. */
price_t round_to_tick_in(tick_table_t table, price_t price, bool up);

/*
 * price가 속한 호가 단위 구간의 배타적 상한. 범위 밖이면 0.
 * 예: tick_segment_end_in(TICK_TABLE_KRX, 3000) == 5000
 *     (5원 구간은 2,000 이상 5,000 미만).
 * 호가창이 가격을 배열 인덱스로 접을 때 구간 단위로 건너뛰기 위해 쓴다.
 */
price_t tick_segment_end_in(tick_table_t table, price_t price);

/*
 * --- 국내 표를 쓰는 짧은 이름 ---
 *
 * 표가 하나였을 때부터 있던 이름이다. 국내만 다루는 자리(벤치마크, 국내 전용
 * 테스트)에서 인자를 하나 덜 적게 하려고 남겨 둔다.
 */
static inline price_t tick_size_of(price_t price)
{
    return tick_size_in(TICK_TABLE_KRX, price);
}

static inline bool is_valid_tick(price_t price)
{
    return is_valid_tick_in(TICK_TABLE_KRX, price);
}

static inline price_t round_to_tick(price_t price, bool up)
{
    return round_to_tick_in(TICK_TABLE_KRX, price, up);
}

static inline price_t tick_segment_end(price_t price)
{
    return tick_segment_end_in(TICK_TABLE_KRX, price);
}

#endif /* MINI_SOR_TICK_SIZE_H */
