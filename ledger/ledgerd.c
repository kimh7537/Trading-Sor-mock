/*
 * 원장 데몬.
 *
 *   ledgerd [포트] [--live <초당 주문 수>] [--ref-price <원>]
 *
 * 포트를 안 주면 9100에서 기다린다(채널계 기본 설정과 같다). 0을 주면 커널이 고른
 * 포트를 쓰고 그 번호를 찍는다.

 * `--ref-price`는 **호가창이 다룰 가격대**를 정한다. 호가창은 기준가 ±30%(가격 제한폭,
 * docs/SPEC.md 3.1)만 펼쳐 두므로 그 밖의 가격은 받지 않는다. 실시세를 심을 때 종목의
 * 실제 가격이 기본값(70,000원)과 멀면 **스냅샷이 통째로 버려진다** — 조용히 아무 일도
 * 일어나지 않는다. 실시세 모드로 쓸 때는 그 종목의 실제 가격대를 줘야 한다.
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
#include "msg.h"
#include "wire.h"

#define LEDGERD_DEFAULT_PORT 9100

/*
 * 틱 간격의 하한. 이보다 자주 깨우면 초당 주문 수가 많을 때 poll()만 돌린다.
 * 그래서 빠른 쪽은 간격을 줄이는 대신 한 번에 여러 건을 낸다.
 */
#define LEDGERD_TICK_MIN_MS 10

typedef struct {
    ledger_core_t       *core;
    ledger_core_config_t cfg;
    char                 symbol[MSG_SYMBOL_LEN + 1];
    int32_t              per_tick;
} live_ctx_t;

/* 기다리다 심심하면 호가창을 한 틱 움직인다(T8-01). */
static void on_idle(void *ctx)
{
    live_ctx_t *lc = ctx;
    (void)ledger_core_tick(lc->core, lc->per_tick);
}

/*
 * 종목을 바꾼다(T8-10) — **그 종목의 원장을 새로 연다.**
 *
 * 호가창은 만들 때 정한 기준가 ±30%(가격 제한폭)만 펼쳐 둔다. 그래서 다루는 가격대를
 * 바꾸는 방법은 호가창을 다시 만드는 것뿐이다. 미체결 주문과 잔고는 초기화된다 — 이
 * 원장은 한 종목짜리이고, 앞 종목의 주문을 다른 종목의 호가창에 남겨 둘 자리가 없다.
 *
 * **새 코어를 먼저 만들고 성공했을 때만 갈아끼운다.** 먼저 부수면 만들기가 실패했을 때
 * 돌아갈 곳이 없다. 공유 메모리는 익명 매핑이라 둘이 동시에 있어도 부딪히지 않는다.
 */
static int rebase_symbol(live_ctx_t *lc, const char *symbol, price_t ref_price)
{
    if (symbol[0] == '\0' || ref_price < PRICE_MIN || ref_price > PRICE_MAX) {
        return ERR_INVALID_ARG;
    }

    char prev[MSG_SYMBOL_LEN + 1];
    price_t prev_ref = lc->cfg.ref_price;
    snprintf(prev, sizeof(prev), "%s", lc->symbol);

    snprintf(lc->symbol, sizeof(lc->symbol), "%s", symbol);
    lc->cfg.symbol = lc->symbol;
    lc->cfg.ref_price = ref_price;

    ledger_core_t *fresh = ledger_core_create(&lc->cfg);
    if (fresh == NULL) {
        snprintf(lc->symbol, sizeof(lc->symbol), "%s", prev);
        lc->cfg.ref_price = prev_ref;
        return ERR_INVALID_ARG;
    }

    ledger_core_destroy(lc->core);
    lc->core = fresh;
    printf("종목 전환: %s -> %s, 기준가 %d원 (가격대 %d ~ %d원)\n", prev, lc->symbol,
           ref_price, ref_price - ref_price * 3 / 10,
           ref_price + ref_price * 3 / 10);
    fflush(stdout);
    return ERR_OK;
}

/*
 * 전문 하나를 처리한다. 종목 전환만 여기서 답하고 나머지는 코어에 넘긴다 — 코어를
 * **통째로 갈아끼우는** 일이라 코어 안에서 자기를 부술 수는 없다.
 */
