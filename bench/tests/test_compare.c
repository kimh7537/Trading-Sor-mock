/*
 * T2-14 전략 비교 실험 하네스 — **하네스 자체의 재현성**을 검사한다.
 *
 * 완료 조건에서 이 테스트가 맡은 것은 "재현 가능: 같은 시드로 두 번 돌리면 같은 표가
 * 나온다"이다. 전략의 우열은 여기서 판정하지 않는다 — 그건 실험 결과지 단위 테스트의
 * 대상이 아니다. 다만 **비교가 성립할 조건**은 검사한다.
 *
 *  1. 같은 시드 -> 바이트까지 같은 표
 *  2. 다른 시드 -> 다른 표 (시드가 실제로 쓰인다)
 *  3. **유동성이 전략마다 같다** — 네 전략이 같은 호가창에서 출발한다
 *  4. 16칸이 다 채워지고 기준선 열의 KRX_ONLY 대비가 0이다
 */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "compare.h"
#include "errors.h"

/* --- 1. 재현성 --- */

static void test_same_seed_same_table(void)
{
    compare_result_t a;
    compare_result_t b;

    assert(compare_run(NULL, &a) == ERR_OK);
    assert(compare_run(NULL, &b) == ERR_OK);

    /*
     * 필드를 하나씩 보지 않고 표 전체를 바이트로 비교한다. 칸을 더해도 비교가
     * 저절로 따라온다. 전략 이름은 정적 문자열이라 두 실행에서 같은 주소를
     * 가리킨다 — 그것도 재현성의 일부다.
     */
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        assert(memcmp(a.row[s], b.row[s],
                      sizeof(compare_row_t) * COMPARE_STRATEGY_COUNT) == 0);
    }
}

static void test_different_seed_different_table(void)
{
    compare_config_t c1 = COMPARE_DEFAULT;
    compare_config_t c2 = COMPARE_DEFAULT;
    c2.seed = COMPARE_DEFAULT.seed + 12345;

    compare_result_t a;
    compare_result_t b;
    assert(compare_run(&c1, &a) == ERR_OK);
    assert(compare_run(&c2, &b) == ERR_OK);

    /*
     * 시드를 바꿨는데 표가 그대로면 시드가 어딘가에서 무시되고 있다는 뜻이다.
     * 한 칸이라도 달라야 한다.
     */
    bool differs = false;
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT && !differs; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            if (a.row[s][k].notional != b.row[s][k].notional ||
                a.row[s][k].filled_qty != b.row[s][k].filled_qty) {
                differs = true;
                break;
            }
        }
    }
    assert(differs);
}

/*
 * 시드가 **두 곳 모두**에 실제로 쓰이는가.
 *
 * 위의 검사만으로는 한쪽만 시드를 따라도 통과한다 — 유동성이 고정이어도 taker가
 * 달라지면 표가 달라지고, 그 반대도 마찬가지다. 그래서 둘을 따로 떼어 본다.
 */
static void test_seed_feeds_both_generators(void)
{
    /*
     * (1) taker 생성기 — 낸 수량 합(order_qty)은 **taker 난수만으로** 정해진다.
     *     호가창이 무엇이든 주문 수량은 그대로다. 시드를 바꿔 이 값이 안 변하면
     *     taker가 시드를 무시하고 있다는 뜻이다.
     */
    compare_config_t c1 = COMPARE_DEFAULT;
    compare_config_t c2 = COMPARE_DEFAULT;
    c2.seed = COMPARE_DEFAULT.seed + 999;

    compare_result_t a;
    compare_result_t b;
    assert(compare_run(&c1, &a) == ERR_OK);
    assert(compare_run(&c2, &b) == ERR_OK);
    assert(a.row[0][0].order_qty != b.row[0][0].order_qty);

    /*
     * (2) 유동성 생성기 — taker를 **완전히 고정**해 놓고 시드를 바꾼다.
     *     수량 범위를 한 점으로 좁히면 주문 수량·가격·번호·시각이 모두 같아지므로
     *     taker 난수는 결과에 아무 영향도 주지 못한다. 그래도 표가 달라진다면
     *     그 차이는 오직 호가창에서 온 것이다.
     */
    compare_config_t f1 = COMPARE_DEFAULT;
    f1.taker_qty_min = 100;
    f1.taker_qty_max = 100;
    compare_config_t f2 = f1;
    f2.seed = f1.seed + 999;

    compare_result_t x;
    compare_result_t y;
    assert(compare_run(&f1, &x) == ERR_OK);
    assert(compare_run(&f2, &y) == ERR_OK);

    /* taker가 같으므로 낸 수량은 반드시 같다 — 조건이 제대로 걸렸는지 먼저 본다. */
    assert(x.row[0][0].order_qty == y.row[0][0].order_qty);

    bool liq_differs = false;
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT && !liq_differs; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            if (x.row[s][k].notional != y.row[s][k].notional) {
                liq_differs = true;
                break;
            }
        }
    }
    assert(liq_differs);
}

