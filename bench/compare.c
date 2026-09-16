#include "compare.h"

#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "executor.h"
#include "match.h"
#include "routing_log.h"
#include "tick_size.h"

const compare_config_t COMPARE_DEFAULT = {
    .seed = 20260916,
    .ref_price = 10000,
    .price_low = 7000,
    .price_high = 13000,
    .start_ts = TOD_NS(10, 0, 0),
    .liquidity_orders = 400,
    .taker_orders = 200,
    .taker_qty_min = 50,
    .taker_qty_max = 500,
    .aggression_ticks = 5,
};

/* 표의 열 순서. 0번이 기준선이다. */
static const exec_strategy_t *const STRATS[COMPARE_STRATEGY_COUNT] = {
    &STRATEGY_KRX_ONLY,
    &STRATEGY_BEST_PRICE,
    &STRATEGY_SPLIT,
    &STRATEGY_SWEEP,
};

const exec_strategy_t *compare_strategy(int32_t index)
{
    if (index < 0 || index >= COMPARE_STRATEGY_COUNT) {
        return NULL;
    }
    return STRATS[index];
}

/*
 * 측정 대상 주문(taker) 생성기.
 *
 * 유동성 생성기(T1-18)를 쓰지 않는다. 그쪽은 기준가 아래에 매수, 위에 매도를 놓아
 * **교차하지 않는** 호가를 만든다 — 체결이 나지 않으면 체결 단가를 잴 수 없다.
 * 여기서 필요한 것은 반대로 호가를 파고드는 주문이다.
 *
 * xorshift64*를 직접 돌린다. 시드에서만 나오고 시스템 시각을 읽지 않으므로
 * 같은 시드는 언제나 같은 주문 열을 만든다.
 */
typedef struct {
    uint64_t   state;
    order_id_t next_id;
    ts_t       now;
} taker_gen_t;

static uint64_t next_u64(taker_gen_t *g)
{
    uint64_t x = g->state;

    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g->state = x;
    return x * 0x2545f4914f6cdd1dULL;
}

static void taker_init(taker_gen_t *g, const compare_config_t *cfg)
{
    /* 유동성 시드와 같은 값을 쓰면 두 수열이 같이 움직인다. 섞어서 떼어 놓는다. */
    g->state = cfg->seed ^ 0x9e3779b97f4a7c15ULL;
    if (g->state == 0) {
        g->state = 1; /* xorshift는 0 상태를 벗어나지 못한다 */
    }
    g->next_id = COMPARE_LOGICAL_ID_BASE;
    g->now = cfg->start_ts;
}

static void taker_next(taker_gen_t *g, const compare_config_t *cfg,
                       order_t *out)
{
    memset(out, 0, sizeof(*out));

    qty_t span = cfg->taker_qty_max - cfg->taker_qty_min + 1;
    qty_t qty = cfg->taker_qty_min + (qty_t)(next_u64(g) % (uint64_t)span);

    /*
     * 기준가에서 aggression_ticks만큼 위를 지정가로 잡는다. 여러 단을 파고들어야
     * 전략 간 차이가 드러난다 — 한 단만 먹고 끝나면 어느 전략이든 같은 값이 나온다.
     */
    price_t tick = tick_size_of(cfg->ref_price);
    price_t limit = cfg->ref_price + tick * cfg->aggression_ticks;
    price_t aligned = round_to_tick(limit, true);
    if (aligned > 0) {
        limit = aligned;
    }

    out->id = g->next_id++;
    out->ts = g->now;
    out->side = SIDE_BUY;
    out->price = limit;
    out->qty = qty;
    out->type = ORDER_LIMIT;

    /* 논리 시각은 일정하게 나아간다. 도착 분포는 여기서 재는 대상이 아니다. */
    g->now += 1000000; /* 1ms */
}

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
    venues_t        venues;
    order_map_t    *map;
} venue_set_t;

static void vs_free(venue_set_t *vs)
{
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(vs->eng[m]);
        vs->eng[m] = NULL;
    }
    omap_destroy(vs->map);
    vs->map = NULL;
}

