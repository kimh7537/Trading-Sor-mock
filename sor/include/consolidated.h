#ifndef MINI_SOR_CONSOLIDATED_H
#define MINI_SOR_CONSOLIDATED_H

#include <stdbool.h>
#include <stdint.h>

#include "market_rules.h"
#include "order_book.h"
#include "types.h"

/*
 * 통합 호가창 — 두 시장의 호가를 하나로 겹쳐 본다.
 *
 * SOR이 "어느 시장이 유리한가"를 판단하려면 먼저 양쪽을 같은 자리에 놓고 봐야 한다.
 * 이 구조체는 시장 호가창을 **소유하지 않는다.** 참조만 들고 매번 물어본다.
 *
 * 캐시를 두지 않는 이유는 무효화 시점이 또 하나의 버그 원천이기 때문이다.
 * 체결·취소·정정 어느 것이든 호가를 바꾸는데, 그때마다 통합 뷰에 알리는 경로를
 * 만들면 알리는 것을 빠뜨린 경로가 곧 조용한 오답이 된다. 시장 호가창의 최우선호가
 * 조회는 이미 O(1)이므로(T1-06 캐시), 매번 묻는 비용이 그 위험을 살 만큼 크지 않다.
 *
 * 열려 있지 않은 시장은 통합 뷰에서 빠진다. NXT만 열려 있는 구간(08:00~09:00,
 * 15:30~20:00)에서는 통합 최우선호가가 곧 NXT의 호가다.
 */

/* 통합 호가 한 줄. 어느 시장의 호가인지가 함께 온다. */
typedef struct {
    price_t  price;
    qty_t    total_qty;
    int32_t  order_count;
    market_t market;
} cons_level_view_t;

typedef struct {
    const order_book_t   *book[MARKET_COUNT];
    const market_rules_t *rules[MARKET_COUNT];
} cons_book_t;

/* 전부 0으로 되돌린다. 붙이지 않은 시장은 없는 것으로 친다. */
void cons_init(cons_book_t *cons);

/*
 * 시장 하나를 붙인다. rules가 NULL이면 그 시장은 항상 열린 것으로 본다 —
 * 세션과 무관하게 라우팅만 시험하는 테스트를 위한 것이다.
 * 잘못된 시장이거나 book이 NULL이면 ERR_INVALID_ARG.
 */
int cons_attach(cons_book_t *cons, market_t market, const order_book_t *book,
                const market_rules_t *rules);

/* 이 시각에 그 시장이 거래 가능한가. 붙지 않은 시장은 false. */
bool cons_is_open(const cons_book_t *cons, market_t market, ts_t ts);

/*
 * 통합 최우선호가. 없으면 BOOK_PRICE_NONE이고 *out_market은 건드리지 않는다.
 *
 * 동률 규칙 — 가격이 같으면 **잔량이 많은 시장**, 잔량도 같으면 **시장 열거 순서**
 * (MARKET_KRX 우선). 잔량이 많은 쪽이 체결 가능성이 높다는 최선집행 기준과 맞고,
 * 마지막 동률까지 결정적이라 같은 입력에 항상 같은 시장이 나온다.
 *
 * out_market은 NULL을 줘도 된다.
 */
price_t cons_best_bid(const cons_book_t *cons, ts_t ts, market_t *out_market);
price_t cons_best_ask(const cons_book_t *cons, ts_t ts, market_t *out_market);

/* 그 가격의 양 시장 합산 잔량. 닫힌 시장은 빼고 센다. */
qty_t cons_qty_at(const cons_book_t *cons, side_t side, price_t price, ts_t ts);

/*
 * 통합 N단 호가. 매수는 비싼 가격부터, 매도는 싼 가격부터.
 * **같은 가격이라도 시장이 다르면 줄이 따로 나온다.** 합쳐 버리면 "그 물량이 어느
 * 시장에 있는가"를 잃고, 그것이 곧 라우팅 판단의 재료다.
 * 같은 가격 안에서는 위의 동률 규칙과 같은 순서로 놓는다.
 *
 * 채운 줄 수를 반환한다. 인자가 잘못되면 음수 에러 코드.
 */
int cons_snapshot(const cons_book_t *cons, side_t side, int depth, ts_t ts,
                  cons_level_view_t *out);

#endif /* MINI_SOR_CONSOLIDATED_H */
