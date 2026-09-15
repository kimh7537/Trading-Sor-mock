/*
 * T2-12 라우팅 판단 근거 로깅.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 주문마다 "왜 이 시장을 골랐는가"를 **항목별 점수와 함께** 남긴다
 *  2. 이벤트 싱크 방식 (T1-12와 같은 형태)
 *  3. **로그만 보고 라우팅 결정을 재현할 수 있어야 한다**
 *  4. 순서가 결정적
 *
 * 3번이 이 테스트의 중심이다. "재현할 수 있다"를 말로 확인하지 않는다 —
 * **로그에 담긴 값만으로 총점을 다시 계산하고, 어느 시장이 이겼는지 다시 고른다.**
 * 그 결과가 실제 배분과 맞는지 본다. 근거가 모자라면 이 검산이 성립하지 않는다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "match.h"
#include "routing_log.h"

#define BASE 10000
#define CAP 512
#define T_BOTH TOD_NS(12, 0, 0)
#define T_CLOSED TOD_NS(23, 0, 0)
#define LOG_MAX 64

static order_id_t MAKER_ID = 1000000;
static order_id_t NEXT_ID = 1;

/* 싱크가 결정을 쌓는 곳. 소비자가 무엇을 하든 엔진은 모른다. */
typedef struct {
    routing_decision_t rec[LOG_MAX];
    int32_t            count;
} log_buf_t;

static void collect(const routing_decision_t *d, void *ctx)
{
    log_buf_t *log = ctx;
    assert(log->count < LOG_MAX);
    log->rec[log->count++] = *d;
}

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
    exec_context_t  ctx;
    log_buf_t       log;
    routing_sink_t  sink;
} fixture_t;

static void fx_init(fixture_t *fx, bool with_rules, ts_t ts)
{
    const market_rules_t *rules[MARKET_COUNT] = {
        [MARKET_KRX] = &KRX_RULES,
        [MARKET_NXT] = &NXT_RULES,
    };

    memset(&fx->log, 0, sizeof(fx->log));
    cons_init(&fx->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        fx->eng[m] = match_engine_create(BASE, CAP);
        assert(fx->eng[m] != NULL);
        assert(cons_attach(&fx->cons, (market_t)m, match_book(fx->eng[m]),
                           with_rules ? rules[m] : NULL) == ERR_OK);
    }
    fx->ctx.cons = &fx->cons;
    fx->ctx.weights = NULL;
    fx->ctx.config = NULL;
    fx->ctx.ts = ts;

    fx->sink.fn = collect;
    fx->sink.ctx = &fx->log;
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
    order_t       req;

    memset(&req, 0, sizeof(req));
    req.id = MAKER_ID++;
    req.ts = T_BOTH;
    req.side = side;
    req.price = price;
    req.qty = qty;
    req.type = ORDER_LIMIT;
    req.market = m;
    assert(match_limit(fx->eng[m], &req, &res) == ERR_OK);
}

static order_t buy_req(price_t limit, qty_t qty, ts_t ts)
{
    order_t r;
    memset(&r, 0, sizeof(r));
    r.id = NEXT_ID++;
    r.ts = ts;
    r.side = SIDE_BUY;
    r.price = limit;
    r.qty = qty;
    r.type = ORDER_LIMIT;
    return r;
}

/* --- 1. 항목별 점수가 남는가 --- */

