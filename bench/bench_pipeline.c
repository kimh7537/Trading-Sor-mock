/*
 * T5-05 전 구간 성능 측정.
 *
 * ===========================================================================
 * 조각의 속도를 전체의 속도로 읽으면 안 된다
 * ===========================================================================
 *
 * T1-20은 **매칭 엔진만** 재서 3.8M TPS를 얻었다. 그 숫자는 그 조각의
 * 것이다. 주문 한 건이 실제로 지나는 길에는 원장 검증도, 저널도, 라우팅도
 * 있고, 그중 어느 하나가 매칭보다 천 배 비싸면 전체는 그것의 속도가 된다.
 *
 * 그래서 여기서는 단계를 나눠 재고 **가장 비싼 단계를 이름으로 지목한다.**
 * "전 구간 N μs"만 적으면 어디를 고쳐야 하는지 아무도 모른다.
 *
 * 재는 단계 — 주문 한 건이 지나는 순서 그대로.
 *
 *   1. 전문 해석    바이트 -> 구조체 (채널계가 보낸 것을 원장이 읽는다)
 *   2. 원장 검증    계좌 잠금 + 한도 + 증거금 묶기
 *   3. 저널 기록    입력을 덧붙인다 (T5-01)
 *   4. SOR 계획     통합 호가창을 보고 시장별 몫을 정한다
 *   5. 물리 등록    논리 주문을 물리 다리들로 펼친다
 *   6. 매칭         시장별로 체결시킨다
 *   7. 체결 반영    물리 체결을 논리 주문에 합산한다
 *
 * ===========================================================================
 * 평균이 아니라 분위수
 * ===========================================================================
 *
 * T1-20과 같은 이유다. 비용이 균일하지 않다 — 대부분의 주문은 한 레벨에서
 * 끝나지만 여러 레벨을 가로지르는 주문은 수십 배 걸린다. 평균은 그 꼬리를
 * 감춘다. 운영에서 아픈 것은 평균이 아니라 p99다.
 *
 * ===========================================================================
 * 저널을 켠 것과 끈 것을 나란히 잰다
 * ===========================================================================
 *
 * T5-01은 "매 레코드 `fsync`"를 골랐다. 잃으면 안 되는 기록이라는 이유였고
 * 그 이유는 지금도 맞다. 하지만 **그 선택의 값이 얼마인지는 재기 전에는
 * 아무도 몰랐다.** 숫자를 보고 나서 다시 판단할 수 있어야 한다.
 *
 * 여기서만 시스템 시각을 읽는다. 엔진은 읽지 않는다 — 벤치는 엔진 밖이다.
 *
 * ===========================================================================
 * 이 측정이 **덮지 않는** 것
 * ===========================================================================
 *
 * 변이 검사 8종 중 셋이 살아남았고, 셋 다 "이 워크로드가 그 경로를 안
 * 밟는다"가 이유였다. 테스트 구멍이 아니라 **측정의 경계**다.
 *
 *  - **거부되는 주문이 없다.** 계좌에 돈을 넉넉히 넣고 호가 단위에 맞는
 *    가격만 내므로 2만 건이 전부 통과한다. 그래서 거부 집계 경로와
 *    "0건이면 멈춘다"는 방어는 정상 측정에서 한 번도 돌지 않는다.
 *    (그 방어의 값은 따로 입증됐다 — 아래 참조)
 *
 *  - **논리 주문이 한 다리씩만 만들어진다.** BEST_PRICE는 유리한 한 시장을
 *    고르지 쪼개지 않는다. 그래서 매칭·체결 반영을 다리 수만큼 반복하는
 *    경로가 한 번도 돌지 않고, 이 두 단계의 숫자는 **한 시장에 보낸
 *    주문의 비용**이다. SPLIT/SWEEP으로 재면 달라진다.
 *    보고서가 다리 수 평균을 같이 적으므로 읽는 사람이 속지 않는다.
 *
 * ===========================================================================
 * 0건으로 낸 보고서는 보고서가 아니다
 * ===========================================================================
 *
 * 처음 돌렸을 때 주문 가격(기준가 +15원)이 호가 단위 10원에 안 맞아
 * **2만 건이 전부 원장에서 거부됐다.** 그런데도 프로그램은 성공으로 끝나고
 * "TPS 2300만"이라는 표를 냈다 — 그것은 거부 루프의 속도였다.
 *
 * 숫자가 그럴듯하면 아무도 "측정된 건수: 0"을 안 본다. 그래서 측정 건수가
 * 0이면 어느 단계에서 몇 건이 왜 떨어졌는지 찍고 **실패로 끝낸다.**
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "account.h"
#include "consolidated.h"
#include "divergent.h"
#include "errors.h"
#include "executor.h"
#include "journal.h"
#include "market_rules.h"
#include "match.h"
#include "msg.h"
#include "order_map.h"
#include "order_validate.h"
#include "routing_log.h"
#include "shm_segment.h"
#include "strategy.h"

/* --- 설정 --- */

