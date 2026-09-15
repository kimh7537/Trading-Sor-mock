/*
 * T1-18 유동성 공급 — Divergent.
 *
 * 프리셋이 "의도대로 다른 장면을 만드는가"를 본다. 그런데 무엇이 "얇다"인가?
 * 도착률만 낮으면 주문 수만 적을 뿐 체결 단가에는 불리하지 않을 수 있다.
 * 그래서 두 가지를 함께 잰다.
 *   깊이(depth) — 최우선호가 근처 10단의 잔량 합계
 *   스프레드    — 최우선매도 - 최우선매수
 * 얇은 시장은 깊이가 얕고 스프레드가 넓어야 한다.
 */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "divergent.h"
#include "errors.h"
#include "match.h"

#define REF 10000
#define CAP 8192
#define DEPTH 10
#define CFG_PATH "divergent_test.cfg"

typedef struct {
    price_t      bid;
    price_t      ask;
    qty_t        depth_bid;
    qty_t        depth_ask;
    level_view_t ask_levels[DEPTH];
    int          n_ask;
} book_shape_t;

static divergent_config_t base_cfg(scenario_t s, uint64_t seed)
{
    divergent_config_t cfg = {0};
    cfg.scenario = s;
    cfg.seed = seed;
    cfg.ref_price = REF;
    cfg.price_low = 7000;
    cfg.price_high = 13000;
    cfg.start_ts = 0;
    cfg.orders_per_market = 3000;
    return cfg;
}

/* 시나리오를 돌려 두 시장의 호가창 모양을 뽑는다 */
static void run(const divergent_config_t *cfg, book_shape_t out[MARKET_COUNT])
{
    divergent_t *div = divergent_create(cfg);
    assert(div != NULL);

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_t *eng =
            match_engine_create(divergent_ref_price(div, (market_t)m), CAP);
        assert(eng != NULL);
        const order_book_t *book = match_book(eng);

        synth_gen_t *gen = divergent_gen(div, (market_t)m);
        assert(gen != NULL);

        for (int32_t i = 0; i < cfg->orders_per_market; i++) {
            order_t o;
            exec_result_t res;
            assert(synth_next(gen, &o) == ERR_OK);
            int rc = match_limit(eng, &o, &res);
            assert(rc == ERR_OK || rc == ERR_POOL_EXHAUSTED);
        }

        level_view_t view[DEPTH];
        book_shape_t shape = {0};
        shape.bid = book_best_bid(book);
        shape.ask = book_best_ask(book);

        int n = book_snapshot(book, SIDE_BUY, DEPTH, view);
        for (int i = 0; i < n; i++) {
            shape.depth_bid += view[i].total_qty;
        }
        shape.n_ask = book_snapshot(book, SIDE_SELL, DEPTH, shape.ask_levels);
        for (int i = 0; i < shape.n_ask; i++) {
            shape.depth_ask += shape.ask_levels[i].total_qty;
        }

        out[m] = shape;
        match_engine_destroy(eng);
    }

    divergent_destroy(div);
}

static price_t spread_of(const book_shape_t *s)
{
    assert(s->bid != BOOK_PRICE_NONE && s->ask != BOOK_PRICE_NONE);
    return s->ask - s->bid;
}

static qty_t depth_of(const book_shape_t *s)
{
    return s->depth_bid + s->depth_ask;
}

/*
 * want주를 매수로 쓸어 담을 때의 슬리피지 — 평균 체결 단가에서 최우선매도호가를 뺀 값.
 *
 * 최우선호가 스프레드를 쓰지 않는 이유는 주문이 많이 쌓이면 양쪽 다 기준가 근처가
 * 촘촘해져 스프레드가 1틱으로 포화되기 때문이다. 그러면 "얇다"가 안 보인다.
 * 슬리피지는 호가를 실제로 몇 단이나 파고들어야 하는지를 재므로 포화되지 않고,
 * 이 프로젝트가 최종적으로 재려는 값(평균 체결 단가)과 같은 종류다.
 *
 * 깊이가 모자라 want를 다 못 채우면 있는 만큼으로 계산한다.
 */
static int64_t slippage_of(const book_shape_t *s, qty_t want)
{
    int64_t notional = 0;
    qty_t filled = 0;

    for (int i = 0; i < s->n_ask && filled < want; i++) {
        qty_t take = s->ask_levels[i].total_qty;
        if (take > want - filled) {
            take = want - filled;
        }
        notional += (int64_t)s->ask_levels[i].price * (int64_t)take;
        filled += take;
    }
    assert(filled > 0);
    return notional / filled - (int64_t)s->ask;
}

/* 중간 가격. 두 시장의 가격대가 얼마나 어긋나 있는지 재는 데 쓴다 */
static price_t mid_of(const book_shape_t *s)
{
    assert(s->bid != BOOK_PRICE_NONE && s->ask != BOOK_PRICE_NONE);
    return (s->bid + s->ask) / 2;
}

static price_t mid_gap(const book_shape_t s[MARKET_COUNT])
{
    price_t a = mid_of(&s[MARKET_KRX]);
    price_t b = mid_of(&s[MARKET_NXT]);
    return (a > b) ? (a - b) : (b - a);
}