static void test_records_component_scores(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 500);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 500); /* NXT가 더 싸다 */
    put(&fx, MARKET_KRX, SIDE_BUY, 9990, 500);
    put(&fx, MARKET_NXT, SIDE_BUY, 9980, 500);

    order_t     req = buy_req(10010, 100, T_BOTH);
    exec_plan_t plan;
    assert(routing_plan(&STRATEGY_BEST_PRICE, &fx.ctx, &req, &fx.sink, &plan) ==
           ERR_OK);

    assert(fx.log.count == 1);
    const routing_decision_t *d = &fx.log.rec[0];

    /* 주문이 그대로 남는다. */
    assert(d->logical_id == req.id);
    assert(d->ts == req.ts);
    assert(d->side == SIDE_BUY);
    assert(d->limit_price == 10010);
    assert(d->order_qty == 100);
    assert(strcmp(d->strategy, "BEST_PRICE") == 0);

    /* 판단의 재료가 남는다 — 이것이 없으면 검산이 불가능하다. */
    assert(d->scores[MARKET_KRX].quote == 10010);
    assert(d->scores[MARKET_NXT].quote == 10000);
    assert(d->scores[MARKET_KRX].fillable > 0);
    assert(d->scores[MARKET_NXT].fillable > 0);

    /* 항목별 점수가 넷 다 남는다. 합계만 남기지 않는다. */
    assert(d->scores[MARKET_NXT].price_score >
           d->scores[MARKET_KRX].price_score);
    assert(d->scores[MARKET_KRX].fill_score > 0);
    assert(d->scores[MARKET_NXT].cost_score > 0);
    assert(d->scores[MARKET_KRX].state_score > 0);

    /* 기준도 남는다. 같은 재료도 가중치가 다르면 답이 달라진다. */
    assert(d->weights.price == BE_WEIGHTS_DEFAULT.price);
    assert(d->config.fee_bp[MARKET_NXT] == BE_CONFIG_DEFAULT.fee_bp[MARKET_NXT]);

    /* 결과도 남는다. BEST_PRICE는 이긴 시장에 전량. */
    assert(d->alloc[MARKET_NXT] == 100);
    assert(d->alloc[MARKET_KRX] == 0);
    assert(d->leg_count == 1);
    assert(d->reason == ERR_OK);

    fx_free(&fx);
}

/*
 * --- 2. 완료 조건 3: 로그만 보고 결정을 재현한다 ---
 *
 * 로그에 담긴 항목별 점수와 가중치만으로 총점을 다시 계산한다. 그다음 그 점수로
 * 시장을 다시 골라, 실제로 물량이 간 시장과 같은지 본다.
 *
 * 이 검산이 통과한다는 것은 **판단을 되짚는 데 필요한 값이 로그에 다 있다**는 뜻이다.
 * 하나라도 빠지면 여기서 걸린다.
 */
static int32_t recompute_total(const venue_score_t *v, const be_weights_t *w)
{
    int64_t wsum = (int64_t)w->price + w->fill + w->cost + w->state;
    int64_t sum = (int64_t)v->price_score * w->price +
                  (int64_t)v->fill_score * w->fill +
                  (int64_t)v->cost_score * w->cost +
                  (int64_t)v->state_score * w->state;
    return (int32_t)(sum / wsum);
}

static void replay_one(const routing_decision_t *d)
{
    /* 총점을 로그의 값만으로 다시 계산한다. */
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (!d->scores[m].eligible) {
            continue;
        }
        assert(recompute_total(&d->scores[m], &d->weights) ==
               d->scores[m].total);
    }

    /* 그 점수로 시장을 다시 고른다. */
    market_t picked;
    int      rc = be_pick(d->scores, &picked);

    if (rc != ERR_OK) {
        /*
         * 고를 후보가 없었다. 그러면 물량이 어디로 갔든 그 근거는 평가가 아니라
         * 등록 시장 규칙이다 — 로그의 reason이 그 사실을 들고 있다.
         */
        return;
    }

    /*
     * 평가로 고르는 전략(BEST_PRICE)은 이긴 시장에 전량 보낸다.
     * 로그만 보고 그 결론에 도달할 수 있어야 한다.
     */
    if (strcmp(d->strategy, "BEST_PRICE") == 0 && d->reason == ERR_OK) {
        assert(d->alloc[picked] == d->order_qty);
    }
}