#define WARMUP 2000
#define MEASURED 20000
#define LIQUIDITY 40000
#define REF_PRICE 10000
#define PRICE_LOW 9000
#define PRICE_HIGH 11000
#define START_TS TOD_NS(9, 1, 0)

/*
 * 기준가에서 얼마나 위에 놓는가. **호가 단위의 배수여야 한다** —
 * 5000~20000 구간의 단위는 10원이다(`tick_size.c`).
 * 상수로 두는 이유: 처음에 코드와 보고서에 따로 적었다가 한쪽만 고쳐서
 * 조건이 틀린 보고서가 나왔다.
 */
#define AGGRESSION 20
#define SEED 20260916u

#define ACCOUNT_NO "31940771"
#define SYMBOL "005930"

/* 계좌에 넣어 둘 돈. 측정 중에 증거금이 모자라면 경로가 짧아져 측정이 망가진다. */
#define CASH ((int64_t)1000000000000)

#define STAGE_COUNT 7
#define STAGE_JOURNAL 2

static const char *const STAGE_NAME[STAGE_COUNT] = {
    "전문 해석", "원장 검증", "저널 기록", "SOR 계획",
    "물리 등록", "매칭",      "체결 반영",
};

/* --- 분위수 --- */

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/* 정렬된 배열에서의 분위수. 보간하지 않는다 — 표본이 2만이면 의미가 없다. */
static int64_t percentile(const int64_t *sorted, size_t n, double p)
{
    if (n == 0) {
        return 0;
    }
    size_t idx = (size_t)(p * (double)n);
    if (idx >= n) {
        idx = n - 1;
    }
    return sorted[idx];
}

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/*
 * CPU 이름을 읽는다. 손으로 적으면 기계를 바꾼 뒤에도 옛 이름이 남는다 —
 * 조건이 틀린 보고서는 숫자가 맞아도 근거가 아니다.
 */
static void cpu_name(char *out, size_t cap)
{
    snprintf(out, cap, "%s", "unknown");

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f == NULL) {
        return;
    }
    char line[512];
    while (fgets(line, (int)sizeof(line), f) != NULL) {
        if (strncmp(line, "model name", 10) != 0) {
            continue;
        }
        const char *colon = strchr(line, ':');
        if (colon == NULL) {
            break;
        }
        colon++;
        while (*colon == ' ') {
            colon++;
        }
        snprintf(out, cap, "%s", colon);
        size_t n = strlen(out);
        while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == ' ')) {
            out[--n] = '\0';
        }
        break;
    }
    fclose(f);
}

/* --- 측정 대상 주문 생성 (시드에서만 나온다) --- */

typedef struct {
    uint64_t state;
    uint64_t next_id;
    ts_t     ts;
} taker_t;

static uint64_t next_u64(taker_t *g)
{
    /* splitmix64. 전역 rand()를 쓰지 않는다(CLAUDE.md). */
    g->state += 0x9E3779B97F4A7C15ull;
    uint64_t z = g->state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void taker_init(taker_t *g)
{
    g->state = SEED ^ 0x5DEECE66Dull;
    g->next_id = 200000000ull; /* 유동성 주문번호와 겹치지 않는 구간 */
    g->ts = START_TS;
}

static void taker_next(taker_t *g, msg_order_req_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->account, sizeof(out->account), "%s", ACCOUNT_NO);
    snprintf(out->symbol, sizeof(out->symbol), "%s", SYMBOL);
    out->cl_ord_id = g->next_id++;
    out->side = SIDE_BUY;
    out->type = ORDER_LIMIT;
    out->market = MARKET_KRX; /* SOR이 다시 정한다. 전문의 기본값일 뿐이다 */
    /*
     * 기준가보다 위에 놓아 대부분 체결되게 한다 — 매칭 비용이 실제로 들어야
     * 한다. **호가 단위에 맞춰야 한다**: 5000~20000 구간의 단위는 10원이라
     * (`tick_size.c`) 어긋난 값을 쓰면 원장이 전부 거부하고, 그러면 이 벤치는
     * 거부 루프의 속도를 재게 된다. 처음에 +15로 뒀다가 실제로 그랬다.
     */
    out->price = REF_PRICE + AGGRESSION;
    out->qty = (qty_t)(10 + (next_u64(g) % 41)); /* 10..50주 */

    g->ts += 1000000; /* 1ms */
}

