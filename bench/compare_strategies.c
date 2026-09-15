/*
 * 전략 비교 실험을 돌리고 결과 표를 파일로 남긴다.
 *
 *   compare_strategies <날짜> [출력경로] [시드]
 *
 * **날짜를 인자로 받는다.** 하네스가 시스템 시각을 읽으면 같은 시드로 돌린 두
 * 실행의 산출물이 달라진다 — 재현 가능해야 한다는 완료 조건과 정면으로 부딪친다.
 * 그래서 시각은 밖에서 들어오고, 안쪽은 시드에서만 움직인다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compare.h"
#include "errors.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "사용법: %s <날짜(YYYY-MM-DD)> [출력경로] [시드]\n"
                "  날짜를 인자로 받는 이유: 하네스가 시스템 시각을 읽으면\n"
                "  같은 시드로 돌린 두 실행의 산출물이 달라진다.\n",
                argv[0]);
        return 2;
    }

    const char *date = argv[1];

    char path[512];
    if (argc >= 3) {
        snprintf(path, sizeof(path), "%s", argv[2]);
    } else {
        snprintf(path, sizeof(path), "bench/results/strategies-%s.md", date);
    }

    compare_config_t cfg = COMPARE_DEFAULT;
    if (argc >= 4) {
        cfg.seed = strtoull(argv[3], NULL, 10);
        if (cfg.seed == 0) {
            fprintf(stderr, "시드는 0이 아니어야 한다\n");
            return 2;
        }
    }

    compare_result_t res;
    int              rc = compare_run(&cfg, &res);
    if (rc != ERR_OK) {
        fprintf(stderr, "실험 실패: %s\n", err_str(rc));
        return 1;
    }

    rc = compare_write_md(&res, date, path);
    if (rc != ERR_OK) {
        fprintf(stderr, "%s 에 쓸 수 없다: %s\n", path, err_str(rc));
        return 1;
    }

    /* 화면에도 요약을 찍는다. 파일을 열지 않고도 결과를 본다. */
    printf("시드 %llu, %s 에 %d x %d 표를 썼다\n", (unsigned long long)cfg.seed,
           path, COMPARE_SCENARIO_COUNT, COMPARE_STRATEGY_COUNT);
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            const compare_row_t *r = &res.row[s][k];
            printf("  %-9s %-11s 평균 %6d  슬리피지 %+5dbp  체결률 %3d.%02d%%  "
                   "KRX_ONLY 대비 %+5dbp\n",
                   scenario_str(r->scenario), r->strategy, r->avg_price,
                   r->slippage_bp, r->fill_rate_bp / 100, r->fill_rate_bp % 100,
                   r->vs_krx_only_bp);
        }
    }

    return 0;
}
