/*
 * T1-06 호가창.
 *
 * 위험 지점 둘.
 *  1. 가격 <-> 인덱스가 정말 일대일인가. 제한폭이 호가 단위 구간 경계를 걸치면
 *     구간마다 칸 폭이 달라진다. 구간을 걸치는 기준가로 전 가격을 훑어 확인한다.
 *  2. 최우선호가 캐시가 실제 호가창과 어긋나지 않는가. 매 연산 뒤에 캐시된 값을
 *     전수 조사로 구한 값과 대조한다.
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "order_book.h"
#include "tick_size.h"

#define POOL_CAP 1024

/*
 * 캐시를 믿지 않고 직접 찾은 최우선호가. book_qty_at()만 쓰므로 내부를 들여다보지 않는다.
 * 테스트에서만 하는 O(범위) 검사다.
 */
static price_t brute_best(const order_book_t *book, side_t side)
{
    price_t found = BOOK_PRICE_NONE;

    for (price_t p = book_price_low(book); p <= book_price_high(book);
         p += tick_size_of(p)) {
        if (book_qty_at(book, side, p) <= 0) {
            continue;
        }
        if (side == SIDE_SELL) {
            return p; /* 매도는 낮은 가격이 우선 */
        }
        found = p; /* 매수는 끝까지 가서 가장 높은 가격 */
    }
    return found;
}

static void check_best_cache(const order_book_t *book)
{
    assert(book_best_bid(book) == brute_best(book, SIDE_BUY));
    assert(book_best_ask(book) == brute_best(book, SIDE_SELL));
}

static order_t *make_order(order_pool_t *pool, side_t side, price_t price,
                           qty_t qty)
{
    order_t *o = order_pool_acquire(pool);
    assert(o != NULL);
    o->side = side;
    o->price = price;
    o->qty = qty;
    return o;
}

/* 생성 거부와 제한폭 계산 */
static void test_create(void)
{
    assert(book_create(0) == NULL);
    assert(book_create(-1) == NULL);
    assert(book_create(PRICE_MAX + 1) == NULL);
    book_destroy(NULL);

    /* 기준가 70,000 -> 49,000 ~ 91,000. 49,000대는 50원, 50,000 이상은 100원 단위 */
    order_book_t *book = book_create(70000);
    assert(book != NULL);
    assert(book_price_low(book) == 49000);
    assert(book_price_high(book) == 91000);
    assert(is_valid_tick(book_price_low(book)));
    assert(is_valid_tick(book_price_high(book)));
    book_destroy(book);

    /* 상한이 PRICE_MAX를 넘는 기준가도 만들어져야 한다 */
    order_book_t *top = book_create(PRICE_MAX);
    assert(top != NULL);
    assert(book_price_high(top) == PRICE_MAX);
    book_destroy(top);

    /* 제한폭 경계가 호가 단위에 안 맞는 기준가. 7,778 의 ±30%는 5,444.6 과 10,111.4 다.
     * 이 가격대 호가 단위는 10원이므로 상하한 모두 어긋난다. 하한은 올리고(5,450)
     * 상한은 내려야(10,110) 제한폭 안에 남는다. 반대로 하면 제한폭 밖 가격을 받는다. */
    order_book_t *odd = book_create(7778);
    assert(odd != NULL);
    assert(book_price_low(odd) == 5450);
    assert(book_price_low(odd) * 100 >= 7778 * (100 - PRICE_LIMIT_PCT));
    assert(book_price_high(odd) == 10110);
    assert(book_price_high(odd) * 100 <= 7778 * (100 + PRICE_LIMIT_PCT));
    book_destroy(odd);

    /* 하한이 PRICE_MIN 아래로 내려가는 기준가 */
    order_book_t *bottom = book_create(1);
    assert(bottom != NULL);
    assert(book_price_low(bottom) == PRICE_MIN);
    book_destroy(bottom);
}

/*
 * 가격 <-> 인덱스 일대일. 기준가 2,500이면 제한폭이 1,750~3,250이라
 * 1원 구간과 5원 구간을 함께 걸친다.
 */
static void test_index_bijection(order_pool_t *pool)
{
    order_book_t *book = book_create(2500);
    assert(book != NULL);
    assert(book_price_low(book) == 1750);
    assert(book_price_high(book) == 3250);

    int n = 0;
    for (price_t p = book_price_low(book); p <= book_price_high(book);
         p += tick_size_of(p)) {
        order_t *o = make_order(pool, SIDE_BUY, p, 10);
        assert(book_insert(book, o) == ERR_OK);
        n++;
        assert(n <= POOL_CAP);
    }
    /* 1,750~1,999 는 1원 단위 250개, 2,000~3,250 은 5원 단위 251개 */
    assert(n == 250 + 251);

    /* 두 가격이 같은 칸에 겹쳤다면 어딘가는 10이 아니라 20이 된다 */
    for (price_t p = book_price_low(book); p <= book_price_high(book);
         p += tick_size_of(p)) {
        assert(book_qty_at(book, SIDE_BUY, p) == 10);
    }
    assert(book_best_bid(book) == 3250);
    check_best_cache(book);

    /* 구간 경계 바로 앞뒤가 서로 다른 칸이다 */
    assert(book_qty_at(book, SIDE_BUY, 1999) == 10);
    assert(book_qty_at(book, SIDE_BUY, 2000) == 10);
    assert(book_qty_at(book, SIDE_BUY, 2001) == 0); /* 5원 구간에서 어긋난 가격 */

    book_destroy(book);
}

