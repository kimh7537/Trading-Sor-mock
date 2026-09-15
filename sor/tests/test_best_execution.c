/*
 * T2-03 최선집행기준 평가.
 *
 * 항목이 넷이므로, 각 항목이 **혼자서** 승부를 가르는 장면을 하나씩 만든다.
 * 나머지 셋을 같게 두고 한 항목만 다르게 하면, 그 항목이 실제로 점수에 반영되는지가
 * 드러난다. 넷을 한꺼번에 흔들어 놓고 총점만 보면 어느 항목이 일했는지 알 수 없다.
 *
 * 제외된 시장은 사라지지 않고 이유와 함께 남아야 한다 — "왜 안 썼는가"도 근거다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

#include "best_execution.h"
#include "errors.h"
#include "match.h"

#define BASE 10000
#define CAP 512
#define T_BOTH TOD_NS(12, 0, 0)
#define T_KRX_ONLY TOD_NS(15, 25, 0) /* NXT 오후 휴장 */

static order_id_t NEXT_ID = 1;

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
} fixture_t;

static void fx_init(fixture_t *fx, bool with_rules)
{
    const market_rules_t *rules[MARKET_COUNT] = {
        [MARKET_KRX] = &KRX_RULES,
        [MARKET_NXT] = &NXT_RULES,
    };
    cons_init(&fx->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        fx->eng[m] = match_engine_create(BASE, CAP);
        assert(fx->eng[m] != NULL);
        assert(cons_attach(&fx->cons, (market_t)m, match_book(fx->eng[m]),
                           with_rules ? rules[m] : NULL) == ERR_OK);
    }
}

static void fx_free(fixture_t *fx)
{
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(fx->eng[m]);
    }
}

static void put(fixture_t *fx, market_t m, side_t side, price_t price, qty_t qty)
{
    exec_result_t res;
    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = T_BOTH;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = ORDER_LIMIT;
    req.market = m;
    assert(match_limit(fx->eng[m], &req, &res) == ERR_OK);
}

static order_t buy_req(price_t limit, qty_t qty)
{
    order_t r = {0};
    r.id = NEXT_ID++;
    r.ts = T_BOTH;
    r.side = SIDE_BUY;
    r.price = limit;
    r.qty = qty;
    r.type = ORDER_LIMIT;
    return r;
}

/* 양쪽에 같은 모양의 호가를 깐다 — 한 항목만 흔들기 위한 바탕 */
static void symmetric(fixture_t *fx, price_t ask, price_t bid, qty_t qty)
{
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        put(fx, (market_t)m, SIDE_SELL, ask, qty);
        put(fx, (market_t)m, SIDE_BUY, bid, qty);
    }
}

/* 가격만 다르면 가격이 이긴다 */
static void test_price_decides(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_SELL, 10100, 1000);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 1000); /* NXT가 싸다 */
    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 1000);
    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 1000);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10100, 500);
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, NULL, s) == ERR_OK);

    assert(s[MARKET_KRX].eligible && s[MARKET_NXT].eligible);
    assert(s[MARKET_NXT].quote == 10000 && s[MARKET_KRX].quote == 10100);
    assert(s[MARKET_NXT].price_score == BE_SCORE_MAX); /* 최선 호가 */
    assert(s[MARKET_KRX].price_score < s[MARKET_NXT].price_score);
    /* 체결 가능성은 양쪽 다 충분하다 */
    assert(s[MARKET_KRX].fill_score == BE_SCORE_MAX);
    assert(s[MARKET_NXT].fill_score == BE_SCORE_MAX);

    market_t pick;
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT);

    fx_free(&fx);
}

/* 가격이 같으면 물량이 많은 쪽이 이긴다 (체결 가능성) */
static void test_fill_decides(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 100); /* 물량 부족 */
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 500);
    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 100);
    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 500);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10000, 500);
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, NULL, s) == ERR_OK);

    /* 가격은 무승부 */
    assert(s[MARKET_KRX].price_score == s[MARKET_NXT].price_score);
    /* 체결 가능성이 갈린다: 100/500 vs 500/500 */
    assert(s[MARKET_KRX].fillable == 100 && s[MARKET_NXT].fillable == 500);
    assert(s[MARKET_KRX].fill_score == BE_SCORE_MAX / 5);
    assert(s[MARKET_NXT].fill_score == BE_SCORE_MAX);

    market_t pick;
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT);

    fx_free(&fx);
}