/* --- 무대 --- */

typedef struct {
    match_engine_t *eng[MARKET_COUNT];
    cons_book_t     cons;
    venues_t        venues;
    order_map_t    *map;

    shm_segment_t  *seg;
    account_store_t store;
    int32_t         acct;

    validate_config_t vcfg;
} stage_t;

static void stage_free(stage_t *s)
{
    if (s->map != NULL) {
        omap_destroy(s->map);
        s->map = NULL;
    }
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        if (s->eng[m] != NULL) {
            match_engine_destroy(s->eng[m]);
            s->eng[m] = NULL;
        }
    }
    if (s->seg != NULL) {
        acct_store_destroy(&s->store);
        shm_destroy(s->seg);
        s->seg = NULL;
    }
}

static int stage_build(stage_t *s)
{
    memset(s, 0, sizeof(*s));

    divergent_config_t dcfg;
    memset(&dcfg, 0, sizeof(dcfg));
    dcfg.scenario = SCENARIO_BALANCED;
    dcfg.seed = SEED;
    dcfg.ref_price = REF_PRICE;
    dcfg.price_low = PRICE_LOW;
    dcfg.price_high = PRICE_HIGH;
    dcfg.start_ts = START_TS;
    dcfg.orders_per_market = LIQUIDITY;

    divergent_t *div = divergent_create(&dcfg);
    if (div == NULL) {
        return ERR_INVALID_ARG;
    }

    int32_t cap = LIQUIDITY + (WARMUP + MEASURED) * 2 + 64;

    cons_init(&s->cons);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        s->eng[m] =
            match_engine_create(divergent_ref_price(div, (market_t)m), cap);
        if (s->eng[m] == NULL) {
            divergent_destroy(div);
            stage_free(s);
            return ERR_POOL_EXHAUSTED;
        }
        if (cons_attach(&s->cons, (market_t)m, match_book(s->eng[m]), NULL) !=
            ERR_OK) {
            divergent_destroy(div);
            stage_free(s);
            return ERR_INVALID_ARG;
        }
        s->venues.eng[m] = s->eng[m];
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        synth_gen_t *gen = divergent_gen(div, (market_t)m);
        for (int32_t i = 0; i < LIQUIDITY; i++) {
            order_t       o;
            exec_result_t res;
            if (synth_next(gen, &o) != ERR_OK) {
                break;
            }
            o.market = (market_t)m;
            (void)match_limit(s->eng[m], &o, &res);
        }
    }
    divergent_destroy(div);

    s->map = omap_create(WARMUP + MEASURED + 8);
    if (s->map == NULL) {
        stage_free(s);
        return ERR_POOL_EXHAUSTED;
    }

    /* 원장. 계좌 하나면 된다 — 재려는 것은 계좌 수가 아니라 한 건의 비용이다. */
    const int32_t rec_count[SHM_REGION_COUNT] = {4, 4};
    const size_t  rec_size[SHM_REGION_COUNT] = {sizeof(account_t), 64};
    s->seg = shm_create(rec_count, rec_size);
    if (s->seg == NULL) {
        stage_free(s);
        return ERR_POOL_EXHAUSTED;
    }
    if (acct_store_init(&s->store, s->seg) != ERR_OK) {
        stage_free(s);
        return ERR_INVALID_ARG;
    }
    s->acct = acct_open(&s->store, ACCOUNT_NO);
    if (s->acct < 0) {
        stage_free(s);
        return s->acct;
    }
    if (acct_deposit(&s->store, s->acct, CASH) != ERR_OK) {
        stage_free(s);
        return ERR_INVALID_ARG;
    }

    vcfg_init(&s->vcfg);
    /* 증거금률 40%. 전액이면 2만 건을 버티려고 돈을 더 비현실적으로 넣어야 한다 */
    if (vcfg_add_symbol(&s->vcfg, SYMBOL, 4000, true) != ERR_OK) {
        stage_free(s);
        return ERR_INVALID_ARG;
    }
    s->vcfg.max_order_notional = 0; /* 한도를 보지 않는다 */

    return ERR_OK;
}

/* --- 한 건이 지나는 길 --- */

