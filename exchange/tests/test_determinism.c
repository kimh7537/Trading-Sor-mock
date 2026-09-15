/*
 * T1-19 결정성 검증.
 *
 * 이 프로젝트의 결론은 "전략 A와 B의 평균 체결 단가가 이만큼 달랐다"이다.
 * 그 문장이 성립하려면 **두 실행의 차이가 전략 때문이라는 것 말고는 없어야** 한다.
 * 같은 입력에 같은 출력이 나오지 않으면 측정한 차이가 무엇 때문인지 말할 수 없다.
 *
 * 그래서 파이프라인 전체(유동성 생성 -> 두 시장 엔진 -> 이벤트 스트림)를 두 번
 * 돌리고 이벤트를 **바이트 단위로** 대조한다. 요약값(건수, 해시)만 비교하면
 * 어긋난 곳을 못 찾을 뿐 아니라 충돌 가능성도 남는다.
 *
 * 시드가 다르면 결과가 달라지는 것도 함께 본다. 그게 아니면 위 비교가
 * "아무것도 안 하는 코드"를 통과시키는 것과 구별되지 않는다.
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "divergent.h"
#include "errors.h"
#include "market_rules.h"
#include "match.h"

/* 시장당 5만 건 = 총 10만 건 (완료 조건의 최소 규모) */
#define ORDERS_PER_MARKET 50000
#define ENGINE_CAP 65536
#define REF 10000

/*
 * 논리 시각 시작점. 두 시장이 모두 정규 거래 중인 구간이어야 한다 —
 * NXT 메인마켓은 09:00:30부터, KRX 정규장은 09:00부터다.
 * 도착률 100/초로 5만 건이면 약 500초 흐르므로 09:09 언저리에 끝난다.
 */
#define START_TS TOD_NS(9, 1, 0)

/* --- 이벤트 기록 --- */

typedef struct {
    order_event_t *ev;
    size_t         n;
    size_t         cap;

    /* 재생 모드: 기록 대신 기존 기록과 대조한다 */
    const order_event_t *expect;
    size_t               expect_n;
    bool                 replay;
    size_t               mismatch_at; /* (size_t)-1이면 어긋난 곳 없음 */
} evlog_t;

static void log_init(evlog_t *log)
{
    memset(log, 0, sizeof(*log));
    log->mismatch_at = (size_t)-1;
}

static void log_free(evlog_t *log)
{
    free(log->ev);
    log->ev = NULL;
}

static void on_event(const order_event_t *e, void *ctx)
{
    evlog_t *log = ctx;

    if (log->replay) {
        /*
         * 두 번째 실행은 기록을 따로 들지 않는다. 들어오는 이벤트를 그 자리에서
         * 첫 실행의 것과 바이트 비교한다 — 10만 건짜리 배열을 두 벌 들 이유가 없다.
         */
        if (log->n < log->expect_n &&
            memcmp(e, &log->expect[log->n], sizeof(*e)) != 0 &&
            log->mismatch_at == (size_t)-1) {
            log->mismatch_at = log->n;
        }
        log->n++;
        return;
    }

    if (log->n == log->cap) {
        size_t cap = (log->cap == 0) ? 65536 : log->cap * 2;
        order_event_t *grown = realloc(log->ev, cap * sizeof(*grown));
        assert(grown != NULL);
        log->ev = grown;
        log->cap = cap;
    }
    log->ev[log->n++] = *e;
}

/* --- 시나리오 실행 --- */

typedef struct {
    int64_t notional;
    qty_t   filled;
    int64_t rejected;
} run_stat_t;

/*
 * 한 번의 실행. 두 시장에 유동성을 흘려 넣으면서 중간중간 취소·정정을 섞는다.
 * 이벤트는 sink로 나가고, 반환값은 두 시장 합산 체결 금액·수량이다.
 */