/* 가격도 물량도 같으면 수수료가 이긴다 */
static void test_cost_decides(void)
{
    fixture_t fx;
    fx_init(&fx, false);
    symmetric(&fx, 10000, 9900, 1000);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10000, 500);

    /* 기본 설정은 NXT가 1bp 싸다 */
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, NULL, s) == ERR_OK);
    assert(s[MARKET_KRX].price_score == s[MARKET_NXT].price_score);
    assert(s[MARKET_KRX].fill_score == s[MARKET_NXT].fill_score);
    assert(s[MARKET_KRX].state_score == s[MARKET_NXT].state_score);
    assert(s[MARKET_NXT].cost_score > s[MARKET_KRX].cost_score);
    assert(s[MARKET_NXT].total > s[MARKET_KRX].total);

    market_t pick;
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT);

    /* 수수료를 뒤집으면 선택도 뒤집힌다 */
    be_config_t flipped = {.fee_bp = {[MARKET_KRX] = 1, [MARKET_NXT] = 5}};
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, &flipped, s) == ERR_OK);
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_KRX);

    fx_free(&fx);
}

/* 가격·물량·수수료가 같으면 스프레드(시장 상태)가 이긴다 */
static void test_state_decides(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    /* 매도호가는 같고 매수호가만 다르게 해서 스프레드를 벌린다 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 1000);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 1000);
    put(&fx, MARKET_KRX, SIDE_BUY, 9000, 1000); /* 스프레드 1,000 */
    put(&fx, MARKET_NXT, SIDE_BUY, 9990, 1000); /* 스프레드 10 */

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10000, 500);
    /* 수수료를 같게 둬서 상태만 남긴다 */
    be_config_t same_fee = {.fee_bp = {[MARKET_KRX] = 2, [MARKET_NXT] = 2}};
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, &same_fee, s) == ERR_OK);

    assert(s[MARKET_KRX].price_score == s[MARKET_NXT].price_score);
    assert(s[MARKET_KRX].fill_score == s[MARKET_NXT].fill_score);
    assert(s[MARKET_KRX].cost_score == s[MARKET_NXT].cost_score);
    assert(s[MARKET_KRX].spread == 1000 && s[MARKET_NXT].spread == 10);
    assert(s[MARKET_NXT].state_score > s[MARKET_KRX].state_score);
    assert(s[MARKET_NXT].total > s[MARKET_KRX].total);

    market_t pick;
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT);

    fx_free(&fx);
}

/* 모든 항목이 같으면 완전 동점 — 시장 열거 순서로 끊는다 */
static void test_exact_tie(void)
{
    fixture_t fx;
    fx_init(&fx, false);
    symmetric(&fx, 10000, 9900, 1000);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10000, 500);
    be_config_t same_fee = {.fee_bp = {[MARKET_KRX] = 2, [MARKET_NXT] = 2}};
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, &same_fee, s) == ERR_OK);

    assert(s[MARKET_KRX].total == s[MARKET_NXT].total);
    assert(s[MARKET_KRX].fillable == s[MARKET_NXT].fillable);

    market_t pick;
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_KRX); /* 열거 순서 */

    fx_free(&fx);
}

/* 총점이 같고 물량이 다르면 물량이 많은 쪽 */
static void test_tie_broken_by_fillable(void)
{
    venue_score_t s[MARKET_COUNT] = {0};
    s[MARKET_KRX].eligible = true;
    s[MARKET_KRX].total = 8000;
    s[MARKET_KRX].fillable = 100;
    s[MARKET_NXT].eligible = true;
    s[MARKET_NXT].total = 8000;
    s[MARKET_NXT].fillable = 300;

    market_t pick;
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT);
}

/* 제외된 시장은 사라지지 않고 이유와 함께 남는다 */
static void test_ineligible_reasons(void)
{
    fixture_t fx;
    fx_init(&fx, true); /* 규칙을 붙인다 */

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 500);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 500);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10000, 100);

    /* NXT 오후 휴장 */
    assert(be_evaluate(&fx.cons, &req, T_KRX_ONLY, NULL, NULL, s) == ERR_OK);
    assert(s[MARKET_KRX].eligible);
    assert(!s[MARKET_NXT].eligible);
    assert(s[MARKET_NXT].reason == ERR_MARKET_CLOSED);
    assert(s[MARKET_NXT].total == 0);

    market_t pick;
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_KRX);

    fx_free(&fx);

    /* 상대 호가가 아예 없는 시장 */
    fixture_t fx2;
    fx_init(&fx2, false);
    put(&fx2, MARKET_KRX, SIDE_SELL, 10000, 500);
    assert(be_evaluate(&fx2.cons, &req, T_BOTH, NULL, NULL, s) == ERR_OK);
    assert(s[MARKET_KRX].eligible);
    assert(!s[MARKET_NXT].eligible);
    assert(s[MARKET_NXT].reason == ERR_NO_LIQUIDITY);
    assert(s[MARKET_NXT].quote == BOOK_PRICE_NONE);

    /* 호가는 있지만 전부 지정가 밖 */
    put(&fx2, MARKET_NXT, SIDE_SELL, 10500, 500);
    assert(be_evaluate(&fx2.cons, &req, T_BOTH, NULL, NULL, s) == ERR_OK);
    assert(!s[MARKET_NXT].eligible);
    assert(s[MARKET_NXT].reason == ERR_NO_LIQUIDITY);
    assert(s[MARKET_NXT].quote == 10500); /* 호가 자체는 남긴다 */
    assert(s[MARKET_NXT].fillable == 0);

    fx_free(&fx2);
}

