/*
 * T1-13 시장 규칙 추상화.
 *
 * 여기서 검증하는 것은 규칙의 내용이 아니라 **엔진이 규칙을 실제로 거쳐 가는가**다.
 * 더미 규칙이 호출 횟수를 세고, 답을 마음대로 바꿔 가며 엔진의 행동이 따라오는지 본다.
 * 규칙 내용은 T1-14(KRX), T1-15(NXT)에서 따로 검증한다.
 */
#include <assert.h>
#include <stddef.h>

#include "errors.h"
#include "market_rules.h"
#include "match.h"

#define BASE 10000
#define CAP 64

/* 더미 규칙이 참조하는 상태. 규칙 함수 자체는 인자만 보므로 여기에 몰아 둔다. */
static struct {
    bool      open;
    session_t session;
    bool      allow_type;
    bool      allow_submit;
    bool      allow_cancel;
    price_t   forced_price; /* 0이면 주문 가격 그대로 */

    int n_is_open;
    int n_allowed;
    int n_submit;
    int n_cancel;
    int n_resolve;
} G;

static void reset_rules(void)
{
    G.open = true;
    G.session = SESSION_REGULAR;
    G.allow_type = true;
    G.allow_submit = true;
    G.allow_cancel = true;
    G.forced_price = 0;
    G.n_is_open = 0;
    G.n_allowed = 0;
    G.n_submit = 0;
    G.n_cancel = 0;
    G.n_resolve = 0;
}

static bool dummy_is_open(ts_t ts, session_t *out)
{
    (void)ts;
    G.n_is_open++;
    *out = G.session;
    return G.open;
}

static bool dummy_type_allowed(session_t session, order_type_t type)
{
    (void)type;
    G.n_allowed++;
    assert(session == G.session); /* 엔진이 is_open의 결과를 그대로 넘겨야 한다 */
    return G.allow_type;
}

static bool dummy_can_submit(session_t session)
{
    G.n_submit++;
    assert(session == G.session);
    return G.allow_submit;
}

static bool dummy_can_cancel(session_t session)
{
    G.n_cancel++;
    assert(session == G.session);
    return G.allow_cancel;
}

static bool dummy_allows_reprice(order_type_t type)
{
    return type != ORDER_MIDPOINT;
}

static price_t dummy_resolve(const order_book_t *book, const order_t *req)
{
    (void)book;
    G.n_resolve++;
    return (G.forced_price != 0) ? G.forced_price : req->price;
}

static const market_rules_t DUMMY = {
    .name = "더미",
    .is_open = dummy_is_open,
    .is_order_type_allowed = dummy_type_allowed,
    .can_submit = dummy_can_submit,
    .can_cancel = dummy_can_cancel,
    .resolve_price = dummy_resolve,
    .allows_reprice = dummy_allows_reprice,
};

static order_id_t NEXT_ID = 1;
static ts_t NEXT_TS = 1000;

static order_t req_of(side_t side, price_t price, qty_t qty, order_type_t type)
{
    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = NEXT_TS++;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = type;
    req.market = MARKET_KRX;
    return req;
}

/* 규칙을 걸지 않으면 예전과 똑같이 동작한다 */
static void test_no_rules(void)
{
    reset_rules();
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    exec_result_t res;
    order_t req = req_of(SIDE_BUY, 9900, 10, ORDER_LIMIT);
    assert(match_limit(eng, &req, &res) == ERR_OK);
    assert(res.resting);
    assert(G.n_is_open == 0); /* 규칙을 안 걸었으니 부르지 않는다 */

    match_engine_destroy(eng);
}

/* 열려 있으면 네 갈래를 모두 거쳐 통과한다 */
static void test_open_passes(void)
{
    reset_rules();
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_rules(eng, &DUMMY);

    exec_result_t res;
    order_t req = req_of(SIDE_BUY, 9900, 10, ORDER_LIMIT);
    assert(match_limit(eng, &req, &res) == ERR_OK);

    assert(G.n_is_open == 1);
    assert(G.n_submit == 1);
    assert(G.n_allowed == 1);
    assert(G.n_resolve == 1); /* 가격도 규칙이 정한다 */
    assert(res.resting);

    match_engine_destroy(eng);
}