/* --- 2. 유동성이 전략마다 같은가 --- */

/*
 * 완료 조건의 핵심이다. 전략은 호가창을 바꾸므로, 네 전략이 **같은 출발점**에서
 * 시작하지 않으면 비교가 아니라 순서 측정이 된다.
 */
static void test_liquidity_is_identical_per_strategy(void)
{
    compare_result_t full;
    compare_result_t again;
    assert(compare_run(NULL, &full) == ERR_OK);
    assert(compare_run(NULL, &again) == ERR_OK);

    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            assert(full.row[s][k].filled_qty == again.row[s][k].filled_qty);
            assert(full.row[s][k].notional == again.row[s][k].notional);
        }
    }

    /*
     * 네 전략이 같은 호가창에서 출발했다면 **같은 수량을 주문했어야 한다.**
     * 낸 수량은 전략과 무관한 입력이므로 네 칸이 모두 같다. 호가창을 공유해
     * taker 열이 어긋났다면 여기서 걸린다.
     */
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        qty_t first = full.row[s][0].order_qty;
        assert(first > 0);
        for (int32_t k = 1; k < COMPARE_STRATEGY_COUNT; k++) {
            assert(full.row[s][k].order_qty == first);
        }
    }
}

/* --- 3. 표의 모양 --- */

