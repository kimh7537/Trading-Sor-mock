/*
 * T1-20 벤치마크 하네스.
 *
 * 재는 것은 둘이다.
 *   TPS       — 초당 주문 처리 건수
 *   지연 분포 — 주문 접수부터 체결 응답까지, p50 / p95 / p99
 *
 * 평균이 아니라 분위수를 보는 이유는, 매칭 엔진의 비용이 균일하지 않기 때문이다.
 * 대부분의 주문은 호가창에 등록만 되고 끝나지만, 여러 레벨을 가로지르며 체결되는
 * 주문은 그보다 수십 배 걸린다. 평균은 그 꼬리를 감춘다.
 *
 * 여기서만 시스템 시각을 읽는다. 엔진은 읽지 않는다 — 벤치는 엔진 밖이다.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "divergent.h"
#include "errors.h"
#include "market_rules.h"
#include "match.h"

#define WARMUP 20000
#define MEASURED 200000
#define ENGINE_CAP 262144
#define REF 10000
#define START_TS TOD_NS(9, 1, 0)

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/* 정렬된 배열에서의 분위수. 보간하지 않는다 — 표본이 40만 개면 의미가 없다. */
static int64_t percentile(const int64_t *sorted, size_t n, double p)
{
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

typedef struct {
    double  tps;
    int64_t p50;
    int64_t p95;
    int64_t p99;
    int64_t max;
    int64_t total_ns;
    int64_t filled_qty;
    int64_t notional;
    int64_t rejected;
    size_t  samples;
} bench_result_t;

static bench_result_t run_bench(scenario_t scenario, uint64_t seed)
{
    divergent_config_t dcfg = {0};
    dcfg.scenario = scenario;
    dcfg.seed = seed;
    dcfg.ref_price = REF;
    dcfg.price_low = 7000;
    dcfg.price_high = 13000;
    dcfg.start_ts = START_TS;
    dcfg.orders_per_market = WARMUP + MEASURED;

    divergent_t *div = divergent_create(&dcfg);
    assert(div != NULL);

    match_engine_t *eng[MARKET_COUNT];
    const market_rules_t *rules[MARKET_COUNT] = {
        [MARKET_KRX] = &KRX_RULES,
        [MARKET_NXT] = &NXT_RULES,
    };
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        eng[m] =
            match_engine_create(divergent_ref_price(div, (market_t)m), ENGINE_CAP);
        assert(eng[m] != NULL);
        match_set_rules(eng[m], rules[m]);
        /* 이벤트 싱크는 걸지 않는다. 소비자 비용까지 재면 엔진 비용이 가려진다. */
    }

    /*
     * 예열. 빈 호가창에 넣는 주문은 체결이 없어 유난히 싸다. 그 구간을 측정에
     * 넣으면 TPS가 부풀고 분위수가 왜곡된다.
     */
    for (int32_t i = 0; i < WARMUP; i++) {
        for (int32_t m = 0; m < MARKET_COUNT; m++) {
            order_t o;
            exec_result_t res;
            (void)synth_next(divergent_gen(div, (market_t)m), &o);
            (void)match_limit(eng[m], &o, &res);
        }
    }

    size_t n = (size_t)MEASURED * MARKET_COUNT;
    int64_t *lat = malloc(n * sizeof(*lat));
    assert(lat != NULL);

    bench_result_t r = {0};
    size_t k = 0;
    int64_t begin = now_ns();

    for (int32_t i = 0; i < MEASURED; i++) {
        for (int32_t m = 0; m < MARKET_COUNT; m++) {
            order_t o;
            exec_result_t res;
            (void)synth_next(divergent_gen(div, (market_t)m), &o);

            int64_t t0 = now_ns();
            int rc = match_limit(eng[m], &o, &res);
            int64_t t1 = now_ns();

            lat[k++] = t1 - t0;
            if (rc == ERR_OK) {
                r.filled_qty += res.filled_qty;
                r.notional += res.notional;
            } else {
                r.rejected++;
            }
        }
    }

    r.total_ns = now_ns() - begin;
    r.samples = k;
    r.tps = (double)k * 1.0e9 / (double)r.total_ns;

    qsort(lat, k, sizeof(*lat), cmp_i64);
    r.p50 = percentile(lat, k, 0.50);
    r.p95 = percentile(lat, k, 0.95);
    r.p99 = percentile(lat, k, 0.99);
    r.max = lat[k - 1];

    free(lat);
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        match_engine_destroy(eng[m]);
    }
    divergent_destroy(div);

    return r;
}