static void test_log_alone_reproduces_decision(void)
{
    /* 유동성 배치를 바꿔 가며, 어느 경우에도 검산이 성립하는지 본다. */
    static const price_t KRX_ASK[] = {9990, 10000, 10010, 10020};
    static const price_t NXT_ASK[] = {9990, 10000, 10010, 10020};
    static const qty_t   QTY[] = {10, 100, 1000};

    for (int32_t a = 0; a < 4; a++) {
        for (int32_t b = 0; b < 4; b++) {
            for (int32_t q = 0; q < 3; q++) {
                fixture_t fx;
                fx_init(&fx, false, T_BOTH);

                put(&fx, MARKET_KRX, SIDE_SELL, KRX_ASK[a], 500);
                put(&fx, MARKET_NXT, SIDE_SELL, NXT_ASK[b], 500);
                put(&fx, MARKET_KRX, SIDE_BUY, 9900, 500);
                put(&fx, MARKET_NXT, SIDE_BUY, 9900, 500);

                order_t     req = buy_req(10020, QTY[q], T_BOTH);
                exec_plan_t plan;
                assert(routing_plan(&STRATEGY_BEST_PRICE, &fx.ctx, &req,
                                    &fx.sink, &plan) == ERR_OK);

                assert(fx.log.count == 1);
                replay_one(&fx.log.rec[0]);

                fx_free(&fx);
            }
        }
    }
}

/*
 * 기본값이 아닌 기준으로 돌렸으면 **그 기준이 남아야 한다.**
 *
 * 기본값을 적어 두면 로그는 그럴듯해 보이지만 검산이 틀린 답을 낸다 — 다른 잣대로
 * 잰 점수를 기본 가중치로 되짚게 되기 때문이다. 기본값과 다른 가중치·수수료로
 * 돌려서 그 경우를 잡는다.
 */
static void test_records_actual_criteria(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 500);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 500);
    put(&fx, MARKET_KRX, SIDE_BUY, 9990, 500);
    put(&fx, MARKET_NXT, SIDE_BUY, 9980, 500);

    /* 기본값(40/30/20/10, 수수료 3/2)과 모든 항목이 다르다. */
    be_weights_t w = {11, 22, 33, 44};
    be_config_t  cfg = {{7, 5}};
    fx.ctx.weights = &w;
    fx.ctx.config = &cfg;

    order_t     req = buy_req(10010, 100, T_BOTH);
    exec_plan_t plan;
    assert(routing_plan(&STRATEGY_BEST_PRICE, &fx.ctx, &req, &fx.sink, &plan) ==
           ERR_OK);

    const routing_decision_t *d = &fx.log.rec[0];
    assert(d->weights.price == 11 && d->weights.fill == 22);
    assert(d->weights.cost == 33 && d->weights.state == 44);
    assert(d->config.fee_bp[MARKET_KRX] == 7);
    assert(d->config.fee_bp[MARKET_NXT] == 5);

    /* 남은 기준으로 검산이 성립한다. 기본값을 적어 뒀다면 여기서 어긋난다. */
    replay_one(d);

    char buf[512];
    assert(routing_format(d, buf, sizeof(buf)) > 0);
    assert(strstr(buf, "w=11/22/33/44") != NULL);
    assert(strstr(buf, "fee=7/5") != NULL);

    fx_free(&fx);
}

/* --- 3. 못 세운 계획도 근거를 남긴다 --- */

static void test_failure_is_logged_too(void)
{
    fixture_t fx;
    fx_init(&fx, true, T_CLOSED); /* 양 시장 마감 */

    order_t     req = buy_req(10000, 100, T_CLOSED);
    exec_plan_t plan;
    int rc = routing_plan(&STRATEGY_BEST_PRICE, &fx.ctx, &req, &fx.sink, &plan);
    assert(rc != ERR_OK);

    /* 왜 아무 데도 안 보냈는지가 근거의 일부다. 기록이 빠지면 안 된다. */
    assert(fx.log.count == 1);
    const routing_decision_t *d = &fx.log.rec[0];
    assert(d->leg_count == 0);
    assert(d->reason == rc);
    assert(!d->scores[MARKET_KRX].eligible);
    assert(!d->scores[MARKET_NXT].eligible);
    /* 후보가 없었던 이유도 시장별로 남는다. */
    assert(d->scores[MARKET_KRX].reason != ERR_OK);

    fx_free(&fx);
}

/* --- 4. 순서가 결정적인가 --- */

/*
 * 같은 입력 시퀀스는 같은 결정 시퀀스를 만든다.
 *
 * 필드를 하나씩 비교하지 않고 **기록 전체를 바이트로 비교한다**(T1-19와 같은 방법).
 * 그래야 나중에 필드를 더해도 비교가 저절로 따라온다. 구조체 패딩까지 밀어 두지
 * 않으면 이 비교가 깨지므로, 이 테스트가 곧 그 습관의 검사이기도 하다.
 */