static void test_table_shape(void)
{
    compare_result_t res;
    assert(compare_run(NULL, &res) == ERR_OK);

    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
        /* 기준선은 자기 자신과의 차이가 0이다. */
        assert(res.row[s][0].vs_krx_only_bp == 0);
        assert(strcmp(res.row[s][0].strategy, "KRX_ONLY") == 0);

        for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
            const compare_row_t *r = &res.row[s][k];

            assert(r->scenario == (scenario_t)s);
            assert(r->strategy == compare_strategy(k)->name);

            /* 낸 수량이 있어야 실험이 돈 것이다. */
            assert(r->order_qty > 0);
            assert(r->filled_qty >= 0);
            assert(r->filled_qty <= r->order_qty);

            /* 체결이 있으면 평균 단가와 금액이 서로 맞는다. */
            if (r->filled_qty > 0) {
                assert(r->notional > 0);
                assert(r->avg_price == (price_t)(r->notional / r->filled_qty));
                assert(r->bench_notional > 0);

                /*
                 * **기준가는 접수 시점의 최우선 매도호가다.** 매수는 그보다 싸게
                 * 체결될 수 없다 — 최우선호가가 그 시점의 가장 싼 값이므로.
                 * 따라서 슬리피지는 결코 음수가 아니다.
                 *
                 * 기준가를 집행 뒤에 잡거나 주문 지정가로 잡으면 이 부등식이
                 * 깨진다. 그래서 이 한 줄이 "기준가를 언제 무엇으로 잡았는가"를
                 * 붙잡는다.
                 */
                assert(r->bench_notional <= r->notional);
                assert(r->slippage_bp >= 0);
            } else {
                assert(r->notional == 0);
                assert(r->avg_price == 0);
            }

            /* 체결률은 낸 수량 대비다. 범위만 보면 "항상 100%"도 통과한다. */
            assert(r->fill_rate_bp == eq_to_bp(r->filled_qty, r->order_qty));
            assert(r->fill_rate_bp >= 0 && r->fill_rate_bp <= 10000);

            /*
             * 기준선 대비 부호 — **싸게 샀으면 양수**다. 부호가 뒤집히면 표를
             * 읽는 사람이 결론을 정반대로 받아들인다.
             *
             * 부호는 **원 단위 평균이 아니라 체결 금액으로** 판단한다(T6-07).
             * 평균 N_b/F_b와 N_r/F_r의 대소는 N_b*F_r와 N_r*F_b의 대소와 같다.
             * 차이가 0.5bp 미만이면 0으로 반올림될 수 있으므로 "반대 부호가
             * 아니다"만 요구한다.
             */
            const compare_row_t *b = &res.row[s][0];
            if (r->filled_qty > 0 && b->filled_qty > 0) {
                int64_t cross = b->notional * r->filled_qty -
                                r->notional * b->filled_qty;
                if (cross > 0) {
                    assert(r->vs_krx_only_bp >= 0); /* r이 더 싸게 샀다 */
                } else if (cross < 0) {
                    assert(r->vs_krx_only_bp <= 0);
                } else {
                    assert(r->vs_krx_only_bp == 0);
                }
            }
        }

        /*
         * 실제로 부호가 갈리는 칸이 있어야 위 검사가 일한다. 네 전략의 평균
         * 단가가 모두 같은 시나리오만 있으면 부호 검사가 한 번도 실행되지 않는다.
         */
    }

    bool any_sign_checked = false;
    for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT && !any_sign_checked; s++) {
        for (int32_t k = 1; k < COMPARE_STRATEGY_COUNT; k++) {
            if (res.row[s][k].avg_price != res.row[s][0].avg_price) {
                any_sign_checked = true;
                break;
            }
        }
    }
    assert(any_sign_checked);
}

/* 시나리오가 실제로 다른 장면을 만드는가 — 같으면 비교할 것이 없다. */
static void test_scenarios_differ(void)
{
    compare_result_t res;
    assert(compare_run(NULL, &res) == ERR_OK);

    bool any_differ = false;
    for (int32_t s = 1; s < COMPARE_SCENARIO_COUNT; s++) {
        if (res.row[s][0].notional != res.row[0][0].notional) {
            any_differ = true;
            break;
        }
    }
    assert(any_differ);
}

/* --- 4. 인자와 출력 --- */

static void test_args(void)
{
    compare_result_t res;
    assert(compare_run(NULL, NULL) == ERR_NULL_PTR);

    compare_config_t bad = COMPARE_DEFAULT;
    bad.seed = 0;
    assert(compare_run(&bad, &res) == ERR_INVALID_ARG);

    bad = COMPARE_DEFAULT;
    bad.taker_orders = 0;
    assert(compare_run(&bad, &res) == ERR_INVALID_ARG);

    bad = COMPARE_DEFAULT;
    bad.liquidity_orders = 0;
    assert(compare_run(&bad, &res) == ERR_INVALID_ARG);

    bad = COMPARE_DEFAULT;
    bad.taker_qty_max = bad.taker_qty_min - 1;
    assert(compare_run(&bad, &res) == ERR_INVALID_ARG);

    assert(compare_strategy(-1) == NULL);
    assert(compare_strategy(COMPARE_STRATEGY_COUNT) == NULL);
    assert(compare_strategy(0) != NULL);
}

/*
 * 표를 파일로 쓴다. **같은 결과에서 쓴 두 파일은 바이트까지 같다** —
 * 하네스가 시스템 시각을 읽으면 여기서 걸린다.
 */
