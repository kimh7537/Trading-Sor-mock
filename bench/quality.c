#include "quality.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"

enum { METRIC_VS, METRIC_SLIP, METRIC_FILL, METRIC_COUNT };

static int cmp_i32(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a;
    int32_t y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

int quality_dist(int32_t *values, int32_t n, quality_dist_t *out)
{
    if (values == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    if (n <= 0) {
        return ERR_INVALID_ARG;
    }

    qsort(values, (size_t)n, sizeof(values[0]), cmp_i32);
    out->min = values[0];
    out->max = values[n - 1];
    out->p50 = values[(n - 1) / 2]; /* 짝수면 가운데 둘 중 낮은 쪽 */
    return ERR_OK;
}

quality_verdict_t quality_verdict_of(int32_t wins, int32_t losses,
                                     int32_t ties)
{
    /*
     * **비김은 판정을 뒤집지 않는다**(T6-02). 처음에는 비김이 하나라도 있으면
     * "엇갈림"으로 보냈는데, 29승 0패 1무가 엇갈림이 되어 결론 문장이 데이터와
     * 반대로 읽혔다.
     */
    (void)ties;

    if (wins == 0 && losses == 0) {
        return QUALITY_NO_DIFF;
    }
    if (losses == 0) {
        return QUALITY_NEVER_WORSE;
    }
    if (wins == 0) {
        return QUALITY_NEVER_BETTER;
    }
    return QUALITY_MIXED;
}

const char *quality_verdict_str(quality_verdict_t v)
{
    switch (v) {
    case QUALITY_NEVER_WORSE:
        return "진 적 없음";
    case QUALITY_NEVER_BETTER:
        return "이긴 적 없음";
    case QUALITY_NO_DIFF:
        return "차이 없음";
    case QUALITY_MIXED:
        return "엇갈림";
    default:
        return "?";
    }
}

int quality_run(const compare_config_t *base, int32_t seed_count,
                quality_report_t *out)
{
    if (out == NULL) {
        return ERR_NULL_PTR;
    }
    compare_config_t c = (base != NULL) ? *base : COMPARE_DEFAULT;
    if (seed_count < 1 || seed_count > QUALITY_MAX_SEEDS) {
        return ERR_INVALID_ARG;
    }

    /* [시나리오][전략][지표][시드]. 핫 패스가 아니므로 한 번 할당한다. */
    size_t   per_cell = (size_t)METRIC_COUNT * (size_t)seed_count;
    int32_t *samples = calloc(
        (size_t)COMPARE_SCENARIO_COUNT * COMPARE_STRATEGY_COUNT * per_cell,
        sizeof(int32_t));
    if (samples == NULL) {
        return ERR_POOL_EXHAUSTED;
    }

    memset(out, 0, sizeof(*out));
    out->base = c;
    out->seed_count = seed_count;

    int rc = ERR_OK;
    for (int32_t i = 0; i < seed_count && rc == ERR_OK; i++) {
        compare_config_t cfg = c;
        cfg.seed = c.seed + (uint64_t)i;

        compare_result_t res;
        rc = compare_run(&cfg, &res);

        for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT && rc == ERR_OK; s++) {
            for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
                const compare_row_t *row = &res.row[s][k];
                quality_cell_t      *cell = &out->cell[s][k];
                int32_t             *m =
                    samples +
                    ((size_t)s * COMPARE_STRATEGY_COUNT + (size_t)k) * per_cell;

                m[METRIC_VS * seed_count + i] = row->vs_krx_only_bp;
                m[METRIC_SLIP * seed_count + i] = row->slippage_bp;
                m[METRIC_FILL * seed_count + i] = row->fill_rate_bp;

                cell->strategy = row->strategy;
                cell->scenario = row->scenario;
                if (row->vs_krx_only_bp > 0) {
                    cell->wins++;
                } else if (row->vs_krx_only_bp < 0) {
                    cell->losses++;
                } else {
                    cell->ties++;
                }
            }
        }
    }

    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT && rc == ERR_OK; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            quality_cell_t *cell = &out->cell[s][k];
            int32_t        *m =
                samples +
                ((size_t)s * COMPARE_STRATEGY_COUNT + (size_t)k) * per_cell;

            assert(cell->wins + cell->losses + cell->ties == seed_count);
            (void)quality_dist(m + METRIC_VS * seed_count, seed_count,
                               &cell->vs_krx_only_bp);
            (void)quality_dist(m + METRIC_SLIP * seed_count, seed_count,
                               &cell->slippage_bp);
            (void)quality_dist(m + METRIC_FILL * seed_count, seed_count,
                               &cell->fill_rate_bp);
            cell->verdict =
                quality_verdict_of(cell->wins, cell->losses, cell->ties);
        }
    }

    free(samples);
    return rc;
}