/* 후보가 하나도 없으면 고를 수 없다 */
static void test_no_candidate(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10000, 100);
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, NULL, s) == ERR_OK);
    assert(!s[MARKET_KRX].eligible && !s[MARKET_NXT].eligible);

    market_t pick;
    assert(be_pick(s, &pick) == ERR_NO_LIQUIDITY);

    fx_free(&fx);
}

/* 매도 주문은 매수호가를 본다 — 방향이 뒤집힌다 */
static void test_sell_side(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 1000);
    put(&fx, MARKET_NXT, SIDE_BUY, 9950, 1000); /* 매도에게는 NXT가 유리 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10100, 1000);
    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 1000);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(9900, 500);
    req.side = SIDE_SELL;
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, NULL, s) == ERR_OK);

    assert(s[MARKET_NXT].quote == 9950 && s[MARKET_KRX].quote == 9900);
    assert(s[MARKET_NXT].price_score == BE_SCORE_MAX);
    assert(s[MARKET_KRX].price_score < s[MARKET_NXT].price_score);

    market_t pick;
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT);

    fx_free(&fx);
}

/* 가중치를 바꾸면 선택이 바뀐다 — 가중치가 실제로 일한다는 확인 */
static void test_weights_matter(void)
{
    fixture_t fx;
    fx_init(&fx, false);

    /* KRX: 가격이 좋고 물량이 적다. NXT: 가격이 나쁘고 물량이 많다 */
    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 50);
    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 1000);
    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 1000);
    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 1000);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10100, 500);
    be_config_t same_fee = {.fee_bp = {[MARKET_KRX] = 2, [MARKET_NXT] = 2}};
    market_t pick;

    be_weights_t price_only = {.price = 100, .fill = 0, .cost = 0, .state = 0};
    assert(be_evaluate(&fx.cons, &req, T_BOTH, &price_only, &same_fee, s) ==
           ERR_OK);
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_KRX); /* 가격만 보면 KRX */

    be_weights_t fill_only = {.price = 0, .fill = 100, .cost = 0, .state = 0};
    assert(be_evaluate(&fx.cons, &req, T_BOTH, &fill_only, &same_fee, s) ==
           ERR_OK);
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT); /* 체결 가능성만 보면 NXT */

    fx_free(&fx);
}

/* 인자 검증 */
static void test_args(void)
{
    fixture_t fx;
    fx_init(&fx, false);
    symmetric(&fx, 10000, 9900, 100);

    venue_score_t s[MARKET_COUNT];
    order_t req = buy_req(10000, 100);

    assert(be_evaluate(NULL, &req, T_BOTH, NULL, NULL, s) == ERR_NULL_PTR);
    assert(be_evaluate(&fx.cons, NULL, T_BOTH, NULL, NULL, s) == ERR_NULL_PTR);
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, NULL, NULL) == ERR_NULL_PTR);

    order_t bad = req;
    bad.qty = 0;
    assert(be_evaluate(&fx.cons, &bad, T_BOTH, NULL, NULL, s) == ERR_INVALID_QTY);
    bad = req;
    bad.side = (side_t)9;
    assert(be_evaluate(&fx.cons, &bad, T_BOTH, NULL, NULL, s) == ERR_INVALID_ARG);

    be_weights_t zero = {0};
    assert(be_evaluate(&fx.cons, &req, T_BOTH, &zero, NULL, s) ==
           ERR_INVALID_ARG);
    be_weights_t negative = {.price = -1, .fill = 10, .cost = 0, .state = 0};
    assert(be_evaluate(&fx.cons, &req, T_BOTH, &negative, NULL, s) ==
           ERR_INVALID_ARG);

    be_config_t bad_fee = {.fee_bp = {[MARKET_KRX] = -1, [MARKET_NXT] = 0}};
    assert(be_evaluate(&fx.cons, &req, T_BOTH, NULL, &bad_fee, s) ==
           ERR_INVALID_ARG);

    market_t pick;
    assert(be_pick(NULL, &pick) == ERR_NULL_PTR);
    assert(be_pick(s, NULL) == ERR_NULL_PTR);

    fx_free(&fx);
}

int main(void)
{
    test_args();
    test_price_decides();
    test_fill_decides();
    test_cost_decides();
    test_state_decides();
    test_exact_tie();
    test_tie_broken_by_fillable();
    test_ineligible_reasons();
    test_no_candidate();
    test_sell_side();
    test_weights_matter();
    return 0;
}