/*
 * 스택을 더럽힌다.
 *
 * 같은 프로세스에서 같은 순서로 두 번 돌리면 스택 배치도 같아서, 기록 구조체의
 * 패딩이 **우연히** 일치한다. 그러면 "패딩을 밀지 않았다"는 버그를 비교로 잡을 수
 * 없다 — T1-19에서 실제로 당했던 함정이다.
 *
 * 그래서 한쪽 실행에서는 호출 직전에 스택을 0xAA로 덮어 둔다. 기록을 만들 때
 * memset을 먼저 하지 않으면 두 실행의 패딩이 달라지고 비교가 깨진다.
 */
static void dirty_stack(void)
{
    volatile unsigned char pad[4096];

    for (size_t i = 0; i < sizeof(pad); i++) {
        pad[i] = 0xAA;
    }
}

static void run_sequence(log_buf_t *out, bool dirty)
{
    fixture_t fx;

    NEXT_ID = 1; /* 같은 입력이려면 주문번호도 같아야 한다 */
    MAKER_ID = 1000000;
    fx_init(&fx, false, T_BOTH);

    static const exec_strategy_t *STRATS[] = {
        &STRATEGY_KRX_ONLY,
        &STRATEGY_BEST_PRICE,
        &STRATEGY_SPLIT,
        &STRATEGY_SWEEP,
    };

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 40);
    put(&fx, MARKET_KRX, SIDE_SELL, 10010, 60);
    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 70);
    put(&fx, MARKET_NXT, SIDE_BUY, 9990, 50);
    put(&fx, MARKET_KRX, SIDE_BUY, 9980, 50);

    for (int32_t s = 0; s < 4; s++) {
        for (qty_t q = 10; q <= 200; q += 30) {
            order_t     req = buy_req(10010, q, T_BOTH);
            exec_plan_t plan;

            if (dirty) {
                dirty_stack();
            }
            (void)routing_plan(STRATS[s], &fx.ctx, &req, &fx.sink, &plan);
        }
    }

    *out = fx.log;
    fx_free(&fx);
}

static void test_deterministic_order(void)
{
    log_buf_t a;
    log_buf_t b;

    run_sequence(&a, false);
    run_sequence(&b, true); /* 스택이 달라도 기록은 바이트까지 같아야 한다 */

    assert(a.count == b.count);
    assert(a.count > 0);
    /* 패딩까지 같아야 한다. 기록을 만들 때 memset을 먼저 하는 이유다. */
    assert(memcmp(a.rec, b.rec, sizeof(routing_decision_t) * (size_t)a.count) ==
           0);

    /* 결정 순서가 주문 순서 그대로다. */
    for (int32_t i = 1; i < a.count; i++) {
        assert(a.rec[i].logical_id > a.rec[i - 1].logical_id);
    }
}

/* --- 5. 텍스트 형식 --- */