typedef struct {
    int64_t ns[STAGE_COUNT];
    bool    completed;   /* 일곱 단계를 다 지났는가 */
    int32_t stopped_at;  /* 못 지났으면 어느 단계에서 멈췄나 */
    int     stop_reason; /* 그때의 에러 코드 */
    qty_t   filled;
    int32_t legs; /* 이 논리 주문이 몇 개의 물리 주문으로 갈라졌나 */
} pass_t;

/*
 * 주문 한 건을 전 구간에 통과시키고 단계마다 걸린 시간을 담는다.
 *
 * 중간에 거부되면 그 뒤 단계는 0으로 남고 `completed`가 false다.
 * **거부된 건을 분위수에 섞지 않는다** — 짧은 경로가 p50을 끌어내린다.
 */
static void run_one(stage_t *s, journal_t *jrn, const exec_strategy_t *strat,
                    const msg_order_req_t *req, order_id_t logical_id, ts_t ts,
                    pass_t *out)
{
    memset(out, 0, sizeof(*out));
    out->stopped_at = -1;

    /* --- 1. 전문 해석 --- */
    uint8_t wire[MSG_ORDER_REQ_LEN];
    if (msg_encode_order_req(req, wire, sizeof(wire)) < 0) {
        out->stopped_at = 0;
        out->stop_reason = ERR_INVALID_ARG;
        return;
    }

    int64_t         t0 = now_ns();
    msg_order_req_t decoded;
    int             rc = msg_decode_order_req(wire, sizeof(wire), &decoded);
    out->ns[0] = now_ns() - t0;
    /*
     * **디코드는 성공하면 읽은 바이트 수를 돌려준다**(`msg.c`). 0이 아니다.
     * `!= ERR_OK`로 보면 성공한 건이 전부 거부로 잡힌다 — 실제로 그랬다.
     */
    if (rc < 0) {
        out->stopped_at = 0;
        out->stop_reason = rc;
        return;
    }

    /* --- 2. 원장 검증 --- */
    t0 = now_ns();
    validate_result_t vres;
    rc = validate_order(&s->store, &s->vcfg, &decoded, &vres);
    out->ns[1] = now_ns() - t0;
    if (rc != ERR_OK) {
        out->stopped_at = 1;
        out->stop_reason = rc;
        return;
    }

    /* --- 3. 저널 기록 --- */
    if (jrn != NULL) {
        t0 = now_ns();
        rc = journal_append(jrn, (uint8_t)MSG_ORDER_REQ, logical_id,
                            (int64_t)ts, wire, (uint32_t)sizeof(wire));
        out->ns[2] = now_ns() - t0;
        if (rc != ERR_OK) {
            out->stopped_at = 2;
            out->stop_reason = rc;
            return;
        }
    }

    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = logical_id;
    o.side = (side_t)decoded.side;
    o.type = (order_type_t)decoded.type;
    o.price = decoded.price;
    o.qty = decoded.qty;
    o.ts = ts;

    /* --- 4. SOR 계획 --- */
    exec_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.cons = &s->cons;
    ctx.ts = ts;

    t0 = now_ns();
    exec_plan_t plan;
    rc = routing_plan(strat, &ctx, &o, NULL, &plan);
    out->ns[3] = now_ns() - t0;
    if (rc != ERR_OK) {
        out->stopped_at = 3;
        out->stop_reason = rc;
        return;
    }

    /* --- 5. 물리 등록 --- */
    t0 = now_ns();
    order_id_t phys[PLAN_LEGS_MAX];
    rc = omap_register(s->map, &o, &plan, phys);
    out->ns[4] = now_ns() - t0;
    if (rc != ERR_OK) {
        out->stopped_at = 4;
        out->stop_reason = rc;
        return;
    }

    /*
     * --- 6. 매칭 + 7. 체결 반영 ---
     *
     * 다리마다 시장에 넣고 그 결과를 논리 주문에 합산한다. 두 단계를 다리
     * 수만큼 반복하므로 시간을 **더해서** 담는다 — 재려는 것은 한 논리
     * 주문의 비용이지 한 다리의 비용이 아니다.
     */
    for (int32_t i = 0; i < plan.leg_count; i++) {
        order_t leg = o;
        leg.id = phys[i];
        leg.market = plan.legs[i].market;
        leg.qty = plan.legs[i].qty;
        leg.price = plan.legs[i].limit_price;
        leg.type = plan.legs[i].type;

        t0 = now_ns();
        exec_result_t res;
        int           mrc = match_limit(s->eng[leg.market], &leg, &res);
        out->ns[5] += now_ns() - t0;

        t0 = now_ns();
        if (mrc == ERR_OK) {
            (void)omap_on_accept(s->map, phys[i]);
            for (int32_t f = 0; f < res.fill_count; f++) {
                (void)omap_on_fill(s->map, phys[i], res.fills[f].qty,
                                   res.fills[f].price);
                out->filled += res.fills[f].qty;
            }
        }
        out->ns[6] += now_ns() - t0;
    }

    out->legs = plan.leg_count;
    out->completed = true;
}