/* 닫혀 있으면 ERR_MARKET_CLOSED. 유형 검사까지 가지 않는다 */
static void test_closed_rejects(void)
{
    reset_rules();
    G.open = false;
    G.session = SESSION_CLOSED;

    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_rules(eng, &DUMMY);

    exec_result_t res;
    order_t limit = req_of(SIDE_BUY, 9900, 10, ORDER_LIMIT);
    assert(match_limit(eng, &limit, &res) == ERR_MARKET_CLOSED);
    assert(res.status == STATUS_REJECTED);
    assert(G.n_allowed == 0); /* 단락됐다 */
    assert(G.n_resolve == 0); /* 가격 해석까지 가지도 않았다 */

    /* 네 진입점 모두 같은 관문을 지난다 */
    order_t mkt = req_of(SIDE_BUY, 0, 10, ORDER_MARKET);
    order_t ioc = req_of(SIDE_BUY, 9900, 10, ORDER_IOC);
    order_t fok = req_of(SIDE_BUY, 9900, 10, ORDER_FOK);
    assert(match_market(eng, &mkt, &res) == ERR_MARKET_CLOSED);
    assert(match_ioc(eng, &ioc, &res) == ERR_MARKET_CLOSED);
    assert(match_fok(eng, &fok, &res) == ERR_MARKET_CLOSED);

    const order_book_t *book = match_book(eng);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

/* 휴장 구간 — 신규는 안 되고 취소는 된다 */
static void test_break_allows_cancel_only(void)
{
    reset_rules();
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);

    /* 규칙 걸기 전에 주문을 하나 심어 둔다 */
    exec_result_t res;
    order_t seed = req_of(SIDE_BUY, 9900, 10, ORDER_LIMIT);
    assert(match_limit(eng, &seed, &res) == ERR_OK);

    match_set_rules(eng, &DUMMY);
    G.session = SESSION_PRE_BREAK;
    G.open = false;        /* 휴장은 열린 상태가 아니다 */
    G.allow_submit = false;
    G.allow_cancel = true; /* 그래도 취소는 받는다 */

    order_t neu = req_of(SIDE_BUY, 9890, 10, ORDER_LIMIT);
    assert(match_limit(eng, &neu, &res) == ERR_MARKET_CLOSED);
    assert(match_modify(eng, seed.id, 9890, 10, NEXT_TS++, &res) ==
           ERR_MARKET_CLOSED);

    /* 취소만 통과 */
    assert(match_cancel(eng, seed.id, NEXT_TS++, &res) == ERR_OK);
    assert(G.n_cancel >= 1);
    assert(res.status == STATUS_CANCELED);

    match_engine_destroy(eng);
}

/* 이 구간이 안 받는 유형이면 ERR_NOT_SUPPORTED */
static void test_type_not_allowed(void)
{
    reset_rules();
    G.allow_type = false;

    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_rules(eng, &DUMMY);

    exec_result_t res;
    order_t req = req_of(SIDE_BUY, 9900, 10, ORDER_MIDPOINT);
    assert(match_limit(eng, &req, &res) == ERR_NOT_SUPPORTED);
    assert(res.status == STATUS_REJECTED);
    assert(G.n_submit == 1); /* 여기까지는 왔다 */
    assert(G.n_resolve == 0);

    match_engine_destroy(eng);
}

/* 규칙이 정한 가격이 실제로 쓰인다 — 엔진이 주문의 price를 그냥 믿지 않는다 */
static void test_resolved_price_is_used(void)
{
    reset_rules();
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_rules(eng, &DUMMY);

    exec_result_t res;
    G.forced_price = 9950; /* 주문은 9,900인데 규칙이 9,950으로 바꾼다 */
    order_t req = req_of(SIDE_BUY, 9900, 10, ORDER_LIMIT);
    assert(match_limit(eng, &req, &res) == ERR_OK);

    const order_book_t *book = match_book(eng);
    assert(book_best_bid(book) == 9950);
    assert(book_qty_at(book, SIDE_BUY, 9950) == 10);
    assert(book_qty_at(book, SIDE_BUY, 9900) == 0);

    match_engine_destroy(eng);
}

/* 가격을 정할 수 없다고 하면(BOOK_PRICE_NONE) 거부한다 */
static void test_unresolvable_price(void)
{
    reset_rules();
    match_engine_t *eng = match_engine_create(BASE, CAP);
    assert(eng != NULL);
    match_set_rules(eng, &DUMMY);

    exec_result_t res;
    /* 주문 가격을 BOOK_PRICE_NONE으로 두면 더미가 그대로 돌려준다 */
    order_t req = req_of(SIDE_BUY, BOOK_PRICE_NONE, 10, ORDER_MIDPOINT);
    assert(match_limit(eng, &req, &res) == ERR_INVALID_PRICE);
    assert(res.status == STATUS_REJECTED);
    assert(G.n_resolve == 1);

    const order_book_t *book = match_book(eng);
    assert(book_best_bid(book) == BOOK_PRICE_NONE);

    match_engine_destroy(eng);
}

/* 세션 이름표 */
static void test_session_str(void)
{
    const session_t all[] = {SESSION_CLOSED,     SESSION_PRE,
                             SESSION_PRE_BREAK,  SESSION_REGULAR,
                             SESSION_POST_BREAK, SESSION_AFTER};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        assert(session_str(all[i]) != NULL);
    }
    assert(session_str((session_t)99) != NULL); /* 미정의 값도 NULL 아님 */
}

int main(void)
{
    test_no_rules();
    test_open_passes();
    test_closed_rejects();
    test_break_allows_cancel_only();
    test_type_not_allowed();
    test_resolved_price_is_used();
    test_unresolvable_price();
    test_session_str();
    return 0;
}