/* 양 시장이 비슷하다 — 라우팅이 무의미한 대조군 */
static void test_balanced(void)
{
    divergent_config_t cfg = base_cfg(SCENARIO_BALANCED, 2026);
    book_shape_t s[MARKET_COUNT];
    run(&cfg, s);

    assert(s[MARKET_KRX].bid != BOOK_PRICE_NONE);
    assert(s[MARKET_NXT].bid != BOOK_PRICE_NONE);

    /* 깊이가 같은 자릿수다. 시드가 다르므로 정확히 같을 수는 없다 */
    qty_t dk = depth_of(&s[MARKET_KRX]);
    qty_t dn = depth_of(&s[MARKET_NXT]);
    assert(dk > 0 && dn > 0);
    assert(dk < dn * 3 && dn < dk * 3);

    /* 쓸어 담는 비용도 비슷하다 */
    int64_t gk = slippage_of(&s[MARKET_KRX], 1000);
    int64_t gn = slippage_of(&s[MARKET_NXT], 1000);
    assert(gk < gn * 3 + 10 && gn < gk * 3 + 10);
    assert(spread_of(&s[MARKET_KRX]) > 0 && spread_of(&s[MARKET_NXT]) > 0);

    /*
     * 두 시장의 가격대가 거의 겹친다.
     *
     * 여기서 "두 시장이 교차하지 않는다"고 단언하면 안 된다. 같은 기준가 주위의
     * 독립된 두 호가창은 우연히 교차할 수 있고, 그건 버그가 아니라 실제로 SOR이
     * 노리는 상황이다. BALANCED가 주장하는 것은 "어긋남이 작다"이지
     * "어긋남이 없다"가 아니다. CROSSED와 비교해서 말한다.
     */
    divergent_config_t crossed = base_cfg(SCENARIO_CROSSED, 2026);
    book_shape_t c[MARKET_COUNT];
    run(&crossed, c);
    assert(mid_gap(s) < mid_gap(c) / 2);
}

/* KRX가 얇다 — 깊이가 얕고 스프레드가 넓다 */
static void test_krx_thin(void)
{
    divergent_config_t cfg = base_cfg(SCENARIO_KRX_THIN, 2026);
    book_shape_t s[MARKET_COUNT];
    run(&cfg, s);

    assert(depth_of(&s[MARKET_KRX]) < depth_of(&s[MARKET_NXT]));
    /* 얇은 쪽에서 쓸어 담으면 더 비싸다 */
    assert(slippage_of(&s[MARKET_KRX], 1000) > slippage_of(&s[MARKET_NXT], 1000));
}

/* NXT가 얇다 — 정확히 반대 */
static void test_nxt_thin(void)
{
    divergent_config_t cfg = base_cfg(SCENARIO_NXT_THIN, 2026);
    book_shape_t s[MARKET_COUNT];
    run(&cfg, s);

    assert(depth_of(&s[MARKET_NXT]) < depth_of(&s[MARKET_KRX]));
    assert(slippage_of(&s[MARKET_NXT], 1000) > slippage_of(&s[MARKET_KRX], 1000));
}

/*
 * 한쪽 최우선호가가 다른 쪽보다 유리하다.
 * NXT를 위로 밀었으므로 NXT의 매수호가가 KRX의 매도호가보다 높다 —
 * 즉 KRX에서 사서 NXT에 파는 것이 이득인 상태다. SOR이 잡아내야 할 장면이다.
 */
static void test_crossed(void)
{
    divergent_config_t cfg = base_cfg(SCENARIO_CROSSED, 2026);
    book_shape_t s[MARKET_COUNT];
    run(&cfg, s);

    assert(s[MARKET_NXT].bid > s[MARKET_KRX].ask);

    /* 반대 방향은 교차하지 않는다 — 한쪽으로만 유리해야 한다 */
    assert(s[MARKET_KRX].bid < s[MARKET_NXT].ask);

    /* 시장별 기준가가 실제로 다르다 */
    divergent_t *div = divergent_create(&cfg);
    assert(div != NULL);
    assert(divergent_ref_price(div, MARKET_NXT) >
           divergent_ref_price(div, MARKET_KRX));
    divergent_destroy(div);
}

/* 시드 고정 시 재현 가능 */
static void test_reproducible(void)
{
    divergent_config_t cfg = base_cfg(SCENARIO_KRX_THIN, 777);
    book_shape_t a[MARKET_COUNT];
    book_shape_t b[MARKET_COUNT];

    run(&cfg, a);
    run(&cfg, b);
    assert(memcmp(a, b, sizeof(a)) == 0);

    /* 시드가 다르면 달라진다 — 위 비교가 무의미하지 않다는 확인 */
    divergent_config_t other = base_cfg(SCENARIO_KRX_THIN, 778);
    book_shape_t c[MARKET_COUNT];
    run(&other, c);
    assert(memcmp(a, c, sizeof(a)) != 0);
}