/* 측정 조건을 결과와 함께 남긴다. 조건 없는 숫자는 나중에 비교할 수 없다. */
static void write_hardware(FILE *f)
{
    FILE *cpu = fopen("/proc/cpuinfo", "r");
    char model[256] = "알 수 없음";

    if (cpu != NULL) {
        char line[512];
        while (fgets(line, sizeof(line), cpu) != NULL) {
            if (strncmp(line, "model name", 10) == 0) {
                char *colon = strchr(line, ':');
                if (colon != NULL) {
                    char *v = colon + 1;
                    while (*v == ' ') {
                        v++;
                    }
                    size_t len = strlen(v);
                    while (len > 0 &&
                           (v[len - 1] == '\n' || v[len - 1] == ' ')) {
                        v[--len] = '\0';
                    }
                    snprintf(model, sizeof(model), "%s", v);
                }
                break;
            }
        }
        fclose(cpu);
    }
    fprintf(f, "- CPU: %s\n", model);
}

static void report(FILE *f, const char *name, const bench_result_t *r)
{
    fprintf(f, "### %s\n\n", name);
    fprintf(f, "| 지표 | 값 |\n|---|---|\n");
    fprintf(f, "| 처리 건수 | %zu |\n", r->samples);
    fprintf(f, "| 소요 시간 | %.3f초 |\n", (double)r->total_ns / 1.0e9);
    fprintf(f, "| TPS | %.0f |\n", r->tps);
    fprintf(f, "| 지연 p50 | %lld ns |\n", (long long)r->p50);
    fprintf(f, "| 지연 p95 | %lld ns |\n", (long long)r->p95);
    fprintf(f, "| 지연 p99 | %lld ns |\n", (long long)r->p99);
    fprintf(f, "| 지연 최대 | %lld ns |\n", (long long)r->max);
    fprintf(f, "| 체결 수량 | %lld주 |\n", (long long)r->filled_qty);
    if (r->filled_qty > 0) {
        fprintf(f, "| 평균 체결 단가 | %lld원 |\n",
                (long long)(r->notional / r->filled_qty));
    }
    fprintf(f, "| 거부 | %lld건 |\n\n", (long long)r->rejected);
}

int main(int argc, char **argv)
{
    const char *out_path = (argc > 1) ? argv[1] : NULL;

    const struct {
        const char *name;
        scenario_t  scenario;
    } CASES[] = {
        {"BALANCED", SCENARIO_BALANCED},
        {"KRX_THIN", SCENARIO_KRX_THIN},
        {"CROSSED", SCENARIO_CROSSED},
    };
    const size_t case_count = sizeof(CASES) / sizeof(CASES[0]);

    bench_result_t results[3];
    for (size_t i = 0; i < case_count; i++) {
        printf("%s 측정 중...\n", CASES[i].name);
        results[i] = run_bench(CASES[i].scenario, 20260915);
    }

    FILE *f = stdout;
    if (out_path != NULL) {
        f = fopen(out_path, "w");
        if (f == NULL) {
            fprintf(stderr, "결과 파일을 열 수 없다: %s\n", out_path);
            return 1;
        }
    }

    fprintf(f, "# 매칭 엔진 벤치마크\n\n");
    fprintf(f, "## 측정 조건\n\n");
    write_hardware(f);
    fprintf(f, "- 빌드: Release (`-O3`), 단일 스레드\n");
    fprintf(f, "- 예열: 시장당 %d건 (측정에서 제외)\n", WARMUP);
    fprintf(f, "- 측정: 시장당 %d건, 두 시장 합계 %d건\n", MEASURED,
            MEASURED * MARKET_COUNT);
    fprintf(f, "- 주문 유형: 지정가. 이벤트 싱크 없음\n");
    fprintf(f, "- 시드: 20260915 (고정. 같은 시드는 같은 주문 시퀀스)\n");
    fprintf(f, "- 지연은 `match_limit()` 호출 전후의 `CLOCK_MONOTONIC` 차이다. "
               "측정 자체의 비용이 포함되어 있다\n\n");

    fprintf(f, "## 결과\n\n");
    for (size_t i = 0; i < case_count; i++) {
        report(f, CASES[i].name, &results[i]);
    }

    fprintf(f, "## 읽는 법\n\n");
    fprintf(f, "- p50과 p99의 차이가 곧 \"체결이 붙은 주문\"의 비용이다. "
               "대부분의 주문은 호가창에 등록만 되고 끝난다\n");
    fprintf(f, "- 얇은 시장(KRX_THIN)은 호가가 성기어 체결이 덜 일어나므로 "
               "지연이 오히려 낮게 나올 수 있다. 빠른 것이 아니라 일을 덜 한 것이다\n");
    fprintf(f, "- 평균 체결 단가는 시나리오 간 비교용이다. "
               "절대값 자체에는 의미가 없다\n");

    if (out_path != NULL) {
        fclose(f);
        printf("결과를 %s에 썼다\n", out_path);
    }

    return 0;
}
