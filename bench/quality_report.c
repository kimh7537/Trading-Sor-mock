/*
 * 집행 품질 리포트를 만든다 (T5-08).
 *
 *   quality_report <날짜> [시드 개수=30] [출력경로] [첫 시드]
 *
 * 날짜를 인자로 받는다 — 시스템 시각을 읽으면 같은 인자로 돌린 두 실행의
 * 산출물이 달라진다 (compare_strategies와 같은 규칙).
 */
#include <stdio.h>
#include <stdlib.h>

#include "errors.h"
#include "quality.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr,
                "사용법: %s <날짜(YYYY-MM-DD)> [시드 개수=30] [출력경로] "
                "[첫 시드]\n",
                argv[0]);
        return 2;
    }

    const char *date = argv[1];
    long        n = (argc >= 3) ? strtol(argv[2], NULL, 10) : 30;
    if (n < 1 || n > QUALITY_MAX_SEEDS) {
        fprintf(stderr, "시드 개수는 1~%d\n", QUALITY_MAX_SEEDS);
        return 2;
    }

    char path[512];
    if (argc >= 4) {
        snprintf(path, sizeof(path), "%s", argv[3]);
    } else {
        snprintf(path, sizeof(path), "bench/results/quality-%s.md", date);
    }

    compare_config_t cfg = COMPARE_DEFAULT;
    if (argc >= 5) {
        cfg.seed = strtoull(argv[4], NULL, 10);
    }

    quality_report_t rep;
    int              rc = quality_run(&cfg, (int32_t)n, &rep);
    if (rc != ERR_OK) {
        fprintf(stderr, "실험 실패: %s\n", err_str(rc));
        return 1;
    }
    rc = quality_write_md(&rep, date, path);
    if (rc != ERR_OK) {
        fprintf(stderr, "%s 에 쓸 수 없다: %s\n", path, err_str(rc));
        return 1;
    }

    printf("시드 %ld개, %s 에 썼다\n", n, path);
    return 0;
}