/* 빈 호가창 */
static void test_empty(void)
{
    order_book_t *book = book_create(10000);
    assert(book != NULL);

    assert(book_best_bid(book) == BOOK_PRICE_NONE);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);
    assert(book_qty_at(book, SIDE_BUY, 10000) == 0);
    assert(book_qty_at(book, SIDE_SELL, 10000) == 0);

    level_view_t view[10];
    assert(book_snapshot(book, SIDE_BUY, 10, view) == 0);
    assert(book_snapshot(book, SIDE_SELL, 10, view) == 0);

    /* 범위 밖 조회는 0이지 크래시가 아니다 */
    assert(book_qty_at(book, SIDE_BUY, 1) == 0);
    assert(book_qty_at(book, SIDE_BUY, PRICE_MAX) == 0);
    assert(book_qty_at(NULL, SIDE_BUY, 10000) == 0);

    book_destroy(book);
}

/* 삽입 거부 */
static void test_insert_rejects(order_pool_t *pool)
{
    order_book_t *book = book_create(10000);
    assert(book != NULL);
    price_t low = book_price_low(book);
    price_t high = book_price_high(book);

    order_t *below = make_order(pool, SIDE_BUY, low - tick_size_of(low), 10);
    assert(book_insert(book, below) == ERR_PRICE_LIMIT);

    order_t *above = make_order(pool, SIDE_SELL, high + tick_size_of(high), 10);
    assert(book_insert(book, above) == ERR_PRICE_LIMIT);

    /* 10,000원대 호가 단위는 10원이다 */
    order_t *offtick = make_order(pool, SIDE_BUY, 10005, 10);
    assert(book_insert(book, offtick) == ERR_INVALID_TICK);

    order_t *nothing = make_order(pool, SIDE_BUY, 10000, 10);
    nothing->filled_qty = nothing->qty;
    assert(book_insert(book, nothing) == ERR_INVALID_QTY);

    assert(book_insert(NULL, nothing) == ERR_NULL_PTR);
    assert(book_insert(book, NULL) == ERR_NULL_PTR);

    /* 거부된 주문은 어디에도 들어가지 않았다 */
    assert(book_best_bid(book) == BOOK_PRICE_NONE);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);
    check_best_cache(book);

    order_pool_release(pool, below);
    order_pool_release(pool, above);
    order_pool_release(pool, offtick);
    order_pool_release(pool, nothing);
    book_destroy(book);
}

/* 삽입·제거 후 최우선호가 */
static void test_best_tracking(order_pool_t *pool)
{
    order_book_t *book = book_create(10000);
    assert(book != NULL);

    order_t *b1 = make_order(pool, SIDE_BUY, 9900, 100);
    order_t *b2 = make_order(pool, SIDE_BUY, 9950, 200); /* 더 높은 매수 */
    order_t *b3 = make_order(pool, SIDE_BUY, 9950, 300); /* 같은 레벨 */
    order_t *a1 = make_order(pool, SIDE_SELL, 10100, 100);
    order_t *a2 = make_order(pool, SIDE_SELL, 10050, 200); /* 더 낮은 매도 */

    assert(book_insert(book, b1) == ERR_OK);
    assert(book_best_bid(book) == 9900);
    assert(book_insert(book, b2) == ERR_OK);
    assert(book_best_bid(book) == 9950); /* 갱신 */
    assert(book_insert(book, b3) == ERR_OK);
    assert(book_best_bid(book) == 9950); /* 같은 가격이면 그대로 */
    assert(book_qty_at(book, SIDE_BUY, 9950) == 500);

    assert(book_insert(book, a1) == ERR_OK);
    assert(book_best_ask(book) == 10100);
    assert(book_insert(book, a2) == ERR_OK);
    assert(book_best_ask(book) == 10050);
    check_best_cache(book);

    /* 최우선 레벨의 한 주문만 빠지면 최우선호가는 그대로다 */
    assert(book_remove(book, b2) == ERR_OK);
    assert(book_best_bid(book) == 9950);
    assert(book_qty_at(book, SIDE_BUY, 9950) == 300);
    check_best_cache(book);

    /* 최우선 레벨이 비면 다음 호가로 내려간다 */
    assert(book_remove(book, b3) == ERR_OK);
    assert(book_best_bid(book) == 9900);
    assert(book_qty_at(book, SIDE_BUY, 9950) == 0);
    check_best_cache(book);

    /* 최우선이 아닌 주문을 빼면 최우선호가는 건드리지 않는다 */
    assert(book_remove(book, a1) == ERR_OK);
    assert(book_best_ask(book) == 10050);
    check_best_cache(book);

    /* 다 비우면 센티넬로 돌아간다 */
    assert(book_remove(book, b1) == ERR_OK);
    assert(book_remove(book, a2) == ERR_OK);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);
    assert(book_best_ask(book) == BOOK_PRICE_NONE);
    check_best_cache(book);

    /* 비운 뒤 다시 넣어도 캐시가 살아난다 */
    assert(book_insert(book, b1) == ERR_OK);
    assert(book_best_bid(book) == 9900);
    check_best_cache(book);
    assert(book_remove(book, b1) == ERR_OK);

    order_pool_release(pool, b1);
    order_pool_release(pool, b2);
    order_pool_release(pool, b3);
    order_pool_release(pool, a1);
    order_pool_release(pool, a2);
    book_destroy(book);
}