static void test_format(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_NXT, SIDE_SELL, 10000, 300);

    order_t     req = buy_req(10000, 100, T_BOTH);
    exec_plan_t plan;
    assert(routing_plan(&STRATEGY_BEST_PRICE, &fx.ctx, &req, &fx.sink, &plan) ==
           ERR_OK);

    char buf[512];
    int  n = routing_format(&fx.log.rec[0], buf, sizeof(buf));
    assert(n > 0);
    assert((size_t)n == strlen(buf));

    /* 검산에 필요한 값이 텍스트에도 다 들어간다. */
    assert(strstr(buf, "strategy=BEST_PRICE") != NULL);
    assert(strstr(buf, "side=BUY") != NULL);
    assert(strstr(buf, "qty=100") != NULL);
    assert(strstr(buf, "KRX[") != NULL);
    assert(strstr(buf, "NXT[") != NULL);
    assert(strstr(buf, "quote=10000") != NULL);
    assert(strstr(buf, "alloc=100") != NULL);
    assert(strstr(buf, "reason=0") != NULL);

    /* 같은 결정은 언제 불러도 같은 문자열이다 — 시각도 난수도 읽지 않는다. */
    char again[512];
    assert(routing_format(&fx.log.rec[0], again, sizeof(again)) == n);
    assert(strcmp(buf, again) == 0);

    /* 총점이 텍스트에도 들어간다 — 이것이 없으면 검산을 못 한다. */
    char want[64];
    snprintf(want, sizeof(want), "total=%d", fx.log.rec[0].scores[MARKET_NXT].total);
    assert(strstr(buf, want) != NULL);
    snprintf(want, sizeof(want), "total=%d", fx.log.rec[0].scores[MARKET_KRX].total);
    assert(strstr(buf, want) != NULL);

    /*
     * 버퍼가 모자라면 넘치지 않고 거절한다.
     * **자르는 지점을 세 군데로 나눈다** — 머리글에서 모자란 경우만 보면 시장 블록과
     * 꼬리의 넘침 검사를 지워도 테스트가 통과한다.
     */
    char small[16];
    assert(routing_format(&fx.log.rec[0], small, sizeof(small)) ==
           ERR_INVALID_ARG);
    for (size_t cap = 1; cap <= (size_t)n; cap++) {
        char probe[512];
        memset(probe, 0x5A, sizeof(probe));
        assert(routing_format(&fx.log.rec[0], probe, cap) == ERR_INVALID_ARG);
        /* 받은 크기 밖으로는 한 바이트도 쓰지 않는다. */
        for (size_t i = cap; i < sizeof(probe); i++) {
            assert(probe[i] == 0x5A);
        }
    }
    /* 딱 맞는 크기(널 포함)면 성공한다. */
    char exact[512];
    assert(routing_format(&fx.log.rec[0], exact, (size_t)n + 1) == n);
    assert(routing_format(NULL, buf, sizeof(buf)) == ERR_NULL_PTR);
    assert(routing_format(&fx.log.rec[0], NULL, sizeof(buf)) == ERR_NULL_PTR);
    assert(routing_format(&fx.log.rec[0], buf, 0) == ERR_NULL_PTR);

    fx_free(&fx);
}

/* --- 6. 싱크 규약 --- */

static void test_sink_contract(void)
{
    fixture_t fx;
    fx_init(&fx, false, T_BOTH);

    put(&fx, MARKET_KRX, SIDE_SELL, 10000, 300);

    order_t     req = buy_req(10000, 100, T_BOTH);
    exec_plan_t plan;

    /* 싱크가 없어도 전략은 그대로 돈다. */
    assert(routing_plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, NULL, &plan) ==
           ERR_OK);
    assert(plan.leg_count == 1);
    assert(fx.log.count == 0);

    routing_sink_t empty = {NULL, NULL};
    order_t        req2 = buy_req(10000, 100, T_BOTH);
    assert(routing_plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req2, &empty, &plan) ==
           ERR_OK);
    assert(fx.log.count == 0);

    /* 인자 검사. */
    assert(routing_plan(NULL, &fx.ctx, &req, &fx.sink, &plan) == ERR_NULL_PTR);
    assert(routing_plan(&STRATEGY_KRX_ONLY, NULL, &req, &fx.sink, &plan) ==
           ERR_NULL_PTR);
    assert(routing_plan(&STRATEGY_KRX_ONLY, &fx.ctx, NULL, &fx.sink, &plan) ==
           ERR_NULL_PTR);
    assert(routing_plan(&STRATEGY_KRX_ONLY, &fx.ctx, &req, &fx.sink, NULL) ==
           ERR_NULL_PTR);

    /* NULL을 흘려도 아무 일도 일어나지 않는다. */
    routing_emit(NULL, NULL);
    routing_emit(&fx.sink, NULL);
    routing_emit(&empty, &fx.log.rec[0]);
    assert(fx.log.count == 0);

    fx_free(&fx);
}

int main(void)
{
    test_records_component_scores();
    test_log_alone_reproduces_decision();
    test_records_actual_criteria();
    test_failure_is_logged_too();
    test_deterministic_order();
    test_format();
    test_sink_contract();
    return 0;
}