/*
 * 엔진을 새로 만들고 같은 시드로 유동성을 채운다.
 *
 * **전략마다 이것을 다시 한다.** 이것이 "유동성이 전략마다 동일해야 한다"는 완료
 * 조건을 지키는 방법이다.
 */
static int vs_build(venue_set_t *vs, const compare_config_t *cfg,
                    scenario_t scenario)
{
    memset(vs, 0, sizeof(*vs));

    divergent_config_t dcfg;
    memset(&dcfg, 0, sizeof(dcfg));
    dcfg.scenario = scenario;
    dcfg.seed = cfg->seed;
    dcfg.ref_price = cfg->ref_price;
    dcfg.price_low = cfg->price_low;
    dcfg.price_high = cfg->price_high;
    dcfg.start_ts = cfg->start_ts;
    dcfg.orders_per_market = cfg->liquidity_orders;

    divergent_t *div = divergent_create(&dcfg);
    if (div == NULL) {
        return ERR_INVALID_ARG;
    }

    /* 호가창 용량은 유동성 + 측정 주문의 다리를 다 담을 만큼. */
    int32_t cap = cfg->liquidity_orders + cfg->taker_orders * 2 + 64;

    cons_init(&vs->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        vs->eng[m] =
            match_engine_create(divergent_ref_price(div, (market_t)m), cap);
        if (vs->eng[m] == NULL) {
            divergent_destroy(div);
            vs_free(vs);
            return ERR_POOL_EXHAUSTED;
        }
        /*
         * 규칙 테이블은 걸지 않는다. 세션 규칙은 T1-13/T1-14가 따로 검증하고,
         * 여기서 걸면 "어느 시장이 열려 있었나"가 체결 단가 차이에 섞인다.
         * 이 실험이 재려는 것은 라우팅이지 개장 시간이 아니다.
         */
        if (cons_attach(&vs->cons, (market_t)m, match_book(vs->eng[m]), NULL) !=
            ERR_OK) {
            divergent_destroy(div);
            vs_free(vs);
            return ERR_INVALID_ARG;
        }
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        synth_gen_t *gen = divergent_gen(div, (market_t)m);
        for (int32_t i = 0; i < cfg->liquidity_orders; i++) {
            order_t       o;
            exec_result_t res;

            if (synth_next(gen, &o) != ERR_OK) {
                break;
            }
            o.market = (market_t)m;
            /* 거절되는 주문이 있어도 실험은 계속된다 — 그것도 시장의 일부다. */
            (void)match_limit(vs->eng[m], &o, &res);
        }
    }

    divergent_destroy(div);

    vs->map = omap_create(cfg->taker_orders + 8);
    if (vs->map == NULL) {
        vs_free(vs);
        return ERR_POOL_EXHAUSTED;
    }
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        vs->venues.eng[m] = vs->eng[m];
    }

    return ERR_OK;
}

static int run_cell(const compare_config_t *cfg, scenario_t scenario,
                    int32_t strat_index, compare_row_t *row)
{
    venue_set_t vs;
    int         rc = vs_build(&vs, cfg, scenario);
    if (rc != ERR_OK) {
        return rc;
    }

    const exec_strategy_t *strategy = STRATS[strat_index];

    memset(row, 0, sizeof(*row));
    row->strategy = strategy_name(strategy);
    row->scenario = scenario;

    taker_gen_t tg;
    taker_init(&tg, cfg);

    exec_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cons = &vs.cons;
    ctx.weights = NULL;
    ctx.config = NULL;

    for (int32_t i = 0; i < cfg->taker_orders; i++) {
        order_t req;
        taker_next(&tg, cfg, &req);
        ctx.ts = req.ts;

        /*
         * 기준가는 **집행 전에** 잡는다. 집행 뒤의 호가는 그 집행이 밀어 놓은
         * 결과라서, 자기가 만든 변화를 자기 기준으로 삼는 순환이 된다(T2-13).
         */
        price_t bench = 0;
        if (eq_benchmark(&vs.cons, &req, ctx.ts, &bench) != ERR_OK) {
            /* 상대 호가가 하나도 없다. 잴 것이 없으므로 이 주문은 건너뛴다. */
            continue;
        }

        exec_plan_t plan;
        if (routing_plan(strategy, &ctx, &req, NULL, &plan) != ERR_OK) {
            row->order_qty += req.qty;
            continue;
        }

        exec_report_t rep;
        (void)exec_submit(vs.map, &vs.venues, &req, &plan, &rep);

        row->order_qty += req.qty;
        row->filled_qty += rep.filled_qty;
        row->notional += rep.notional;
        row->bench_notional += (int64_t)bench * (int64_t)rep.filled_qty;
        row->rejected_legs += rep.rejected_count;
    }

    row->avg_price = eq_avg_price(row->notional, row->filled_qty);
    row->fill_rate_bp = eq_to_bp(row->filled_qty, row->order_qty);
    row->slippage_bp =
        eq_to_bp(row->notional - row->bench_notional, row->bench_notional);

    vs_free(&vs);
    return ERR_OK;
}