static void write_pct(FILE *f, int32_t bp)
{
    fprintf(f, "%d.%02d%%", bp / 100, bp % 100);
}

/* 한 시나리오의 결론 한 줄. 기준선(0번 열)은 빼고 판정별로 묶는다. */
static void write_conclusion(FILE *f, const quality_report_t *r, int32_t s)
{
    static const quality_verdict_t ORDER[] = {
        QUALITY_NEVER_WORSE, QUALITY_MIXED, QUALITY_NEVER_BETTER,
        QUALITY_NO_DIFF};

    fprintf(f, "- **%s** —", scenario_str((scenario_t)s));
    int32_t groups = 0;
    for (size_t v = 0; v < sizeof(ORDER) / sizeof(ORDER[0]); v++) {
        int32_t shown = 0;
        for (int32_t k = 1; k < COMPARE_STRATEGY_COUNT; k++) {
            const quality_cell_t *c = &r->cell[s][k];
            if (c->verdict != ORDER[v]) {
                continue;
            }
            if (shown == 0) {
                fprintf(f, "%s%s: ", groups == 0 ? " " : " / ",
                        quality_verdict_str(ORDER[v]));
            } else {
                fprintf(f, ", ");
            }
            fprintf(f, "%s", c->strategy);
            /*
             * 비김이 섞일 수 있으므로 **어느 판정이든 승/패/무를 같이 적는다.**
             * "진 적 없음"만 보고 30전 30승으로 읽으면 안 된다.
             */
            if (ORDER[v] != QUALITY_NO_DIFF) {
                fprintf(f, "(%d승 %d패 %d무, 중앙값 %+dbp)", c->wins,
                        c->losses, c->ties, c->vs_krx_only_bp.p50);
            }
            shown++;
        }
        if (shown > 0) {
            groups++;
        }
    }
    fprintf(f, "\n");
}