/* 두 시장이 같은 주문을 내지 않는다 */
static void test_markets_differ(void)
{
    divergent_config_t cfg = base_cfg(SCENARIO_BALANCED, 555);
    divergent_t *div = divergent_create(&cfg);
    assert(div != NULL);

    int same = 0;
    for (int i = 0; i < 200; i++) {
        order_t k, n;
        assert(synth_next(divergent_gen(div, MARKET_KRX), &k) == ERR_OK);
        assert(synth_next(divergent_gen(div, MARKET_NXT), &n) == ERR_OK);
        assert(k.market == MARKET_KRX && n.market == MARKET_NXT);
        assert(k.id != n.id); /* 주문번호 공간이 갈려 있다 */
        if (k.price == n.price && k.qty == n.qty) {
            same++;
        }
    }
    /* 우연히 겹칠 수는 있어도 전부 같으면 시드 파생이 안 된 것이다 */
    assert(same < 200);

    divergent_destroy(div);
}

/* 생성 인자 검증 */
static void test_create_rejects(void)
{
    assert(divergent_create(NULL) == NULL);

    divergent_config_t c = base_cfg(SCENARIO_BALANCED, 1);
    c.seed = 0;
    assert(divergent_create(&c) == NULL);

    c = base_cfg(SCENARIO_BALANCED, 1);
    c.orders_per_market = 0;
    assert(divergent_create(&c) == NULL);

    c = base_cfg(SCENARIO_BALANCED, 1);
    c.scenario = (scenario_t)99;
    assert(divergent_create(&c) == NULL);

    divergent_destroy(NULL);
    assert(divergent_gen(NULL, MARKET_KRX) == NULL);
    assert(divergent_ref_price(NULL, MARKET_KRX) == 0);
}

/* 시나리오 이름 왕복 */
static void test_scenario_names(void)
{
    const scenario_t all[] = {SCENARIO_BALANCED, SCENARIO_KRX_THIN,
                              SCENARIO_NXT_THIN, SCENARIO_CROSSED};
    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        const char *name = scenario_str(all[i]);
        assert(name != NULL);
        scenario_t back;
        assert(scenario_from_str(name, &back) == ERR_OK);
        assert(back == all[i]);
    }
    assert(scenario_str((scenario_t)99) != NULL);

    scenario_t dummy;
    assert(scenario_from_str("없는시나리오", &dummy) == ERR_NOT_FOUND);
    assert(scenario_from_str(NULL, &dummy) == ERR_NULL_PTR);
    assert(scenario_from_str("BALANCED", NULL) == ERR_NULL_PTR);
}

static void write_cfg(const char *body)
{
    FILE *f = fopen(CFG_PATH, "w");
    assert(f != NULL);
    fputs(body, f);
    fclose(f);
}

/* 설정 파일 */
static void test_config_file(void)
{
    divergent_config_t cfg;

    write_cfg("# 시나리오 설정\n"
              "scenario = CROSSED\n"
              "seed = 12345\n"
              "\n"
              "ref_price = 10000   # 기준가\n"
              "price_low = 7000\n"
              "price_high = 13000\n"
              "start_ts = 500\n"
              "orders_per_market = 2000\n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_OK);
    assert(cfg.scenario == SCENARIO_CROSSED);
    assert(cfg.seed == 12345);
    assert(cfg.ref_price == 10000);
    assert(cfg.price_low == 7000);
    assert(cfg.price_high == 13000);
    assert(cfg.start_ts == 500);
    assert(cfg.orders_per_market == 2000);

    /* 읽어 온 설정으로 실제로 만들어진다 */
    divergent_t *div = divergent_create(&cfg);
    assert(div != NULL);
    divergent_destroy(div);

    /* 모르는 키는 거절한다 — 오타를 조용히 넘기지 않는다 */
    write_cfg("scenario = BALANCED\nseeed = 1\n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_INVALID_ARG);

    /* 숫자가 아닌 값 */
    write_cfg("seed = 열둘\n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_INVALID_ARG);
    write_cfg("ref_price = 10000원\n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_INVALID_ARG);
    write_cfg("seed = -1\n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_INVALID_ARG);

    /* 없는 시나리오 이름 */
    write_cfg("scenario = ZIGZAG\n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_INVALID_ARG);

    /* '=' 없는 줄 */
    write_cfg("scenario BALANCED\n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_INVALID_ARG);

    /* 값이 빈 줄 */
    write_cfg("seed =\n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_INVALID_ARG);

    /* 주석과 빈 줄만 있어도 파일 자체는 유효하다 */
    write_cfg("# 아무것도 없음\n\n   \n");
    assert(divergent_load(CFG_PATH, &cfg) == ERR_OK);

    assert(divergent_load("없는파일.cfg", &cfg) == ERR_NOT_FOUND);
    assert(divergent_load(NULL, &cfg) == ERR_NULL_PTR);
    assert(divergent_load(CFG_PATH, NULL) == ERR_NULL_PTR);

    remove(CFG_PATH);
}

int main(void)
{
    test_create_rejects();
    test_scenario_names();
    test_config_file();
    test_markets_differ();
    test_balanced();
    test_krx_thin();
    test_nxt_thin();
    test_crossed();
    test_reproducible();
    return 0;
}
