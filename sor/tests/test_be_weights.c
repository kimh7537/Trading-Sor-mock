/*
 * T2-04 평가 가중치 설정 분리.
 *
 * 설정을 파일로 뺐다는 것만으로는 의미가 없다. 확인할 것은 **설정을 바꾸면 선택한
 * 시장이 실제로 바뀌는가**다. 읽기만 되고 평가에 안 쓰이면 파일이 장식이 된다.
 *
 * 그래서 한 호가창을 두고 설정만 갈아 끼우며 선택이 뒤집히는지 본다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "best_execution.h"
#include "errors.h"
#include "match.h"

#define BASE 10000
#define CAP 256
#define T_BOTH TOD_NS(12, 0, 0)
#define CFG_PATH "be_test.cfg"

static order_id_t NEXT_ID = 1;

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
} fixture_t;

static void fx_init(fixture_t *fx)
{
    cons_init(&fx->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        fx->eng[m] = match_engine_create(BASE, CAP);
        assert(fx->eng[m] != NULL);
        assert(cons_attach(&fx->cons, (market_t)m, match_book(fx->eng[m]),
                           NULL) == ERR_OK);
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

static void write_cfg(const char *body)
{
    FILE *f = fopen(CFG_PATH, "w");
    assert(f != NULL);
    fputs(body, f);
    fclose(f);
}

/* 정상적으로 읽힌다. 적지 않은 키는 기본값이 남는다 */
static void test_load(void)
{
    be_weights_t w;
    be_config_t  c;

    write_cfg("# 최선집행 평가 설정\n"
              "weight_price = 50\n"
              "weight_fill  = 25\n"
              "weight_cost  = 15\n"
              "weight_state = 10   # 보조 지표\n"
              "\n"
              "fee_krx_bp = 4\n"
              "fee_nxt_bp = 1\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_OK);
    assert(w.price == 50 && w.fill == 25 && w.cost == 15 && w.state == 10);
    assert(c.fee_bp[MARKET_KRX] == 4 && c.fee_bp[MARKET_NXT] == 1);

    /* 일부만 적으면 나머지는 기본값 */
    write_cfg("weight_price = 70\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_OK);
    assert(w.price == 70);
    assert(w.fill == BE_WEIGHTS_DEFAULT.fill);
    assert(w.cost == BE_WEIGHTS_DEFAULT.cost);
    assert(w.state == BE_WEIGHTS_DEFAULT.state);
    assert(c.fee_bp[MARKET_KRX] == BE_CONFIG_DEFAULT.fee_bp[MARKET_KRX]);

    /* 한쪽만 받아도 된다 */
    assert(be_load_config(CFG_PATH, &w, NULL) == ERR_OK);
    assert(be_load_config(CFG_PATH, NULL, &c) == ERR_OK);
    assert(be_load_config(CFG_PATH, NULL, NULL) == ERR_OK);

    /* 주석과 빈 줄만 있어도 유효하다 — 전부 기본값이 된다 */
    write_cfg("# 아무것도 없음\n\n   \n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_OK);
    assert(w.price == BE_WEIGHTS_DEFAULT.price);

    remove(CFG_PATH);
}

/* 잘못된 설정은 거절한다 */
static void test_rejects(void)
{
    be_weights_t w;
    be_config_t  c;

    /* 모르는 키 — 오타를 조용히 넘기지 않는다 */
    write_cfg("weight_pirce = 40\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_INVALID_ARG);

    /* 숫자가 아닌 값 */
    write_cfg("weight_price = 마흔\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_INVALID_ARG);
    write_cfg("weight_price = 40점\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_INVALID_ARG);

    /* 음수 가중치 */
    write_cfg("weight_price = -1\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_INVALID_ARG);

    /* 가중치 합이 0 — 순위를 매길 수 없다 */
    write_cfg("weight_price = 0\nweight_fill = 0\n"
              "weight_cost = 0\nweight_state = 0\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_INVALID_ARG);

    /* 음수 수수료 */
    write_cfg("fee_krx_bp = -2\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_INVALID_ARG);

    /* '=' 없는 줄, 값이 빈 줄 */
    write_cfg("weight_price 40\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_INVALID_ARG);
    write_cfg("weight_price =\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_INVALID_ARG);

    assert(be_load_config("없는파일.cfg", &w, &c) == ERR_NOT_FOUND);
    assert(be_load_config(NULL, &w, &c) == ERR_NULL_PTR);

    remove(CFG_PATH);
}

/*
 * 설정을 바꾸면 선택이 실제로 바뀐다 — 이 태스크의 본론.
 *
 * KRX는 가격이 좋고 물량이 적다. NXT는 가격이 나쁘고 물량이 많다.
 * 가격만 보면 KRX, 체결 가능성만 보면 NXT가 이겨야 한다.
 */
static void test_config_changes_choice(void)
{
    fixture_t fx;
    fx_init(&fx);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 50);
    put(&fx, MARKET_NXT, SIDE_SELL, 10100, 1000);
    put(&fx, MARKET_KRX, SIDE_BUY, 9900, 1000);
    put(&fx, MARKET_NXT, SIDE_BUY, 9900, 1000);

    order_t req = {0};
    req.id = NEXT_ID++;
    req.ts = T_BOTH;
    req.side = SIDE_BUY;
    req.price = 10100;
    req.qty = 500;
    req.type = ORDER_LIMIT;

    be_weights_t  w;
    be_config_t   c;
    venue_score_t s[MARKET_COUNT];
    market_t      pick;

    write_cfg("weight_price = 100\nweight_fill = 0\n"
              "weight_cost = 0\nweight_state = 0\n"
              "fee_krx_bp = 2\nfee_nxt_bp = 2\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_OK);
    assert(be_evaluate(&fx.cons, &req, T_BOTH, &w, &c, s) == ERR_OK);
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_KRX);

    write_cfg("weight_price = 0\nweight_fill = 100\n"
              "weight_cost = 0\nweight_state = 0\n"
              "fee_krx_bp = 2\nfee_nxt_bp = 2\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_OK);
    assert(be_evaluate(&fx.cons, &req, T_BOTH, &w, &c, s) == ERR_OK);
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT); /* 설정만 바꿨는데 선택이 뒤집혔다 */

    /* 수수료만으로도 뒤집을 수 있다 — 가격·물량을 같게 둔 상태에서 */
    fixture_t fx2;
    fx_init(&fx2);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        put(&fx2, (market_t)m, SIDE_SELL, 10000, 1000);
        put(&fx2, (market_t)m, SIDE_BUY, 9900, 1000);
    }
    order_t req2 = req;
    req2.price = 10000;

    write_cfg("weight_price = 0\nweight_fill = 0\n"
              "weight_cost = 100\nweight_state = 0\n"
              "fee_krx_bp = 1\nfee_nxt_bp = 9\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_OK);
    assert(be_evaluate(&fx2.cons, &req2, T_BOTH, &w, &c, s) == ERR_OK);
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_KRX);

    write_cfg("weight_price = 0\nweight_fill = 0\n"
              "weight_cost = 100\nweight_state = 0\n"
              "fee_krx_bp = 9\nfee_nxt_bp = 1\n");
    assert(be_load_config(CFG_PATH, &w, &c) == ERR_OK);
    assert(be_evaluate(&fx2.cons, &req2, T_BOTH, &w, &c, s) == ERR_OK);
    assert(be_pick(s, &pick) == ERR_OK);
    assert(pick == MARKET_NXT);

    fx_free(&fx2);
    fx_free(&fx);
    remove(CFG_PATH);
}

int main(void)
{
    test_load();
    test_rejects();
    test_config_changes_choice();
    return 0;
}