static void test_write_md(void)
{
    compare_config_t cfg = COMPARE_DEFAULT;
    cfg.taker_orders = 20;
    cfg.liquidity_orders = 50;

    compare_result_t res;
    assert(compare_run(&cfg, &res) == ERR_OK);

    const char *p1 = "test_compare_out1.md";
    const char *p2 = "test_compare_out2.md";

    assert(compare_write_md(&res, "2026-09-16", p1) == ERR_OK);
    assert(compare_write_md(&res, "2026-09-16", p2) == ERR_OK);

    FILE *f1 = fopen(p1, "rb");
    FILE *f2 = fopen(p2, "rb");
    assert(f1 != NULL && f2 != NULL);

    int    c1;
    int    c2;
    size_t n = 0;
    do {
        c1 = fgetc(f1);
        c2 = fgetc(f2);
        assert(c1 == c2);
        n++;
    } while (c1 != EOF);
    assert(n > 200); /* 내용이 있어야 한다 */

    fclose(f1);
    fclose(f2);

    /* 날짜가 실제로 파일에 들어간다. */
    f1 = fopen(p1, "rb");
    assert(f1 != NULL);
    char   buf[4096];
    size_t len = fread(buf, 1, sizeof(buf) - 1, f1);
    buf[len] = '\0';
    fclose(f1);
    assert(strstr(buf, "2026-09-16") != NULL);
    assert(strstr(buf, "KRX_ONLY") != NULL);
    assert(strstr(buf, "SWEEP") != NULL);
    assert(strstr(buf, "BALANCED") != NULL);

    remove(p1);
    remove(p2);

    assert(compare_write_md(NULL, "2026-09-16", p1) == ERR_NULL_PTR);
    assert(compare_write_md(&res, NULL, p1) == ERR_NULL_PTR);
    assert(compare_write_md(&res, "2026-09-16", NULL) == ERR_NULL_PTR);
    /* 없는 디렉터리에는 쓸 수 없다. 조용히 성공한 척하지 않는다. */
    assert(compare_write_md(&res, "2026-09-16", "없는디렉터리/x.md") ==
           ERR_NOT_FOUND);
}

/*
 * **KRX_ONLY 대비를 원 단위 평균 단가끼리 빼지 않는다**(T6-07).
 *
 * 기본 시드에서는 옛 계산(버린 평균끼리 차이)과 새 계산(체결 금액에서 직접)이
 * 우연히 같은 값을 낸다. 그래서 기본 시드만 보면 `compare.c`를 옛 식으로
 * 되돌려도 통과한다. **두 식이 실제로 갈리는 시드를 찾아서** 거기서 새 식을
 * 쓰는지 본다. 찾지 못하면 이 검사가 아무 일도 안 한 것이므로 실패시킨다.
 */
static void test_vs_krx_only_uses_notional(void)
{
    static compare_result_t res;
    compare_config_t        c = COMPARE_DEFAULT;
    bool                    diverged = false;

    for (uint64_t seed = c.seed; seed < c.seed + 30u && !diverged; seed++) {
        c.seed = seed;
        assert(compare_run(&c, &res) == ERR_OK);

        for (int32_t s = 0; s < COMPARE_SCENARIO_COUNT; s++) {
            const compare_row_t *b = &res.row[s][0];
            for (int32_t k = 0; k < COMPARE_STRATEGY_COUNT; k++) {
                const compare_row_t *r = &res.row[s][k];

                int32_t precise = eq_avg_diff_bp(b->notional, b->filled_qty,
                                                 r->notional, r->filled_qty);
                assert(r->vs_krx_only_bp == precise);

                int32_t rounded =
                    eq_to_bp((int64_t)b->avg_price - r->avg_price, b->avg_price);
                if (rounded != precise) {
                    diverged = true;
                }
            }
        }
    }

    /* 두 식이 갈린 칸을 한 번은 봤어야 위 대조가 옛 식을 잡을 수 있다 */
    assert(diverged);
}

int main(void)
{
    test_same_seed_same_table();
    test_vs_krx_only_uses_notional();
    test_different_seed_different_table();
    test_seed_feeds_both_generators();
    test_liquidity_is_identical_per_strategy();
    test_table_shape();
    test_scenarios_differ();
    test_args();
    test_write_md();
    return 0;
}