/* --- 한 판 --- */

typedef struct {
    bool    with_journal;
    int32_t completed;
    double  tps;
    double  wall_sec;
    int64_t p50[STAGE_COUNT];
    int64_t p95[STAGE_COUNT];
    int64_t p99[STAGE_COUNT];
    int64_t max[STAGE_COUNT];
    int64_t total_p50;
    int64_t total_p99;
    int64_t filled;

    /* 거부된 건. **어느 단계에서 몇 건이 떨어졌는지 숨기지 않는다.** */
    int32_t rejected[STAGE_COUNT];
    int     first_reason[STAGE_COUNT];
    int32_t rejected_total;

    /*
     * 다리 수 합계. **이 값이 측정 건수와 같으면 다리가 하나씩이라는 뜻**이고,
     * 그러면 "다리마다 반복"하는 경로는 이 워크로드에서 한 번도 돌지 않는다.
     * 변이 검사(P6)가 그것을 드러냈다 — 적어 두지 않으면 읽는 사람이
     * 이 숫자를 여러 시장에 나눠 보낸 주문의 비용으로 오해한다.
     */
    int64_t legs_total;
} round_t;

static int64_t g_samp[STAGE_COUNT][MEASURED];
static int64_t g_total[MEASURED];

static int run_round(bool with_journal, const char *jrn_path, round_t *out)
{
    stage_t s;
    int     rc = stage_build(&s);
    if (rc != ERR_OK) {
        return rc;
    }

    journal_t *jrn = NULL;
    if (with_journal) {
        remove(jrn_path);
        jrn = journal_create(jrn_path);
        if (jrn == NULL) {
            stage_free(&s);
            return ERR_IO;
        }
    }

    const exec_strategy_t *strat = &STRATEGY_BEST_PRICE;

    int32_t n = 0;
    taker_t tg;
    taker_init(&tg);

    memset(out, 0, sizeof(*out));
    out->with_journal = with_journal;

    int64_t t_start = 0;

    for (int32_t i = 0; i < WARMUP + MEASURED; i++) {
        msg_order_req_t req;
        taker_next(&tg, &req);

        if (i == WARMUP) {
            t_start = now_ns(); /* 예열은 재지 않는다 */
        }

        pass_t p;
        run_one(&s, jrn, strat, &req, (order_id_t)req.cl_ord_id, tg.ts, &p);

        if (i < WARMUP) {
            continue;
        }
        if (!p.completed) {
            if (p.stopped_at >= 0 && p.stopped_at < STAGE_COUNT) {
                if (out->rejected[p.stopped_at] == 0) {
                    out->first_reason[p.stopped_at] = p.stop_reason;
                }
                out->rejected[p.stopped_at]++;
            }
            out->rejected_total++;
            continue;
        }
        if (n >= MEASURED) {
            continue;
        }

        int64_t sum = 0;
        for (int32_t k = 0; k < STAGE_COUNT; k++) {
            g_samp[k][n] = p.ns[k];
            sum += p.ns[k];
        }
        g_total[n] = sum;
        out->filled += p.filled;
        out->legs_total += p.legs;
        n++;
    }

    int64_t t_end = now_ns();
    out->completed = n;

    /*
     * **0건으로 낸 보고서는 보고서가 아니다.**
     *
     * 처음 돌렸을 때 주문 가격이 호가 단위에 안 맞아 2만 건이 전부 원장에서
     * 거부됐다. 그런데도 프로그램은 성공으로 끝나고 "TPS 2300만"이라는 표를
     * 냈다 — 그것은 거부 루프의 속도였다. 숫자가 그럴듯하면 아무도 0건인 것을
     * 안 본다. 그래서 여기서 멈춘다.
     */
    if (n == 0) {
        for (int32_t k = 0; k < STAGE_COUNT; k++) {
            if (out->rejected[k] > 0) {
                fprintf(stderr, "  %d. %s에서 %d건 거부: %s\n", k + 1,
                        STAGE_NAME[k], out->rejected[k],
                        err_str(out->first_reason[k]));
            }
        }
        if (jrn != NULL) {
            journal_close(jrn);
            remove(jrn_path);
        }
        stage_free(&s);
        return ERR_INVALID_ARG;
    }
    out->wall_sec = (double)(t_end - t_start) / 1e9;
    out->tps = (out->wall_sec > 0) ? (double)MEASURED / out->wall_sec : 0;

    for (int32_t k = 0; k < STAGE_COUNT; k++) {
        qsort(g_samp[k], (size_t)n, sizeof(int64_t), cmp_i64);
        out->p50[k] = percentile(g_samp[k], (size_t)n, 0.50);
        out->p95[k] = percentile(g_samp[k], (size_t)n, 0.95);
        out->p99[k] = percentile(g_samp[k], (size_t)n, 0.99);
        out->max[k] = (n > 0) ? g_samp[k][n - 1] : 0;
    }
    qsort(g_total, (size_t)n, sizeof(int64_t), cmp_i64);
    out->total_p50 = percentile(g_total, (size_t)n, 0.50);
    out->total_p99 = percentile(g_total, (size_t)n, 0.99);

    if (jrn != NULL) {
        journal_close(jrn);
        remove(jrn_path);
    }
    stage_free(&s);
    return ERR_OK;
}