int compare_run(const compare_config_t *cfg, compare_result_t *out)
{
    if (out == NULL) {
        return ERR_NULL_PTR;
    }

    compare_config_t c = (cfg != NULL) ? *cfg : COMPARE_DEFAULT;
    if (c.taker_orders <= 0 || c.liquidity_orders <= 0 || c.seed == 0 ||
        c.taker_qty_min < QTY_MIN || c.taker_qty_max < c.taker_qty_min) {
        return ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->cfg = c;

    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            int rc = run_cell(&c, (scenario_t)s, k, &out->row[s][k]);
            if (rc != ERR_OK) {
                return rc;
            }
        }

        /*
         * 기준선 대비 차이를 낸다. **매수이므로 싸게 샀을수록 좋다** — 기준선보다
         * 낮은 평균 단가가 양수 bp가 되도록 (기준선 - 이 전략)으로 잡는다.
         *
         * **원 단위 평균 단가(`avg_price`)끼리 빼지 않는다**(T6-07). 처음엔 그렇게
         * 했는데, 10,000원에서 1원 버림이 1bp라 세 전략 사이의 1bp 안팎 차이가
         * 사라져 30개 시드 내내 셋이 똑같이 나왔다. `execution_quality.h`가 적어
         * 둔 "bp는 체결 금액에서 직접"을 여기서만 어겼던 것이다.
         */
        const compare_row_t *base = &out->row[s][0];
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            compare_row_t *r = &out->row[s][k];
            r->vs_krx_only_bp = eq_avg_diff_bp(base->notional, base->filled_qty,
                                               r->notional, r->filled_qty);
        }
    }

    return ERR_OK;
}

