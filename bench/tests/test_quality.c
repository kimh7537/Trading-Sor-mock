/*
 * T5-08 집행 품질 리포트.
 *
 * 전략의 우열은 여기서 판정하지 않는다 — 그건 리포트의 내용이다.
 * 여기서는 **리포트를 믿을 수 있는 조건**을 본다.
 *  1. 분포(중앙값·최소·최대)가 손으로 센 값과 같다. 짝수 개는 낮은 쪽
 *  2. 판정 규칙 — 한 번이라도 비기거나 지면 항상 우위가 아니다
 *  3. 시드 N개 집계가 compare_run() N번과 칸마다 같다 (시드가 1씩 나아간다)
 *  4. 같은 인자 -> 바이트까지 같은 파일
 *  5. 시드 개수 범위, 시드 넘침을 거절한다
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "quality.h"

static void test_dist(void)
{
    quality_dist_t d;

    int32_t odd[] = {5, -1, 3};
    assert(quality_dist(odd, 3, &d) == ERR_OK);
    assert(d.p50 == 3 && d.min == -1 && d.max == 5);

    int32_t even[] = {4, 1, 3, 2};
    assert(quality_dist(even, 4, &d) == ERR_OK);
    assert(d.p50 == 2 && d.min == 1 && d.max == 4); /* 2와 3 중 낮은 쪽 */

    int32_t one[] = {-7};
    assert(quality_dist(one, 1, &d) == ERR_OK);
    assert(d.p50 == -7 && d.min == -7 && d.max == -7);

    assert(quality_dist(one, 0, &d) == ERR_INVALID_ARG);
    assert(quality_dist(NULL, 1, &d) == ERR_NULL_PTR);
}

static void test_verdict(void)
{
    assert(quality_verdict_of(5, 0, 0) == QUALITY_ALWAYS_BETTER);
    assert(quality_verdict_of(0, 5, 0) == QUALITY_ALWAYS_WORSE);
    assert(quality_verdict_of(0, 0, 5) == QUALITY_NO_DIFF);
    assert(quality_verdict_of(4, 0, 1) == QUALITY_MIXED); /* 한 번 비김 */
    assert(quality_verdict_of(0, 4, 1) == QUALITY_MIXED);
    assert(quality_verdict_of(3, 2, 0) == QUALITY_MIXED);
}

static int32_t min3(int32_t a, int32_t b, int32_t c)
{
    int32_t m = a < b ? a : b;
    return m < c ? m : c;
}

static int32_t max3(int32_t a, int32_t b, int32_t c)
{
    int32_t m = a > b ? a : b;
    return m > c ? m : c;
}

/* 셋의 중앙값 = 합 - 최소 - 최대. quality_dist를 쓰지 않고 따로 센다. */
static void check_dist3(const quality_dist_t *d, int32_t a, int32_t b,
                        int32_t c)
{
    assert(d->min == min3(a, b, c));
    assert(d->max == max3(a, b, c));
    assert(d->p50 == a + b + c - min3(a, b, c) - max3(a, b, c));
}