/* --- 보고 --- */

/* 가장 비싼 단계. **이름으로 지목한다.** */
static int32_t worst_stage(const round_t *r)
{
    int32_t best = 0;
    for (int32_t k = 1; k < STAGE_COUNT; k++) {
        if (r->p50[k] > r->p50[best]) {
            best = k;
        }
    }
    return best;
}

static void print_round(FILE *f, const round_t *r)
{
    fprintf(f, "\n### 저널 %s\n\n", r->with_journal ? "켬" : "끔");
    fprintf(f, "| 단계 | p50 | p95 | p99 | 최대 |\n");
    fprintf(f, "|---|---:|---:|---:|---:|\n");
    for (int32_t k = 0; k < STAGE_COUNT; k++) {
        if (!r->with_journal && k == STAGE_JOURNAL) {
            fprintf(f, "| %d. %s | — | — | — | — |\n", k + 1, STAGE_NAME[k]);
            continue;
        }
        fprintf(f, "| %d. %s | %lld | %lld | %lld | %lld |\n", k + 1,
                STAGE_NAME[k], (long long)r->p50[k], (long long)r->p95[k],
                (long long)r->p99[k], (long long)r->max[k]);
    }
    fprintf(f, "| **전 구간 합** | **%lld** | | **%lld** | |\n",
            (long long)r->total_p50, (long long)r->total_p99);
    fprintf(f, "\n(단위 ns)\n");
    /*
     * **분위수는 더해지지 않는다.** 단계별 p50을 더한 값과 "전 구간 합"의
     * p50은 다르다 — 앞의 것은 서로 다른 주문의 중간값을 더한 것이고, 뒤의
     * 것은 주문마다 일곱 단계를 더한 뒤 그 분포의 중간값을 본 것이다.
     * 적어 두지 않으면 산수가 틀린 표로 읽힌다.
     */
    fprintf(f,
            "\n> 단계별 p50을 더해도 \"전 구간 합\"이 되지 않는다. **분위수는 "
            "더해지지 않는다** — 전 구간 합은 주문마다 일곱 단계를 더한 뒤 그 "
            "분포에서 뽑은 값이다.\n");

    int32_t w = worst_stage(r);
    fprintf(f, "\n- 측정된 건수: %d (거부된 건은 분위수에서 뺐다)\n",
            r->completed);
    if (r->rejected_total > 0) {
        fprintf(f, "- 거부된 건수: %d —", r->rejected_total);
        for (int32_t k = 0; k < STAGE_COUNT; k++) {
            if (r->rejected[k] > 0) {
                fprintf(f, " %s %d건(%s)", STAGE_NAME[k], r->rejected[k],
                        err_str(r->first_reason[k]));
            }
        }
        fprintf(f, "\n");
    } else {
        fprintf(f, "- 거부된 건수: 0\n");
    }
    fprintf(f, "- 소요 시간: %.3f초, **TPS %.0f**\n", r->wall_sec, r->tps);
    fprintf(f, "- 체결 수량: %lld주\n", (long long)r->filled);
    {
        double avg_legs = (r->completed > 0)
                              ? (double)r->legs_total / (double)r->completed
                              : 0.0;
        fprintf(f, "- 논리 주문당 물리 다리: 평균 %.2f개", avg_legs);
        if (r->legs_total == (int64_t)r->completed) {
            fprintf(f,
                    " — **전부 한 다리다.** BEST_PRICE는 유리한 한 시장을 "
                    "고르므로 쪼개지 않는다. 그래서 위의 매칭·체결 반영 "
                    "비용은 **한 시장에 보낸 주문의 비용**이다. 두 시장으로 "
                    "쪼개는 전략(SPLIT/SWEEP)은 이 두 단계가 다리 수만큼 "
                    "늘어난다\n");
        } else {
            fprintf(f, "\n");
        }
    }
    fprintf(f, "- **가장 비싼 단계: %d. %s** (p50 %lld ns, 전 구간의 %.1f%%)\n",
            w + 1, STAGE_NAME[w], (long long)r->p50[w],
            (r->total_p50 > 0)
                ? 100.0 * (double)r->p50[w] / (double)r->total_p50
                : 0.0);
}

