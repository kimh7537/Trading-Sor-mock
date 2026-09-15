#include "order_book.h"

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

#include "errors.h"
#include "tick_size.h"

/*
 * 호가 단위 구간은 표에 7개뿐이고(docs/SPEC.md 3.2), 제한폭이 그보다 많은 구간을
 * 걸칠 수는 없다. 여유 한 칸을 더 둔다.
 */
#define BOOK_SEGMENTS_MAX 8

/* 한 호가 단위 구간이 호가창 배열에서 차지하는 몫. */
typedef struct {
    price_t low;      /* 이 구간에서 호가창이 다루는 첫 가격. 호가 단위에 정렬돼 있다 */
    price_t tick;
    int32_t base_idx; /* low에 대응하는 배열 인덱스 */
    int32_t count;    /* 이 구간이 차지하는 칸 수 */
} book_segment_t;

struct order_book {
    price_t base_price;
    price_t low;  /* 제한폭 하한을 호가 단위로 올림한 값 */
    price_t high; /* 제한폭 상한을 호가 단위로 내림한 값 */

    book_segment_t seg[BOOK_SEGMENTS_MAX];
    int32_t seg_count;
    int32_t level_count;

    price_level_t *levels[2]; /* [SIDE_BUY], [SIDE_SELL] */
    price_t best[2];          /* 최우선호가 캐시. 없으면 BOOK_PRICE_NONE */
};

/* --- 가격 <-> 인덱스 --- */

/* 유효 호가가 아니거나 범위 밖이면 -1. */
static int32_t price_to_index(const order_book_t *book, price_t price)
{
    if (price < book->low || price > book->high) {
        return -1;
    }

    for (int32_t i = 0; i < book->seg_count; i++) {
        const book_segment_t *s = &book->seg[i];
        price_t span = s->count * s->tick; /* 이 구간이 덮는 가격 폭 */

        if (price < s->low + span) {
            price_t off = price - s->low;
            if (off % s->tick != 0) {
                return -1; /* 호가 단위에 어긋난 가격 */
            }
            return s->base_idx + off / s->tick;
        }
    }
    return -1;
}

static price_t index_to_price(const order_book_t *book, int32_t idx)
{
    assert(idx >= 0 && idx < book->level_count);

    for (int32_t i = 0; i < book->seg_count; i++) {
        const book_segment_t *s = &book->seg[i];
        if (idx < s->base_idx + s->count) {
            return s->low + (idx - s->base_idx) * s->tick;
        }
    }

    assert(0 && "구간 표가 배열 전체를 덮지 못했다");
    return BOOK_PRICE_NONE;
}

/* --- 생성 --- */

/* 제한폭 경계를 유효 호가로 정렬한다. 실패하면 false. */
static bool compute_band(price_t base, price_t *out_low, price_t *out_high)
{
    /* types.h의 _Static_assert가 이 곱셈이 price_t를 넘지 않음을 보장한다. */
    price_t raw_low = base * (100 - PRICE_LIMIT_PCT) / 100;
    price_t raw_high = base * (100 + PRICE_LIMIT_PCT) / 100;

    /* 정렬 전에 잘라낸다. round_to_tick은 범위 밖 입력에 0을 돌려준다. */
    if (raw_low < PRICE_MIN) {
        raw_low = PRICE_MIN;
    }
    if (raw_high > PRICE_MAX) {
        raw_high = PRICE_MAX;
    }

    price_t low = round_to_tick(raw_low, true);    /* 하한 이상의 첫 유효 호가 */
    price_t high = round_to_tick(raw_high, false); /* 상한 이하의 마지막 유효 호가 */

    if (low == 0 || high == 0 || low > high) {
        return false;
    }

    *out_low = low;
    *out_high = high;
    return true;
}

/* [low, high]를 호가 단위 구간으로 쪼개 배열 배치를 정한다. 실패하면 false. */
static bool build_segments(order_book_t *book)
{
    int32_t idx = 0;
    price_t p = book->low;

    while (p <= book->high) {
        if (book->seg_count >= BOOK_SEGMENTS_MAX) {
            return false;
        }

        price_t tick = tick_size_of(p);
        price_t seg_end = tick_segment_end(p); /* 배타적 상한 */
        assert(tick > 0 && seg_end > p);
        assert(p % tick == 0);

        price_t last = (seg_end - 1 < book->high) ? seg_end - 1 : book->high;
        last -= last % tick; /* 이 구간 안의 마지막 유효 호가 */

        book_segment_t *s = &book->seg[book->seg_count++];
        s->low = p;
        s->tick = tick;
        s->base_idx = idx;
        s->count = (last - p) / tick + 1;
        idx += s->count;

        /* 구간 시작 가격은 새 구간의 호가 단위에도 맞는다(test_tick_size.c에서 확인). */
        p = seg_end;
    }

    book->level_count = idx;
    return book->level_count > 0;
}

order_book_t *book_create(price_t base_price)
{
    if (base_price < PRICE_MIN || base_price > PRICE_MAX) {
        return NULL;
    }

    order_book_t *book = calloc(1, sizeof(*book));
    if (book == NULL) {
        return NULL;
    }
    book->base_price = base_price;
    book->best[SIDE_BUY] = BOOK_PRICE_NONE;
    book->best[SIDE_SELL] = BOOK_PRICE_NONE;

    if (!compute_band(base_price, &book->low, &book->high) ||
        !build_segments(book)) {
        book_destroy(book);
        return NULL;
    }

    book->levels[SIDE_BUY] = calloc((size_t)book->level_count,
                                    sizeof(*book->levels[SIDE_BUY]));
    book->levels[SIDE_SELL] = calloc((size_t)book->level_count,
                                     sizeof(*book->levels[SIDE_SELL]));
    if (book->levels[SIDE_BUY] == NULL || book->levels[SIDE_SELL] == NULL) {
        book_destroy(book);
        return NULL;
    }

    return book;
}