/* 10단 스냅샷 */
static void test_snapshot(order_pool_t *pool)
{
    order_book_t *book = book_create(10000);
    assert(book != NULL);

    /* 매수 12단, 매도 12단. 빈 레벨을 사이에 두려고 20원씩 띄운다 */
    order_t *bids[12];
    order_t *asks[12];
    for (int i = 0; i < 12; i++) {
        bids[i] = make_order(pool, SIDE_BUY, 9980 - i * 20, (qty_t)((i + 1) * 10));
        asks[i] = make_order(pool, SIDE_SELL, 10020 + i * 20, (qty_t)((i + 1) * 10));
        assert(book_insert(book, bids[i]) == ERR_OK);
        assert(book_insert(book, asks[i]) == ERR_OK);
    }
    check_best_cache(book);

    level_view_t view[12];

    /* 매수는 높은 가격부터 */
    assert(book_snapshot(book, SIDE_BUY, 10, view) == 10);
    for (int i = 0; i < 10; i++) {
        assert(view[i].price == 9980 - i * 20);
        assert(view[i].total_qty == (qty_t)((i + 1) * 10));
        assert(view[i].order_count == 1);
        if (i > 0) {
            assert(view[i].price < view[i - 1].price);
        }
    }

    /* 매도는 낮은 가격부터 */
    assert(book_snapshot(book, SIDE_SELL, 10, view) == 10);
    for (int i = 0; i < 10; i++) {
        assert(view[i].price == 10020 + i * 20);
        assert(view[i].total_qty == (qty_t)((i + 1) * 10));
        if (i > 0) {
            assert(view[i].price > view[i - 1].price);
        }
    }

    /* 스냅샷 첫 줄은 최우선호가와 같아야 한다 */
    assert(book_snapshot(book, SIDE_BUY, 1, view) == 1);
    assert(view[0].price == book_best_bid(book));
    assert(book_snapshot(book, SIDE_SELL, 1, view) == 1);
    assert(view[0].price == book_best_ask(book));

    /* 있는 것보다 깊게 요청하면 있는 만큼만 채운다 */
    assert(book_snapshot(book, SIDE_BUY, 12, view) == 12);
    assert(book_snapshot(book, SIDE_SELL, 12, view) == 12);

    assert(book_snapshot(book, SIDE_BUY, 0, view) == ERR_INVALID_ARG);
    assert(book_snapshot(book, SIDE_BUY, -1, view) == ERR_INVALID_ARG);
    assert(book_snapshot(book, SIDE_BUY, 10, NULL) == ERR_NULL_PTR);
    assert(book_snapshot(NULL, SIDE_BUY, 10, view) == ERR_NULL_PTR);

    /* 중간 레벨을 비우면 그 줄만 빠지고 나머지가 당겨진다 */
    assert(book_remove(book, bids[2]) == ERR_OK);
    assert(book_snapshot(book, SIDE_BUY, 10, view) == 10);
    assert(view[2].price == 9980 - 3 * 20);
    check_best_cache(book);

    for (int i = 0; i < 12; i++) {
        if (i != 2) {
            assert(book_remove(book, bids[i]) == ERR_OK);
        }
        assert(book_remove(book, asks[i]) == ERR_OK);
        order_pool_release(pool, bids[i]);
        order_pool_release(pool, asks[i]);
    }
    book_destroy(book);
}

int main(void)
{
    order_pool_t *pool = order_pool_create(POOL_CAP);
    assert(pool != NULL);

    test_create();
    test_empty();
    test_index_bijection(pool);
    test_insert_rejects(pool);
    test_best_tracking(pool);
    test_snapshot(pool);

    order_pool_destroy(pool);
    return 0;
}