int main(int argc, char **argv)
{
    /* 날짜는 인자로 받는다 — 하네스가 시스템 시각을 읽으면 결과가 흔들린다. */
    const char *date = (argc > 1) ? argv[1] : "unknown";
    const char *jrn_path = (argc > 2) ? argv[2] : "/tmp/bench_pipeline.jrn";
    const char *out_path = (argc > 3) ? argv[3] : NULL;
    /* 저널이 놓인 저장 매체. 이 결과에서 가장 크게 작용하는 조건이다. */
    const char *media = (argc > 4) ? argv[4] : "unknown";

    round_t off, on;
    int     rc = run_round(false, jrn_path, &off);
    if (rc != ERR_OK) {
        fprintf(stderr, "저널 끈 판 실패: %s\n", err_str(rc));
        return 1;
    }
    rc = run_round(true, jrn_path, &on);
    if (rc != ERR_OK) {
        fprintf(stderr, "저널 켠 판 실패: %s\n", err_str(rc));
        return 1;
    }

    FILE *f = stdout;
    if (out_path != NULL) {
        f = fopen(out_path, "w");
        if (f == NULL) {
            fprintf(stderr, "결과 파일을 못 엽니다: %s\n", out_path);
            return 1;
        }
    }

    fprintf(f, "# 전 구간 성능 측정 (T5-05)\n\n");
    fprintf(f,
            "T1-20은 매칭 엔진만 쟀다. 그 숫자는 그 조각의 것이다.\n"
            "여기서는 주문 한 건이 실제로 지나는 일곱 단계를 나눠 잰다.\n\n");
    fprintf(f, "## 측정 조건\n\n");
    char cpu[256];
    cpu_name(cpu, sizeof(cpu));

    fprintf(f, "- 날짜: %s\n", date);
    fprintf(f, "- CPU: %s\n", cpu);
    fprintf(f, "- 빌드: Release, 단일 스레드, 단일 프로세스\n");
    fprintf(f, "- 예열 %d건(측정 제외), 측정 %d건\n", WARMUP, MEASURED);
    fprintf(f, "- 유동성: 시장당 %d건, 시나리오 BALANCED, 시드 %u (고정)\n",
            LIQUIDITY, SEED);
    fprintf(f,
            "- 전략: BEST_PRICE. 주문은 매수 지정가, 기준가 %d원 +%d원 "
            "(호가 단위에 맞춘 값), 10~50주\n",
            REF_PRICE, AGGRESSION);
    fprintf(f, "- 저널 경로: `%s` (저장 매체: %s)\n", jrn_path, media);
    fprintf(f,
            "- 지연은 각 단계 호출 전후의 `CLOCK_MONOTONIC` 차이다. "
            "**측정 자체의 비용이 포함되어 있다** (단계마다 `clock_gettime` "
            "두 번). 수십 ns짜리 단계에서는 그 비용이 무시할 수 없다\n");
    fprintf(f,
            "- 계층을 별도 프로세스로 띄우지 않고 **한 프로세스 안에서** "
            "차례로 부른다. 그래서 이 숫자에 전문 송수신과 프로세스 경계 "
            "비용은 **빠져 있다**\n");

    print_round(f, &off);
    print_round(f, &on);

    fprintf(f, "\n## 저널의 값\n\n");
    fprintf(f, "| | 저널 끔 | 저널 켬 | 배수 |\n");
    fprintf(f, "|---|---:|---:|---:|\n");
    fprintf(f, "| 전 구간 p50 | %lld ns | %lld ns | %.1f배 |\n",
            (long long)off.total_p50, (long long)on.total_p50,
            (off.total_p50 > 0)
                ? (double)on.total_p50 / (double)off.total_p50
                : 0.0);
    fprintf(f, "| TPS | %.0f | %.0f | %.3f배 |\n", off.tps, on.tps,
            (off.tps > 0) ? on.tps / off.tps : 0.0);

    /*
     * **숫자만 적으면 보고서가 아니다.** 무엇을 뜻하는지까지 적는다.
     * 이 문단은 위의 측정값에서 계산된다 — 손으로 쓴 결론이 아니다.
     */
    int32_t w_off = worst_stage(&off);
    int32_t w_on = worst_stage(&on);
    double  jrn_share =
        (on.total_p50 > 0)
             ? 100.0 * (double)on.p50[STAGE_JOURNAL] / (double)on.total_p50
             : 0.0;

    fprintf(f, "\n## 읽는 법\n\n");
    fprintf(f,
            "1. **저널을 빼면 전 구간이 %lld ns다.** 그중 가장 비싼 것은 "
            "%s(%lld ns)이고, 매칭은 %lld ns다. T1-20이 잰 매칭만의 비용이 "
            "전 구간의 일부일 뿐임을 확인한 셈이다.\n",
            (long long)off.total_p50, STAGE_NAME[w_off],
            (long long)off.p50[w_off], (long long)off.p50[5]);
    fprintf(f,
            "2. **저널을 켜면 %s이 전 구간의 %.1f%%를 먹는다.** 전 구간 p50이 "
            "%lld ns에서 %lld ns로 %.0f배가 되고, 처리량은 %.0f TPS에서 "
            "%.0f TPS로 떨어진다. 매 레코드 `fsync`(T5-01)의 값이 이것이다.\n",
            STAGE_NAME[w_on], jrn_share, (long long)off.total_p50,
            (long long)on.total_p50,
            (off.total_p50 > 0)
                ? (double)on.total_p50 / (double)off.total_p50
                : 0.0,
            off.tps, on.tps);
    fprintf(f,
            "3. **`fsync`의 비용은 자기 단계에만 머물지 않는다.** 저널을 켜자 "
            "매칭도 %lld ns에서 %lld ns로 느려졌다. 저널을 쓰는 동안 프로세스가 "
            "내려갔다 올라오면서 캐시가 식기 때문이다 — 단계별 표만 보고 "
            "\"저널만 빼면 나머지는 그대로\"라고 읽으면 안 된다.\n",
            (long long)off.p50[5], (long long)on.p50[5]);
    fprintf(f,
            "4. 이 숫자는 저널이 놓인 저장 매체(%s)의 것이다. **다른 장비에서는 "
            "다시 재야 한다.** 다만 바뀌는 것은 배수이지 결론이 아니다 — "
            "동기 `fsync`가 있는 한 이 계층이 전 구간을 지배한다.\n",
            media);

    fprintf(f, "\n## 그래서 무엇을 할 것인가\n\n");
    fprintf(f,
            "**지금은 아무것도 바꾸지 않는다.** T5-01이 매번 `fsync`를 고른 "
            "이유는 성능을 몰라서가 아니라 \"잃으면 안 되는 기록\"이기 "
            "때문이었고, 그 이유는 이 표를 보고도 그대로다. %.0f TPS는 이 "
            "프로젝트의 목표(집행 전략 비교)에 모자라지 않는다.\n\n",
            on.tps);
    fprintf(f,
            "바꿔야 할 때가 오면 선택지는 셋이고, 셋 다 **내구성을 깎지 않고** "
            "지연을 옮기는 방법이다.\n\n"
            "- 그룹 커밋 — 여러 주문을 모아 `fsync` 한 번. 묶은 만큼 응답이 "
            "늦어지지만 잃지는 않는다\n"
            "- 저널 전용 스레드 — 집행 경로에서 `fsync`를 떼어 내고 응답 전에 "
            "완료를 기다린다. 지연은 남고 처리량만 는다\n"
            "- 더 빠른 장치 — 배터리 백업 캐시가 있는 저장 매체면 `fsync`가 "
            "메모리 쓰기에 가까워진다\n\n"
            "셋 다 이 태스크의 범위 밖이다. **재기 전에 고르지 않는다.**\n");

    if (out_path != NULL) {
        fclose(f);
        fprintf(stderr, "결과를 %s에 적었습니다\n", out_path);
    }
    return 0;
}