void book_destroy(order_book_t *book)
{
    if (book == NULL) {
        return;
    }
    free(book->levels[SIDE_BUY]);
    free(book->levels[SIDE_SELL]);
    free(book);
}

price_t book_price_low(const order_book_t *book)
{
    return book != NULL ? book->low : BOOK_PRICE_NONE;
}

price_t book_price_high(const order_book_t *book)
{
    return book != NULL ? book->high : BOOK_PRICE_NONE;
}

/* --- 최우선호가 --- */

/*
 * 최우선호가를 다시 찾는다. 매수는 높은 쪽에서, 매도는 낮은 쪽에서 훑는다.
 * ponytail: 선형 스캔. 레벨 수가 수천 단위라 문제없다. 병목이 되면 비어있지 않은
 * 레벨 비트맵을 얹어 64칸씩 건너뛴다.
 */
static void recompute_best(order_book_t *book, side_t side)
{
    const price_level_t *levels = book->levels[side];

    if (side == SIDE_BUY) {
        for (int32_t i = book->level_count - 1; i >= 0; i--) {
            if (levels[i].order_count > 0) {
                book->best[side] = index_to_price(book, i);
                return;
            }
        }
    } else {
        for (int32_t i = 0; i < book->level_count; i++) {
            if (levels[i].order_count > 0) {
                book->best[side] = index_to_price(book, i);
                return;
            }
        }
    }
    book->best[side] = BOOK_PRICE_NONE;
}

price_t book_best_bid(const order_book_t *book)
{
    return book != NULL ? book->best[SIDE_BUY] : BOOK_PRICE_NONE;
}

price_t book_best_ask(const order_book_t *book)
{
    return book != NULL ? book->best[SIDE_SELL] : BOOK_PRICE_NONE;
}

/* --- 삽입·제거 --- */

int book_insert(order_book_t *book, order_t *order)
{
    if (book == NULL || order == NULL) {
        return ERR_NULL_PTR;
    }
    if (order->side != SIDE_BUY && order->side != SIDE_SELL) {
        return ERR_INVALID_ARG;
    }
    if (order->price < book->low || order->price > book->high) {
        return ERR_PRICE_LIMIT;
    }

    int32_t idx = price_to_index(book, order->price);
    if (idx < 0) {
        return ERR_INVALID_TICK;
    }

    int rc = level_push_back(&book->levels[order->side][idx], order);
    if (rc != ERR_OK) {
        return rc;
    }

    /* 새 주문이 최우선호가를 바꾸는지는 비교 한 번이면 안다. 스캔할 일이 없다. */
    price_t best = book->best[order->side];
    if (best == BOOK_PRICE_NONE ||
        (order->side == SIDE_BUY ? order->price > best : order->price < best)) {
        book->best[order->side] = order->price;
    }

    return ERR_OK;
}

int book_remove(order_book_t *book, order_t *order)
{
    if (book == NULL || order == NULL) {
        return ERR_NULL_PTR;
    }

    int32_t idx = price_to_index(book, order->price);
    /* 이 호가창에 들어간 적 없는 주문이다 — 넣은 쪽의 버그다. */
    assert(idx >= 0);
    if (idx < 0) {
        return ERR_NOT_FOUND;
    }

    price_level_t *level = &book->levels[order->side][idx];
    int rc = level_remove(level, order);
    if (rc != ERR_OK) {
        return rc;
    }

    /* 최우선호가 레벨이 비었을 때만 다음 호가를 찾는다. */
    if (level->order_count == 0 && order->price == book->best[order->side]) {
        recompute_best(book, order->side);
    }

    return ERR_OK;
}

/* --- 조회 --- */

qty_t book_qty_at(const order_book_t *book, side_t side, price_t price)
{
    if (book == NULL || (side != SIDE_BUY && side != SIDE_SELL)) {
        return 0;
    }

    int32_t idx = price_to_index(book, price);
    if (idx < 0) {
        return 0;
    }
    return book->levels[side][idx].total_qty;
}

int book_snapshot(const order_book_t *book, side_t side, int depth,
                  level_view_t *out)
{
    if (book == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (depth <= 0 || (side != SIDE_BUY && side != SIDE_SELL)) {
        return ERR_INVALID_ARG;
    }

    const price_level_t *levels = book->levels[side];
    /* 매수는 높은 가격이 우선이므로 배열을 거꾸로 훑는다. */
    int32_t step = (side == SIDE_BUY) ? -1 : 1;
    int32_t i = (side == SIDE_BUY) ? book->level_count - 1 : 0;
    int filled = 0;

    for (; filled < depth && i >= 0 && i < book->level_count; i += step) {
        if (levels[i].order_count == 0) {
            continue;
        }
        out[filled].price = index_to_price(book, i);
        out[filled].total_qty = levels[i].total_qty;
        out[filled].order_count = levels[i].order_count;
        filled++;
    }

    return filled;
}
