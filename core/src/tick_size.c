#include "tick_size.h"

#include <assert.h>
#include <stddef.h>

/*
 * 가격 구간별 호가 단위. below는 구간의 배타적 상한이다.
 * "2,000원은 5원 단위 구간에 속한다"(SPEC 3.2)를 이 형태로 표현한다 —
 * 2,000원 미만이 1원, 2,000원부터가 5원.
 */
static const struct {
    price_t below;
    price_t tick;
} TICK_TABLE[] = {
    {      2000,    1 },
    {      5000,    5 },
    {     20000,   10 },
    {     50000,   50 },
    {    200000,  100 },
    {    500000,  500 },
    { PRICE_MAX + 1, 1000 }
};

#define TICK_TABLE_LEN (sizeof(TICK_TABLE) / sizeof(TICK_TABLE[0]))

static bool in_range(price_t price)
{
    return price >= PRICE_MIN && price <= PRICE_MAX;
}

price_t tick_size_of(price_t price)
{
    if (!in_range(price)) {
        return 0;
    }

    for (size_t i = 0; i < TICK_TABLE_LEN; i++) {
        if (price < TICK_TABLE[i].below) {
            return TICK_TABLE[i].tick;
        }
    }

    assert(0 && "표 마지막 구간이 PRICE_MAX를 덮지 못했다");
    return 0;
}

bool is_valid_tick(price_t price)
{
    price_t tick = tick_size_of(price);

    if (tick == 0) {
        return false;
    }
    return price % tick == 0;
}

price_t round_to_tick(price_t price, bool up)
{
    price_t tick = tick_size_of(price);

    if (tick == 0) {
        return 0;
    }

    price_t rem = price % tick;

    if (rem == 0) {
        return price;
    }
    if (!up) {
        /* 각 구간의 시작 가격은 그 구간의 호가 단위로 나누어떨어진다.
         * 따라서 내림 결과가 구간 아래로 새지 않는다. */
        price_t down = price - rem;
        assert(tick_size_of(down) == tick);
        return down;
    }

    price_t result = price - rem + tick;

    /* 올림은 다음 구간으로 넘어갈 수 있다(예: 4,999 -> 5,000).
     * 구간 시작 가격은 새 구간의 호가 단위에도 맞으므로 결과는 여전히 유효하다. */
    if (result > PRICE_MAX) {
        return 0;
    }
    assert(is_valid_tick(result));
    return result;
}

price_t tick_segment_end(price_t price)
{
    if (!in_range(price)) {
        return 0;
    }

    for (size_t i = 0; i < TICK_TABLE_LEN; i++) {
        if (price < TICK_TABLE[i].below) {
            return TICK_TABLE[i].below;
        }
    }

    assert(0 && "표 마지막 구간이 PRICE_MAX를 덮지 못했다");
    return 0;
}
