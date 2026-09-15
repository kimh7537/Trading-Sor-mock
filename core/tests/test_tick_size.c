/*
 * T1-03 호가 단위 테이블. docs/SPEC.md 3.2
 *
 * 구간 경계가 이 표의 유일한 위험 지점이다. 각 경계마다 (경계-1, 경계, 경계+1)을
 * 모두 본다. 2,000원이 1원 구간이 아니라 5원 구간에 속한다는 것이 핵심.
 */
#include <assert.h>
#include <stddef.h>

#include "tick_size.h"

static const struct {
    price_t price;
    price_t tick;
} CASES[] = {
    /* 하한 */
    {          1,    1 },
    {       1999,    1 },
    /* 2,000원 경계 — 2,000은 5원 구간이다 */
    {       2000,    5 },
    {       2001,    5 },
    {       4999,    5 },
    /* 5,000원 경계 */
    {       5000,   10 },
    {       5001,   10 },
    {      19999,   10 },
    /* 20,000원 경계 */
    {      20000,   50 },
    {      20001,   50 },
    {      49999,   50 },
    /* 50,000원 경계 */
    {      50000,  100 },
    {      50001,  100 },
    {     199999,  100 },
    /* 200,000원 경계 */
    {     200000,  500 },
    {     200001,  500 },
    {     499999,  500 },
    /* 500,000원 경계 — 이후는 전부 1,000원 */
    {     500000, 1000 },
    {     500001, 1000 },
    {  PRICE_MAX, 1000 }
};

#define CASES_LEN (sizeof(CASES) / sizeof(CASES[0]))

_Static_assert(CASES_LEN >= 15, "경계값 케이스가 15개 이상이어야 한다");

int main(void)
{
    for (size_t i = 0; i < CASES_LEN; i++) {
        assert(tick_size_of(CASES[i].price) == CASES[i].tick);
    }

    /* 범위 밖은 값으로 거른다 */
    assert(tick_size_of(0) == 0);
    assert(tick_size_of(-1) == 0);
    assert(tick_size_of(PRICE_MAX + 1) == 0);
    assert(is_valid_tick(0) == false);
    assert(is_valid_tick(PRICE_MAX + 1) == false);
    assert(round_to_tick(0, true) == 0);
    assert(round_to_tick(PRICE_MAX + 1, false) == 0);

    /* 구간 시작 가격은 그 구간의 호가 단위에 맞는다 — 내림이 구간 밖으로 새지 않는 근거 */
    assert(is_valid_tick(2000) && is_valid_tick(5000) && is_valid_tick(20000));
    assert(is_valid_tick(50000) && is_valid_tick(200000) && is_valid_tick(500000));

    assert(is_valid_tick(1999));   /* 1원 구간 */
    assert(!is_valid_tick(2001));  /* 5원 구간에서 어긋남 */
    assert(is_valid_tick(2005));
    assert(!is_valid_tick(5005));  /* 10원 구간 */
    assert(is_valid_tick(5010));
    assert(!is_valid_tick(20010)); /* 50원 구간 */
    assert(is_valid_tick(20050));

    /* 이미 맞는 가격은 올림·내림 모두 그대로 */
    assert(round_to_tick(2005, true) == 2005);
    assert(round_to_tick(2005, false) == 2005);

    assert(round_to_tick(2001, false) == 2000);
    assert(round_to_tick(2001, true) == 2005);
    assert(round_to_tick(1999, true) == 1999); /* 1원 구간이라 이미 유효 */

    /* 올림이 다음 구간으로 넘어가는 경우. 구간 시작 가격이므로 결과도 유효해야 한다 */
    assert(round_to_tick(4999, true) == 5000);
    assert(round_to_tick(19999, true) == 20000);
    assert(round_to_tick(49999, true) == 50000);
    assert(round_to_tick(199999, true) == 200000);
    assert(round_to_tick(499999, true) == 500000);

    /* 내림은 구간을 벗어나지 않는다 */
    assert(round_to_tick(5009, false) == 5000);
    assert(round_to_tick(20049, false) == 20000);
    assert(round_to_tick(500999, false) == 500000);

    /* 구간의 배타적 상한. 호가창이 구간 단위로 건너뛸 때 쓴다 */
    assert(tick_segment_end(1) == 2000);
    assert(tick_segment_end(1999) == 2000);
    assert(tick_segment_end(2000) == 5000);
    assert(tick_segment_end(4999) == 5000);
    assert(tick_segment_end(5000) == 20000);
    assert(tick_segment_end(20000) == 50000);
    assert(tick_segment_end(50000) == 200000);
    assert(tick_segment_end(200000) == 500000);
    assert(tick_segment_end(500000) == PRICE_MAX + 1);
    assert(tick_segment_end(PRICE_MAX) == PRICE_MAX + 1);
    assert(tick_segment_end(0) == 0);
    assert(tick_segment_end(PRICE_MAX + 1) == 0);

    /* 상한 바로 앞은 같은 구간, 상한은 다음 구간이다 */
    for (price_t b = 1; b < PRICE_MAX; b = tick_segment_end(b)) {
        price_t end = tick_segment_end(b);
        assert(tick_size_of(end - 1) == tick_size_of(b));
        if (end <= PRICE_MAX) {
            assert(tick_size_of(end) != tick_size_of(b));
            assert(is_valid_tick(end)); /* 구간 시작은 새 구간 단위에 맞는다 */
        }
    }

    /* 올림·내림 결과는 항상 유효 호가다 */
    for (price_t p = 1; p < 60000; p += 7) {
        price_t up = round_to_tick(p, true);
        price_t down = round_to_tick(p, false);
        assert(is_valid_tick(up));
        assert(is_valid_tick(down));
        assert(down <= p && p <= up);
        assert(up - down <= tick_size_of(p));
    }

    return 0;
}