int compare_write_md(const compare_result_t *res, const char *date,
                     const char *path)
{
    if (res == NULL || date == NULL || path == NULL) {
        return ERR_NULL_PTR;
    }

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return ERR_NOT_FOUND;
    }

    fprintf(f, "# 집행 전략 비교 (%s)\n\n", date);
    fprintf(f,
            "시나리오 %d종 x 전략 %d종 = %d개 조합. "
            "**전략마다 호가창을 새로 만들어 같은 시드로 다시 채운다** — "
            "전략이 호가창을 바꾸므로 그러지 않으면 비교가 아니라 순서 측정이 "
            "된다.\n\n",
            COMPARE_SCENARIO_COUNT, COMPARE_STRATEGY_COUNT,
            COMPARE_SCENARIO_COUNT * COMPARE_STRATEGY_COUNT);

    fprintf(f, "## 설정\n\n");
    fprintf(f, "| 항목 | 값 |\n|---|---|\n");
    fprintf(f, "| 시드 | %llu |\n", (unsigned long long)res->cfg.seed);
    fprintf(f, "| 기준가 | %d |\n", res->cfg.ref_price);
    fprintf(f, "| 시장당 유동성 주문 | %d |\n", res->cfg.liquidity_orders);
    fprintf(f, "| 측정 주문 | %d (매수만) |\n", res->cfg.taker_orders);
    fprintf(f, "| 측정 주문 수량 | %d ~ %d |\n", res->cfg.taker_qty_min,
            res->cfg.taker_qty_max);
    fprintf(f, "| 지정가 공격도 | 기준가 + %d틱 |\n\n",
            res->cfg.aggression_ticks);

    fprintf(f,
            "기준가(benchmark)는 **접수 시점의 통합 최우선 매도호가**다. 근거는 "
            "`sor/include/execution_quality.h`에 있다.\n\n");

    fprintf(f, "## 결과\n\n");
    fprintf(f,
            "| 시나리오 | 전략 | 평균 체결 단가 | 슬리피지(bp) | 체결률 | "
            "KRX_ONLY 대비(bp) |\n");
    fprintf(f, "|---|---|---:|---:|---:|---:|\n");

    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            const compare_row_t *r = &res->row[s][k];
            fprintf(f, "| %s | %s | %d | %+d | %d.%02d%% | %+d |\n",
                    scenario_str(r->scenario), r->strategy, r->avg_price,
                    r->slippage_bp, r->fill_rate_bp / 100,
                    r->fill_rate_bp % 100, r->vs_krx_only_bp);
        }
    }

    fprintf(f, "\n## 읽는 법\n\n");
    fprintf(f,
            "- **슬리피지(bp)**: 접수 시점 통합 최우선호가 대비. 양수면 그만큼 "
            "불리하게 샀다\n");
    fprintf(f,
            "- **KRX_ONLY 대비(bp)**: 기준선의 평균 체결 단가 대비. "
            "**양수면 그만큼 싸게 샀다** (매수이므로 낮은 단가가 이득)\n");
    fprintf(f,
            "- **체결률**: 낸 수량 중 체결된 비율. 단가가 좋아도 체결률이 낮으면 "
            "남은 수량을 나중에 더 비싸게 사게 된다 — 두 숫자를 함께 봐야 한다\n");
    fprintf(f,
            "\n반올림은 0에서 먼 쪽으로 한다. 손해를 낙관적으로 표시하지 않기 "
            "위해서다.\n");

    fprintf(f, "\n## 이 실험이 말하지 못하는 것\n\n");
    fprintf(f,
            "- **매수만 낸다.** 매수와 매도의 평균 체결 단가를 한 숫자로 합치면 "
            "서로를 상쇄해 의미가 없어지기 때문이다. 그래서 CROSSED처럼 한쪽 "
            "기준가가 밀린 시나리오에서는 밀린 시장이 **불리한 쪽으로만** "
            "나타난다 — 그 시장이 유리해지는 국면은 이 표에 없다\n");
    fprintf(f,
            "- **KRX_ONLY를 뺀 세 전략의 체결률과 KRX_ONLY 대비가 같게 나오는 "
            "것은 우연도 반올림도 아니다.** 셋 다 양 시장에 접근하므로 지정가 "
            "안에서 가져갈 수 있는 물량이 같고, 결국 **같은 호가를 다 먹는다.** "
            "체결 금액을 원 단위로 찍어 보면 KRX_THIN·NXT_THIN·CROSSED는 셋이 "
            "완전히 같고, BALANCED는 5억 원 중 수십~수천 원 차이(0.01bp 미만)다. "
            "셋의 차이는 *어떤 순서로 채웠는가*이고, 그것은 평균 단가가 아니라 "
            "**주문마다 잰 슬리피지**(위 표의 0~2bp)에서 드러난다\n");
    fprintf(f,
            "- **세션 규칙을 걸지 않았다.** 걸면 \"어느 시장이 열려 있었나\"가 "
            "체결 단가 차이에 섞인다. 개장 시간의 영향은 T1-13/T1-14가 따로 "
            "검증한다\n");
    fprintf(f,
            "- 한 시드의 한 장면이다. 전략의 우열을 일반화하려면 시드를 바꿔 "
            "여러 번 돌려야 한다 — `compare_strategies <날짜> <경로> <시드>`\n");

    fclose(f);
    return ERR_OK;
}
