#ifndef MINI_SOR_QUALITY_H
#define MINI_SOR_QUALITY_H

#include <stdint.h>

#include "compare.h"

/*
 * 집행 품질 리포트 — T2-14 비교 실험을 **시드 여러 개로** 돌려 모은다.
 *
 * T2-14의 표는 한 시드의 한 장면이다. "BEST_PRICE가 +5bp"라는 칸이 시드를 바꿔도
 * +5인지, 어떤 시드에서는 -3인지는 그 표로 알 수 없다. 전략의 우열을 말하려면
 * 분포를 봐야 한다.
 *
 * ---
 *
 * **평균을 쓰지 않는다.**
 *
 * 평균 +2bp는 "30번 모두 +2"일 수도, "29번 +5에 한 번 -85"일 수도 있다. 둘은
 * 전혀 다른 전략이다. 그래서 중앙값·최소·최대와 **이긴 시드 수**를 같이 낸다
 * (T1-20·T5-05가 p50/p99를 쓴 것과 같은 이유다).
 *
 * **짝수 개일 때 중앙값은 가운데 둘 중 낮은 쪽이다.**
 *
 * 가운데 둘의 평균을 내면 정수가 아닐 수 있고, 반올림 규칙을 하나 더 들여와야
 * 한다. 게다가 그 값은 **어느 시드에서도 실제로 나오지 않은 숫자**가 된다.
 * 한쪽을 고르면 표의 중앙값은 언제나 실제로 관측된 값이다. KRX_ONLY 대비와
 * 체결률은 클수록 좋은 값이라, 낮은 쪽을 고르면 개선폭을 부풀리지 않는다.
 *
 * ---
 *
 * 시드는 `base->seed`부터 **1씩 늘린다.** 이웃한 시드가 닮은 수열을 만들지 않는
 * 것은 두 생성기가 시드를 섞어 쓰기 때문이다 (compare.c의 taker_init, T1-17).
 */

#define QUALITY_MAX_SEEDS 256

/*
 * 판정. **이름이 문자 그대로 참이어야 한다**(T6-02).
 *
 * 처음에는 "항상 우위/항상 열위"를 비김이 하나도 없을 때만 주고 나머지를 전부
 * "엇갈림"에 넣었다. 과장하지 않으려는 판단이었지만, 그 결과 **29승 0패 1무**와
 * **0승 20패 10무**가 둘 다 "엇갈림"이 됐다 — 한 번도 안 진 전략이 들쭉날쭉한
 * 것처럼, 한 번도 못 이긴 전략이 가끔 이기는 것처럼 읽혔다. 과장을 피하려다
 * 반대 방향으로 틀렸다.
 *
 * 그래서 "항상"이라는 말을 버리고 실제로 일어난 일만 이름으로 쓴다. 비김은
 * 판정을 뒤집지 않는다. 엇갈림은 **이긴 적도 진 적도 있을 때만**이다.
 *
 * 순서를 바꾸지 않는다 — `QUALITY_MIXED`가 마지막이어야 개수 배열이 맞는다.
 */
typedef enum {
    QUALITY_NEVER_WORSE,  /* 이긴 적이 있고 진 적은 없다(비김은 섞여도 된다) */
    QUALITY_NEVER_BETTER, /* 진 적이 있고 이긴 적은 없다 */
    QUALITY_NO_DIFF,      /* 이긴 적도 진 적도 없다. 기준선 자신이 여기 온다 */
    QUALITY_MIXED         /* 이긴 적도 진 적도 있다 */
} quality_verdict_t;

typedef struct {
    int32_t p50; /* 짝수 개면 가운데 둘 중 낮은 쪽 */
    int32_t min;
    int32_t max;
} quality_dist_t;

/* 한 (시나리오, 전략) 칸. */
typedef struct {
    const char *strategy;
    scenario_t  scenario;

    quality_dist_t vs_krx_only_bp; /* 양수면 더 싸게 샀다 */
    quality_dist_t slippage_bp;    /* 양수면 불리하게 샀다 */
    quality_dist_t fill_rate_bp;

    int32_t wins;   /* vs_krx_only_bp > 0 인 시드 수 */
    int32_t losses; /* < 0 */
    int32_t ties;   /* == 0 */

    quality_verdict_t verdict;
} quality_cell_t;

typedef struct {
    compare_config_t base; /* seed가 첫 시드다 */
    int32_t          seed_count;
    quality_cell_t   cell[COMPARE_SCENARIO_COUNT][COMPARE_STRATEGY_COUNT];
} quality_report_t;

/*
 * values[0..n)의 분포. **values를 정렬한다** (호출자의 버퍼를 바꾼다).
 * n이 0 이하면 ERR_INVALID_ARG.
 */
int quality_dist(int32_t *values, int32_t n, quality_dist_t *out);

/*
 * 이긴·진·비긴 수로 판정한다. 규칙은 위 열거형 주석과 같다.
 * `ties`는 판정에 쓰지 않는다 — **비김은 판정을 뒤집지 않는다.** 인자로 받는
 * 것은 부르는 쪽이 세 수를 함께 넘기게 해 빠뜨림을 막으려는 것이다.
 */
quality_verdict_t quality_verdict_of(int32_t wins, int32_t losses,
                                     int32_t ties);

const char *quality_verdict_str(quality_verdict_t v);

/*
 * base->seed부터 seed_count개 시드로 compare_run()을 돌려 모은다.
 * base가 NULL이면 COMPARE_DEFAULT.
 *
 * seed_count가 1~QUALITY_MAX_SEEDS 밖이면 ERR_INVALID_ARG. 시드가 넘치면
 * 반드시 0을 지나고, compare_run()이 시드 0을 거절한다 — 그 실패를 그대로
 * 돌려준다.
 * 같은 인자로 두 번 부르면 결과가 바이트까지 같다.
 */
int quality_run(const compare_config_t *base, int32_t seed_count,
                quality_report_t *out);

/*
 * 마크다운으로 쓴다. 결론 문장을 표에서 뽑아 스스로 쓴다.
 * date는 파일에 적을 날짜다 — 시스템 시각을 읽지 않는다.
 * 파일을 못 열면 ERR_NOT_FOUND.
 */
int quality_write_md(const quality_report_t *r, const char *date,
                     const char *path);

#endif /* MINI_SOR_QUALITY_H */