static int on_msg(const wire_header_t *hdr, const uint8_t *body, uint8_t *out,
                  size_t out_cap, void *ctx)
{
    live_ctx_t *lc = ctx;

    if (hdr->type == MSG_SYMBOL_SET) {
        msg_symbol_set_t req;
        if (msg_decode_symbol_set(body, hdr->body_len, &req) < 0) {
            return -1;
        }

        /*
         * **기준가 0은 "지금 무엇을 다루고 있나"를 묻는 것이다.** 바꾸지 않는다.
         *
         * 채널계가 다시 뜨면 설정에 적힌 종목을 들고 시작하는데, 그사이 원장은 다른 종목으로
         * 바뀌어 있을 수 있다. 그러면 모든 호가 조회가 빈 호가창을 돌려준다 — 원장은 자기
         * 종목에만 답하기 때문이다. 물어볼 자리가 있어야 그것을 맞출 수 있다.
         */
        msg_symbol_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.code = (req.ref_price == 0) ? ERR_OK
                                        : rebase_symbol(lc, req.symbol, req.ref_price);
        snprintf(ack.symbol, sizeof(ack.symbol), "%s", lc->symbol);
        ack.ref_price = lc->cfg.ref_price;

        /* 시퀀스·시각은 요청이 들고 온 것을 그대로 쓴다(원장 코어와 같다) */
        wire_header_t h;
        memset(&h, 0, sizeof(h));
        h.type = MSG_SYMBOL_ACK;
        h.body_len = MSG_SYMBOL_ACK_LEN;
        h.seq = hdr->seq;
        h.ts = hdr->ts;

        int n = wire_encode_header(&h, out, out_cap);
        if (n < 0) {
            return -1;
        }
        int m = msg_encode_symbol_ack(&ack, out + n, out_cap - (size_t)n);
        if (m < 0) {
            return -1;
        }
        return n + m;
    }

    return ledger_core_handle(hdr, body, out, out_cap, lc->core);
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
    long     ref_price = 0; /* 0이면 기본 기준가를 쓴다 */

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
        if (strcmp(argv[i], "--ref-price") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--ref-price 뒤에 가격이 와야 한다\n");
                return 2;
            }
            ref_price = strtol(argv[i + 1], &end, 10);
            if (end == argv[i + 1] || *end != '\0' ||
                ref_price < (long)PRICE_MIN || ref_price > (long)PRICE_MAX) {
                fprintf(stderr, "--ref-price는 %d~%d 사이여야 한다\n",
                        (int)PRICE_MIN, (int)PRICE_MAX);
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

    ledger_core_config_t cfg_buf = LEDGER_CORE_DEFAULT;
    if (ref_price > 0) {
        cfg_buf.ref_price = (price_t)ref_price;
    }

    ledger_core_t *core = ledger_core_create(&cfg_buf);
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

    const ledger_core_config_t *cfg = &cfg_buf;
    printf("ledgerd 포트 %u 에서 대기 (SIGTERM/SIGINT로 종료)\n",
           (unsigned)listener_port(ln));
    printf("  계좌 %s, 예수금 %lld원, 종목 %s, 기준가 %d원\n", cfg->account,
           (long long)cfg->cash, cfg->symbol, cfg->ref_price);
    /* 호가창이 받는 가격대. 실시세를 심을 때 이 밖의 가격은 버려진다 */
    printf("  다루는 가격대 %d ~ %d원\n", cfg->ref_price - cfg->ref_price * 3 / 10,
           cfg->ref_price + cfg->ref_price * 3 / 10);

    live_ctx_t live = {.core = core, .cfg = cfg_buf, .per_tick = 0};
    snprintf(live.symbol, sizeof(live.symbol), "%s", cfg_buf.symbol);
    live.cfg.symbol = live.symbol;
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
    int conns = listener_run(ln, on_msg, &live);

    printf("접속 %d건 처리 후 종료\n", conns);
    listener_close(ln);
    ledger_core_destroy(live.core);
    return 0;
}