static run_stat_t run_once(uint64_t seed, scenario_t scenario, evlog_t *log)
{
    divergent_config_t dcfg = {0};
    dcfg.scenario = scenario;
    dcfg.seed = seed;
    dcfg.ref_price = REF;
    dcfg.price_low = 7000;
    dcfg.price_high = 13000;
    dcfg.start_ts = START_TS;
    dcfg.orders_per_market = ORDERS_PER_MARKET;

    divergent_t *div = divergent_create(&dcfg);
    assert(div != NULL);

    match_engine_t *eng[MARKET_COUNT];
    const market_rules_t *rules[MARKET_COUNT] = {
        [MARKET_KRX] = &KRX_RULES,
        [MARKET_NXT] = &NXT_RULES,
    };
    event_sink_t sink = {on_event, log};

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        eng[m] =
            match_engine_create(divergent_ref_price(div, (market_t)m), ENGINE_CAP);
        assert(eng[m] != NULL);
        match_set_rules(eng[m], rules[m]);
        match_set_sink(eng[m], &sink);
    }

    run_stat_t stat = {0};
    order_id_t recent[MARKET_COUNT] = {0};

    for (int32_t i = 0; i < ORDERS_PER_MARKET; i++) {
        /*
         * 시장을 번갈아 돈다. 순서 자체가 결정적이면 되고, 실제 시각 순서를
         * 흉내 낼 필요는 없다 — 각 시장의 호가창은 서로 독립이다.
         */
        for (int32_t m = 0; m < MARKET_COUNT; m++) {
            order_t o;
            exec_result_t res;
            assert(synth_next(divergent_gen(div, (market_t)m), &o) == ERR_OK);

            int rc = match_limit(eng[m], &o, &res);
            if (rc == ERR_OK) {
                stat.notional += res.notional;
                stat.filled += res.filled_qty;
                if (res.resting) {
                    recent[m] = o.id;
                }
            } else {
                stat.rejected++;
            }

            /* 64건마다 최근 등록 주문을 하나 정정하거나 취소한다 */
            if ((i & 63) == 63 && recent[m] != ORDER_ID_INVALID) {
                exec_result_t r2;
                if ((i & 127) == 127) {
                    (void)match_cancel(eng[m], recent[m], o.ts + 1, &r2);
                } else {
                    (void)match_modify(eng[m], recent[m], o.price, 5, o.ts + 1,
                                       &r2);
                }
                recent[m] = ORDER_ID_INVALID;
            }
        }
    }

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(eng[m]);
    }
    divergent_destroy(div);

    return stat;
}

/* 같은 입력 -> 이벤트 스트림이 바이트 단위로 일치 */
static void test_identical_runs(void)
{
    evlog_t first;
    log_init(&first);
    run_stat_t s1 = run_once(20260915, SCENARIO_CROSSED, &first);

    /* 아무 일도 안 일어났으면 비교가 무의미하다 */
    assert(first.n > 100000);
    assert(s1.filled > 0);

    evlog_t second;
    log_init(&second);
    second.replay = true;
    second.expect = first.ev;
    second.expect_n = first.n;
    run_stat_t s2 = run_once(20260915, SCENARIO_CROSSED, &second);

    assert(second.mismatch_at == (size_t)-1);
    assert(second.n == first.n);

    /* 집계도 같아야 한다 */
    assert(s1.notional == s2.notional);
    assert(s1.filled == s2.filled);
    assert(s1.rejected == s2.rejected);

    printf("이벤트 %zu건, 체결 %d주, 체결금액 %lld원\n", first.n, s1.filled,
           (long long)s1.notional);

    log_free(&first);
    log_free(&second);
}

/* 시드가 다르면 결과가 달라진다 — 위 비교가 무의미하지 않다는 확인 */
static void test_different_seed_differs(void)
{
    evlog_t a;
    log_init(&a);
    run_stat_t sa = run_once(20260915, SCENARIO_CROSSED, &a);

    evlog_t b;
    log_init(&b);
    b.replay = true;
    b.expect = a.ev;
    b.expect_n = a.n;
    run_stat_t sb = run_once(20260916, SCENARIO_CROSSED, &b);

    /* 건수가 같더라도 내용이 어딘가 달라야 한다 */
    bool differs = (b.mismatch_at != (size_t)-1) || (b.n != a.n) ||
                   (sa.notional != sb.notional) || (sa.filled != sb.filled);
    assert(differs);

    log_free(&a);
    log_free(&b);
}

/* 시나리오가 다르면 체결 결과가 달라진다 — 측정 대상이 실제로 움직인다 */
static void test_scenario_changes_outcome(void)
{
    evlog_t balanced;
    log_init(&balanced);
    run_stat_t sb = run_once(777, SCENARIO_BALANCED, &balanced);

    evlog_t thin;
    log_init(&thin);
    run_stat_t st = run_once(777, SCENARIO_KRX_THIN, &thin);

    assert(sb.filled > 0 && st.filled > 0);
    /* 유동성이 다르면 체결량이 같을 수 없다 */
    assert(sb.filled != st.filled);

    log_free(&balanced);
    log_free(&thin);
}

int main(void)
{
    clock_t begin = clock();

    test_identical_runs();
    test_different_seed_differs();
    test_scenario_changes_outcome();

    double elapsed = (double)(clock() - begin) / CLOCKS_PER_SEC;
    printf("결정성 검증 %.1f초\n", elapsed);

    /*
     * CI에 넣을 수 있어야 한다는 완료 조건이 있다. 여기서 넘으면 규모를 줄이거나
     * 실행 횟수를 줄여야 한다 — 통과했다고 그냥 넘어가지 않도록 못 박아 둔다.
     */
    assert(elapsed < 25.0);

    return 0;
}