int quality_write_md(const quality_report_t *r, const char *date,
                     const char *path)
{
    if (r == NULL || date == NULL || path == NULL) {
        return ERR_NULL_PTR;
    }

    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return ERR_NOT_FOUND;
    }

    const compare_config_t *c = &r->base;
    int32_t                 n = r->seed_count;

    fprintf(f, "# 집행 품질 리포트 (%s)\n\n", date);
    fprintf(f,
            "`bench/results/strategies-*.md`(T2-14)는 한 시드의 한 장면이다. "
            "이 리포트는 같은 실험을 **시드 %d개**로 돌려 칸마다 분포와 이긴 "
            "시드 수를 모았다. 평균은 쓰지 않는다 — 평균은 몇 번 이겼는지를 "
            "감춘다.\n\n",
            n);

    fprintf(f, "## 설정\n\n");
    fprintf(f, "| 항목 | 값 |\n|---|---|\n");
    fprintf(f, "| 시드 | %llu ~ %llu (%d개) |\n", (unsigned long long)c->seed,
            (unsigned long long)(c->seed + (uint64_t)(n - 1)), n);
    fprintf(f, "| 기준가 | %d |\n", c->ref_price);
    fprintf(f, "| 시장당 유동성 주문 | %d |\n", c->liquidity_orders);
    fprintf(f, "| 측정 주문 | 시드당 %d (매수만) |\n", c->taker_orders);
    fprintf(f, "| 측정 주문 수량 | %d ~ %d |\n", c->taker_qty_min,
            c->taker_qty_max);
    fprintf(f, "| 지정가 공격도 | 기준가 + %d틱 |\n\n", c->aggression_ticks);

    fprintf(f, "## 결론\n\n");
    fprintf(f, "KRX_ONLY(기준선) 대비. 아래 표에서 자동으로 뽑았다.\n\n");
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        write_conclusion(f, r, s);
    }

    int32_t count[QUALITY_MIXED + 1] = {0};
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        for (int32_t k = 1; k < COMPARE_STRATEGY_COUNT; k++) {
            count[r->cell[s][k].verdict]++;
        }
    }
    fprintf(f,
            "\n기준선을 뺀 %d칸 중 진 적 없음 %d, 엇갈림 %d, 이긴 적 없음 %d, "
            "차이 없음 %d.\n",
            COMPARE_SCENARIO_COUNT * (COMPARE_STRATEGY_COUNT - 1),
            count[QUALITY_NEVER_WORSE], count[QUALITY_MIXED],
            count[QUALITY_NEVER_BETTER], count[QUALITY_NO_DIFF]);

    fprintf(f, "\n## 결과\n\n");
    fprintf(f,
            "| 시나리오 | 전략 | KRX_ONLY 대비 p50 (min ~ max) | 승/패/무 | 판정 "
            "| 슬리피지 p50 (min ~ max) | 체결률 p50 (min ~ max) |\n");
    fprintf(f, "|---|---|---:|---:|---|---:|---:|\n");
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            const quality_cell_t *q = &r->cell[s][k];
            fprintf(f, "| %s | %s | %+d (%+d ~ %+d) | %d/%d/%d | %s | ",
                    scenario_str(q->scenario), q->strategy,
                    q->vs_krx_only_bp.p50, q->vs_krx_only_bp.min,
                    q->vs_krx_only_bp.max, q->wins, q->losses, q->ties,
                    k == 0 ? "기준선" : quality_verdict_str(q->verdict));
            fprintf(f, "%+d (%+d ~ %+d) | ", q->slippage_bp.p50,
                    q->slippage_bp.min, q->slippage_bp.max);
            write_pct(f, q->fill_rate_bp.p50);
            fprintf(f, " (");
            write_pct(f, q->fill_rate_bp.min);
            fprintf(f, " ~ ");
            write_pct(f, q->fill_rate_bp.max);
            fprintf(f, ") |\n");
        }
    }

    fprintf(f, "\n## 읽는 법\n\n");
    fprintf(f,
            "- **KRX_ONLY 대비(bp)**: 양수면 그만큼 싸게 샀다. 승/패/무는 이 "
            "값이 양수·음수·0이었던 시드 수다\n");
    fprintf(f,
            "- **판정**: 이긴 적이 있고 진 적이 없으면 **진 적 없음**, 진 적이 "
            "있고 이긴 적이 없으면 **이긴 적 없음**, 둘 다 있으면 **엇갈림**, "
            "둘 다 없으면 **차이 없음**. **비김은 판정을 뒤집지 않는다** — "
            "29승 1무를 엇갈림이라 부르면 한 번도 안 진 전략이 들쭉날쭉한 것처럼 "
            "읽힌다. \"항상\"이라는 말은 쓰지 않으니 승/패/무를 함께 본다\n");
    fprintf(f,
            "- **p50**: 시드 수가 짝수면 가운데 둘 중 낮은 쪽이다. 평균을 내면 "
            "어느 시드에서도 나오지 않은 숫자가 되고, 낮은 쪽이면 개선폭을 "
            "부풀리지 않는다\n");
    fprintf(f,
            "- **체결률을 같이 본다.** 단가가 좋아도 체결률이 낮으면 남은 "
            "수량을 나중에 더 비싸게 산다\n");

    fprintf(f, "\n## 이 리포트가 말하지 못하는 것\n\n");
    fprintf(f,
            "- T2-14 실험의 한계를 그대로 물려받는다 — **매수만 내고, 세션 "
            "규칙을 걸지 않았다.** 이유는 `strategies-*.md`에 있다\n");
    fprintf(f,
            "- **bp 해상도 안쪽의 차이는 비김으로 센다.** KRX_ONLY 대비는 체결 "
            "금액에서 직접 내지만(T6-07) 결과가 정수 bp라, 0.5bp 미만 차이는 "
            "0이 된다. 세 전략이 칸마다 같은 승패를 보이는 것은 이 때문이 "
            "아니다 — 셋이 실제로 같은 호가를 다 먹어 체결 금액이 같거나 "
            "0.01bp 미만으로만 다르기 때문이다(`strategies-*.md` 참조)\n");
    fprintf(f,
            "- 시드만 바꿨다. 기준가·주문 크기·공격도는 고정이다. 설정이 "
            "바뀌어도 같은 결론인지는 이 표가 말하지 않는다\n");

    fclose(f);
    return ERR_OK;
}