static void test_matches_compare_run(void)
{
    compare_config_t base = COMPARE_DEFAULT;
    quality_report_t rep;
    assert(quality_run(&base, 3, &rep) == ERR_OK);
    assert(rep.seed_count == 3 && rep.base.seed == base.seed);

    compare_result_t r[3];
    for (int32_t i = 0; i < 3; i++) {
        compare_config_t c = base;
        c.seed = base.seed + (uint64_t)i;
        assert(compare_run(&c, &r[i]) == ERR_OK);
    }

    bool spread = false; /* 시드가 실제로 달라졌다면 어딘가 min != max */
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            const quality_cell_t *q = &rep.cell[s][k];
            const compare_row_t  *a = &r[0].row[s][k];
            const compare_row_t  *b = &r[1].row[s][k];
            const compare_row_t  *c = &r[2].row[s][k];

            assert(q->scenario == (scenario_t)s);
            assert(strcmp(q->strategy, a->strategy) == 0);
            check_dist3(&q->vs_krx_only_bp, a->vs_krx_only_bp,
                        b->vs_krx_only_bp, c->vs_krx_only_bp);
            check_dist3(&q->slippage_bp, a->slippage_bp, b->slippage_bp,
                        c->slippage_bp);
            check_dist3(&q->fill_rate_bp, a->fill_rate_bp, b->fill_rate_bp,
                        c->fill_rate_bp);

            int32_t w = (a->vs_krx_only_bp > 0) + (b->vs_krx_only_bp > 0) +
                        (c->vs_krx_only_bp > 0);
            int32_t l = (a->vs_krx_only_bp < 0) + (b->vs_krx_only_bp < 0) +
                        (c->vs_krx_only_bp < 0);
            assert(q->wins == w && q->losses == l && q->ties == 3 - w - l);
            assert(q->verdict == quality_verdict_of(w, l, 3 - w - l));

            if (q->slippage_bp.min != q->slippage_bp.max) {
                spread = true;
            }
        }
        /* 기준선은 자기 자신과 비교하므로 언제나 차이 없음이다. */
        assert(rep.cell[s][0].verdict == QUALITY_NO_DIFF);
    }
    assert(spread);
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static void test_same_args_same_bytes(void)
{
    quality_report_t a;
    quality_report_t b;
    assert(quality_run(NULL, 2, &a) == ERR_OK);
    assert(quality_run(NULL, 2, &b) == ERR_OK);
    assert(quality_write_md(&a, "2026-09-16", "test_quality_a.md") == ERR_OK);
    assert(quality_write_md(&b, "2026-09-16", "test_quality_b.md") == ERR_OK);

    long n = file_size("test_quality_a.md");
    assert(n > 0 && n == file_size("test_quality_b.md"));

    static char ba[65536];
    static char bb[65536];
    assert(n < (long)sizeof(ba));
    FILE *fa = fopen("test_quality_a.md", "rb");
    FILE *fb = fopen("test_quality_b.md", "rb");
    assert(fread(ba, 1, (size_t)n, fa) == (size_t)n);
    assert(fread(bb, 1, (size_t)n, fb) == (size_t)n);
    fclose(fa);
    fclose(fb);
    assert(memcmp(ba, bb, (size_t)n) == 0);

    /* 결론 절이 실제로 쓰였는가 — 표만 있는 리포트를 막는다. */
    ba[n] = '\0';
    assert(strstr(ba, "## 결론") != NULL);
    assert(strstr(ba, "기준선을 뺀 12칸 중") != NULL);
    assert(strstr(ba, "- **BALANCED** — ") != NULL);
    assert(strstr(ba, "- **CROSSED** — ") != NULL);

    remove("test_quality_a.md");
    remove("test_quality_b.md");

    assert(quality_write_md(&a, "2026-09-16", "없는디렉터리/x.md") ==
           ERR_NOT_FOUND);
}

static void test_rejects(void)
{
    quality_report_t rep;
    compare_config_t c = COMPARE_DEFAULT;

    assert(quality_run(&c, 0, &rep) == ERR_INVALID_ARG);
    assert(quality_run(&c, QUALITY_MAX_SEEDS + 1, &rep) == ERR_INVALID_ARG);
    assert(quality_run(&c, 1, NULL) == ERR_NULL_PTR);

    c.seed = UINT64_MAX;
    assert(quality_run(&c, 2, &rep) == ERR_INVALID_ARG); /* 0을 지난다 */
    c.seed = 0;
    assert(quality_run(&c, 1, &rep) == ERR_INVALID_ARG);
}

int main(void)
{
    test_dist();
    test_verdict();
    test_matches_compare_run();
    test_same_args_same_bytes();
    test_rejects();
    printf("test_quality: OK\n");
    return 0;
}
