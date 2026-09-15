#ifndef MINI_SOR_TICK_SIZE_H
#define MINI_SOR_TICK_SIZE_H

#include <stdbool.h>

#include "types.h"

/*
 * 호가 단위 (docs/SPEC.md 3.2)
 *
 * 세 함수 모두 유효 가격 범위를 [PRICE_MIN, PRICE_MAX]로 본다.
 * 범위 밖 입력은 외부 입력이므로 assert가 아니라 값으로 거른다 —
 * tick_size_of()와 round_to_tick()은 0을, is_valid_tick()은 false를 반환한다.
 * 0은 어떤 가격대에서도 정상적인 호가 단위가 아니므로 실패 표시로 쓸 수 있다.
 */

/* 해당 가격의 호가 단위. 범위 밖이면 0. */
price_t tick_size_of(price_t price);

/* 호가 단위에 맞는 가격인가. 범위 밖이면 false. */
bool is_valid_tick(price_t price);

/* 호가 단위로 올림(up=true) 또는 내림. 이미 맞으면 그대로. 범위 밖이면 0. */
price_t round_to_tick(price_t price, bool up);

#endif /* MINI_SOR_TICK_SIZE_H */
