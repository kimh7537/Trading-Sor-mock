/*
 * 원장 데몬.
 *
 *   ledgerd [포트]
 *
 * 포트를 안 주면 9100에서 기다린다(채널계 기본 설정과 같다). 0을 주면 커널이 고른
 * 포트를 쓰고 그 번호를 찍는다.
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

int main(int argc, char **argv)
{
    uint16_t port = LEDGERD_DEFAULT_PORT;

    if (argc >= 2) {
        char *end = NULL;
        long  v = strtol(argv[1], &end, 10);
        if (end == argv[1] || *end != '\0' || v < 0 || v > 65535) {
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
