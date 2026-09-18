/*
 * 원장 데몬.
 *
 *   ledgerd [포트] [--live <초당 주문 수>]
 *
 * 포트를 안 주면 9100에서 기다린다(채널계 기본 설정과 같다). 0을 주면 커널이 고른
 * 포트를 쓰고 그 번호를 찍는다.
 *
 * `--live`를 주면 가상 참가자가 계속 주문을 내 양 시장 호가창이 스스로 움직인다
 * (T8-01). **주지 않으면 예전과 똑같다** — 시드 유동성을 넣고 그대로 멈춰 있다.
 *
 * 주문 전문을 받아 **계좌·증거금 검증 → SOR 배분 → 매칭 → 정산**까지 하고 응답한다.
 * 처리 논리는 전부 `ledger_core`에 있고, 이 파일은 소켓과 시그널만 맡는다.
 *
 * T3-03 때는 전문이 오가는지만 보는 껍데기였다. 계좌(T3-06)·검증(T3-07)이 끝난 뒤에도
 * 연결되지 않은 채 남아 무조건 성공을 돌려줬고, 감사에서 찾아 T6-03에서 이었다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "ledger_core.h"
#include "listener.h"

#define LEDGERD_DEFAULT_PORT 9100

/*
 * 틱 간격의 하한. 이보다 자주 깨우면 초당 주문 수가 많을 때 poll()만 돌린다.
 * 그래서 빠른 쪽은 간격을 줄이는 대신 한 번에 여러 건을 낸다.
 */
#define LEDGERD_TICK_MIN_MS 10

typedef struct {
    ledger_core_t *core;
    int32_t        per_tick;
} live_ctx_t;

/* 기다리다 심심하면 호가창을 한 틱 움직인다(T8-01). */
static void on_idle(void *ctx)
{
    live_ctx_t *lc = ctx;
    (void)ledger_core_tick(lc->core, lc->per_tick);
}

/* 초당 주문 수에서 "몇 ms마다 몇 건"을 정한다. */
static void live_pace(long rate, int *out_ms, int32_t *out_per_tick)
{
    if (rate * LEDGERD_TICK_MIN_MS >= 1000) {
        *out_ms = LEDGERD_TICK_MIN_MS;
        *out_per_tick = (int32_t)(rate * LEDGERD_TICK_MIN_MS / 1000);
    } else {
        *out_ms = (int)(1000 / rate);
        *out_per_tick = 1;
    }
}

int main(int argc, char **argv)
{
    uint16_t port = LEDGERD_DEFAULT_PORT;
    long     live_rate = 0; /* 0이면 실시세 모드가 아니다 */

    for (int i = 1; i < argc; i++) {
        char *end = NULL;
        if (strcmp(argv[i], "--live") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--live 뒤에 초당 주문 수가 와야 한다\n");
                return 2;
            }
            live_rate = strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != '\0' || live_rate <= 0 ||
                live_rate > 100000) {
                fprintf(stderr, "--live는 1~100000 사이여야 한다\n");
                return 2;
            }
            i++;
            continue;
        }
        long v = strtol(argv[i], &end, 10);
        if (end == argv[i] || *end != '\0' || v < 0 || v > 65535) {
            fprintf(stderr, "포트는 0~65535이어야 한다\n");
            return 2;
        }
        port = (uint16_t)v;
    }

    if (listener_install_signals() != ERR_OK) {
        fprintf(stderr, "시그널 핸들러를 걸 수 없다\n");
        return 1;
    }

    ledger_core_t *core = ledger_core_create(NULL);
    if (core == NULL) {
        fprintf(stderr, "원장 코어를 만들 수 없다\n");
        return 1;
    }

    listener_t *ln = listener_open(port, 64);
    if (ln == NULL) {
        fprintf(stderr, "포트 %u 를 열 수 없다\n", (unsigned)port);
        ledger_core_destroy(core);
        return 1;
    }

    const ledger_core_config_t *cfg = &LEDGER_CORE_DEFAULT;
    printf("ledgerd 포트 %u 에서 대기 (SIGTERM/SIGINT로 종료)\n",
           (unsigned)listener_port(ln));
    printf("  계좌 %s, 예수금 %lld원, 종목 %s, 기준가 %d원\n", cfg->account,
           (long long)cfg->cash, cfg->symbol, cfg->ref_price);

    live_ctx_t live = {.core = core, .per_tick = 0};
    if (live_rate > 0) {
        int tick_ms = 0;
        live_pace(live_rate, &tick_ms, &live.per_tick);
        listener_set_idle(ln, on_idle, &live, tick_ms);
        printf("  실시세 모드: 시장마다 초당 %ld건 (%dms마다 %d건)\n", live_rate,
               tick_ms, live.per_tick);
    }
    fflush(stdout);

    /*
     * **접속을 한 번에 하나씩 끝까지 처리한다.** 호가창이 하나여야 해서다
     * (`ledger_core.h`의 설명). 채널계는 접속 풀을 1로 두고 쓴다.
     */
    int conns = listener_run(ln, ledger_core_handle, core);

    printf("접속 %d건 처리 후 종료\n", conns);
    listener_close(ln);
    ledger_core_destroy(core);
    return 0;
}
