/*
 * 원장 데몬.
 *
 *   ledgerd [포트] [--live <초당 주문 수>] [--ref-price <원>] [--strategy <이름>]
 *           [--exchange krx=HOST:PORT,nxt=HOST:PORT]
 *
 * `--exchange`를 주면 **매칭 엔진이 이 프로세스 안에 없다.** 주문은 FEP 세션을 타고
 * 거래소 프로세스(`exchanged`)로 나가고 호가창도 그쪽 것을 물어서 보여 준다(T12-03).
 * 주지 않으면 지금까지와 똑같다 — `bench/results/`의 측정이 그 구성에서 나왔다.
 *
 * 이 구성에서는 **가상 참가자와 실시세가 꺼진다.** 흔들 호가창이 여기 없기 때문이다.
 *
 * 미국 종목으로 바꾸면 가격이 센트 정수가 되고 계좌는 $100,000로 다시 열린다(T10-02).
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
#include "strategy.h"
#include "listener.h"
#include "remote_venue.h"
#include "msg.h"
#include "wire.h"

#define LEDGERD_DEFAULT_PORT 9100

/*
 * 틱 간격의 하한. 이보다 자주 깨우면 초당 주문 수가 많을 때 poll()만 돌린다.
 * 그래서 빠른 쪽은 간격을 줄이는 대신 한 번에 여러 건을 낸다.
 */
#define LEDGERD_TICK_MIN_MS 10

/*
 * 열린 계좌를 기억해 두는 자리(T9-02).
 *
 * 종목을 바꾸면 코어를 통째로 갈아끼운다. 그러면 **그때까지 열린 계좌가 전부
 * 사라진다** — 혼자 쓰던 때는 설정의 데모 계좌가 새 코어에서 다시 열려 티가 나지
 * 않았지만, 사람마다 계좌를 하나씩 여는 지금은 종목 하나 바꾸는 것으로 모두가
 * 거래를 못 하게 된다.
 *
 * 그래서 연 계좌를 여기 적어 두고 새 코어에 다시 연다. **잔고는 처음 입금액으로
 * 돌아간다** — 미체결과 잔고가 초기화되는 것은 종목 전환의 원래 성질이고
 * (앞 종목의 주문을 다른 가격대 호가창에 남겨 둘 자리가 없다), 여기서 바꾸지 않는다.
 */
#define LEDGERD_ACCOUNT_MAX 256

typedef struct {
    char no[MSG_ACCOUNT_LEN + 1];
} opened_account_t;

typedef struct {
    ledger_core_t       *core;
    ledger_core_config_t cfg;
    char                 symbol[MSG_SYMBOL_LEN + 1];
    int32_t              per_tick;

    /*
     * 통화마다 다른 시작 자금(T10-02). 국내는 원, 미국은 센트라 같은 숫자를 쓰면
     * 1억 원짜리 계좌가 미국 종목에서 100만 달러가 된다. 종목을 바꿔 원장을 새로
     * 열 때 그 통화의 금액으로 계좌를 다시 연다.
     */
    int64_t us_cash;

    opened_account_t opened[LEDGERD_ACCOUNT_MAX];
    int32_t          opened_n;

    /*
     * 가상 참가자를 돌릴까(점검). 실시세 모드가 이것을 끈다.
     *
     * **코어가 아니라 데몬이 들고 있다.** 종목을 바꾸면 코어를 통째로 새로 만드는데,
     * 코어에 두면 그때마다 지워져 장이 닫힌 밤에도 호가창이 혼자 걸어간다.
     */
    bool ticks_on;

    /* 거래소 프로세스 접속. NULL이면 매칭 엔진이 이 프로세스 안에 있다 */
    remote_venues_t *remote;
} live_ctx_t;

/*
 * 이미 적어 둔 계좌면 아무것도 하지 않는다. 자리가 없으면 조용히 넘어간다 —
 * 계좌 자체는 코어가 열어 줬고, 못 적는 것은 종목 전환 때 못 살린다는 뜻일 뿐이다.
 */
static void remember_account(live_ctx_t *lc, const char *no)
{
    for (int32_t i = 0; i < lc->opened_n; i++) {
        if (strcmp(lc->opened[i].no, no) == 0) {
            return;
        }
    }
    if (lc->opened_n >= LEDGERD_ACCOUNT_MAX) {
        return;
    }
    snprintf(lc->opened[lc->opened_n].no, sizeof(lc->opened[0].no), "%s", no);
    lc->opened_n++;
}

/* 이 통화에서 계좌를 열 때 넣어 주는 금액. */
static int64_t seed_cash(const live_ctx_t *lc, tick_table_t table)
{
    return (table == TICK_TABLE_US) ? lc->us_cash : lc->cfg.cash;
}

/*
 * 기다리다 심심하면 호가창을 한 틱 움직인다(T8-01).
 *
 * **스위치가 꺼져 있으면 한 틱도 내지 않는다**(점검). 실시세 모드가 그것을 끈다.
 *
 * 예전에는 "스냅샷을 받은 시장"만 건너뛰었는데(`fed[]`), 그 표시는 **코어에 딸려
 * 있어** 종목을 바꿔 코어를 새로 만들면 지워졌다. 장이 닫혀 새 스냅샷이 오지 않으면
 * 다시 세워지지도 않아, 실시세 모드인데 호가창이 혼자 걸어갔다. 스위치를 데몬이
 * 들고 있으면 코어를 몇 번 갈아끼우든 살아남는다.
 */
static void on_idle(void *ctx)
{
    live_ctx_t *lc = ctx;

    /*
     * 거래소 세션에 숨을 불어넣는다(T12-03). **부르지 않으면 말라 죽는다** —
     * 주문 사이가 무응답 한계(15초)보다 길면 다음 주문에서 끊긴 것을 발견한다.
     */
    remote_venues_pump(lc->remote);

    if (!lc->ticks_on || lc->per_tick <= 0) {
        return;
    }
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
static int rebase_symbol(live_ctx_t *lc, const char *symbol, price_t ref_price,
                         tick_table_t table)
{
    if (symbol[0] == '\0' || ref_price < PRICE_MIN || ref_price > PRICE_MAX) {
        return ERR_INVALID_ARG;
    }
    /*
     * 거래소를 따로 띄웠으면 **그쪽이 종목을 들고 있다**(T12-03). 여기서 바꾸면
     * 원장과 거래소가 다른 종목을 말하게 되고, 주문은 전부 ERR_NOT_FOUND가 된다.
     * 종목을 바꾸려면 거래소를 그 종목으로 다시 띄운다.
     */
    if (lc->remote != NULL) {
        return ERR_NOT_SUPPORTED;
    }

    char         prev[MSG_SYMBOL_LEN + 1];
    price_t      prev_ref = lc->cfg.ref_price;
    tick_table_t prev_table = lc->cfg.tick_table;
    snprintf(prev, sizeof(prev), "%s", lc->symbol);

    snprintf(lc->symbol, sizeof(lc->symbol), "%s", symbol);
    lc->cfg.symbol = lc->symbol;
    lc->cfg.ref_price = ref_price;
    lc->cfg.tick_table = table;

    ledger_core_t *fresh = ledger_core_create(&lc->cfg);
    if (fresh == NULL) {
        snprintf(lc->symbol, sizeof(lc->symbol), "%s", prev);
        lc->cfg.ref_price = prev_ref;
        lc->cfg.tick_table = prev_table;
        return ERR_INVALID_ARG;
    }

    /*
     * 새 코어에 계좌를 다시 연다(T9-02). 못 연 계좌가 있어도 전환 자체는 되돌리지
     * 않는다 — 이미 옛 코어는 버릴 참이고, 계좌 하나 때문에 모두를 앞 종목에
     * 묶어 두는 편이 더 나쁘다. 못 연 것은 로그로 남긴다.
     */
    int32_t failed = 0;
    int64_t seed = seed_cash(lc, table);
    for (int32_t i = 0; i < lc->opened_n; i++) {
        if (ledger_core_open_account(fresh, lc->opened[i].no, seed) != ERR_OK) {
            failed++;
        }
    }

    ledger_core_destroy(lc->core);
    lc->core = fresh;
    if (failed > 0) {
        printf("  계좌 %d개를 새 종목에서 열지 못했다\n", failed);
    }
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
        tick_table_t want = (req.kind == MSG_SYMBOL_US) ? TICK_TABLE_US
                                                        : TICK_TABLE_KRX;
        ack.code = (req.ref_price == 0)
                       ? ERR_OK
                       : rebase_symbol(lc, req.symbol, req.ref_price, want);
        snprintf(ack.symbol, sizeof(ack.symbol), "%s", lc->symbol);
        ack.ref_price = lc->cfg.ref_price;
        ack.kind = (lc->cfg.tick_table == TICK_TABLE_US) ? MSG_SYMBOL_US
                                                        : MSG_SYMBOL_KR;

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

    /*
     * 계좌 개설은 코어가 처리하지만, **누가 열렸는지는 여기가 기억한다**(T9-02).
     * 종목을 바꿀 때 새 코어에 다시 열어 줘야 하기 때문이다.
     */
    if (hdr->type == MSG_ACCOUNT_OPEN) {
        msg_account_open_t req;
        if (msg_decode_account_open(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        int n = ledger_core_handle(hdr, body, out, out_cap, lc->core);
        if (n > 0) {
            msg_account_ack_t ack;
            if (msg_decode_account_ack(out + WIRE_HEADER_LEN,
                                       MSG_ACCOUNT_ACK_LEN, &ack) >= 0 &&
                ack.code == ERR_OK) {
                remember_account(lc, req.account);
            }
        }
        return n;
    }

    /* 가상 참가자 스위치. 코어가 아니라 여기서 답한다 — 코어를 갈아끼워도 남는다 */
    if (hdr->type == MSG_TICK_SET) {
        msg_tick_set_t req;
        if (msg_decode_tick_set(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        bool want = req.on != 0;
        if (want != lc->ticks_on) {
            printf("가상 참가자: %s\n", want ? "켠다" : "끈다 (실시세 모드)");
            fflush(stdout);
        }
        lc->ticks_on = want;

        msg_tick_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        /*
         * **스위치가 아니라 "정말 틱이 나가는가"를 답한다**(점검). `--live` 없이 띄우면
         * 유휴 콜백 자체가 등록되지 않아 한 건도 안 나가는데, 스위치만 보면 1이 나가
         * 화면이 "가상 참가자 켜짐"이라고 거짓말을 한다.
         */
        ack.on = (lc->ticks_on && lc->per_tick > 0) ? 1 : 0;
        ack.code = ERR_OK;

        wire_header_t h;
        memset(&h, 0, sizeof(h));
        h.type = MSG_TICK_ACK;
        h.body_len = MSG_TICK_ACK_LEN;
        h.seq = hdr->seq;
        h.ts = hdr->ts;

        int n = wire_encode_header(&h, out, out_cap);
        if (n < 0) {
            return -1;
        }
        int m = msg_encode_tick_ack(&ack, out + n, out_cap - (size_t)n);
        return (m < 0) ? -1 : n + m;
    }

    return ledger_core_handle(hdr, body, out, out_cap, lc->core);
}

/*
 * `--exchange krx=HOST:PORT,nxt=HOST:PORT`를 읽는다.
 *
 * **시장마다 따로 적는다.** 한 주소로 둘을 받으면 그것은 거래소가 아니라 원장이다 —
 * 실제로 KRX와 NXT는 서로 다른 회사이고 접속도 따로다.
 */
static int parse_exchange(const char *spec, remote_venues_t *rv)
{
    char buf[256];
    if (snprintf(buf, sizeof(buf), "%s", spec) >= (int)sizeof(buf)) {
        return ERR_INVALID_ARG;
    }

    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok != NULL;
         tok = strtok_r(NULL, ",", &save)) {
        char *eq = strchr(tok, '=');
        if (eq == NULL) {
            return ERR_INVALID_ARG;
        }
        *eq = '\0';

        market_t m;
        if (strcmp(tok, "krx") == 0) {
            m = MARKET_KRX;
        } else if (strcmp(tok, "nxt") == 0) {
            m = MARKET_NXT;
        } else {
            return ERR_INVALID_ARG;
        }

        /* 뒤에서 찾는다 — 주소에 콜론이 더 있을 수 있다 */
        char *colon = strrchr(eq + 1, ':');
        if (colon == NULL) {
            return ERR_INVALID_ARG;
        }
        *colon = '\0';

        char *end = NULL;
        long  port = strtol(colon + 1, &end, 10);
        if (end == colon + 1 || *end != '\0' || port <= 0 || port > 65535) {
            return ERR_INVALID_ARG;
        }

        int rc = remote_venues_set(rv, m, eq + 1, (uint16_t)port);
        if (rc != ERR_OK) {
            return rc;
        }
    }
    return ERR_OK;
}

/*
 * 이름으로 SOR 전략을 고른다(`--strategy`). 모르는 이름이면 NULL.
 *
 * **기본은 바꾸지 않는다.** 전략이 무엇이었는지가 측정 결과의 전제이므로, 고르지 않으면
 * 지금까지와 같은 BEST_PRICE다. 이 스위치는 "쪼개는 것"을 눈으로 보려고 둔 것이다 —
 * BEST_PRICE는 설계상 이긴 시장 하나에 전량 보내서 주문 해부에 다리가 늘 1개다.
 */
static const exec_strategy_t *strategy_by_name(const char *name)
{
    if (strcmp(name, "best") == 0) {
        return &STRATEGY_BEST_PRICE;
    }
    if (strcmp(name, "split") == 0) {
        return &STRATEGY_SPLIT;
    }
    if (strcmp(name, "sweep") == 0) {
        return &STRATEGY_SWEEP;
    }
    if (strcmp(name, "krx") == 0) {
        return &STRATEGY_KRX_ONLY;
    }
    return NULL;
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
    /*
     * **`--live`는 "실시세"가 아니라 "가상 참가자"다.** 이름이 화면의 실시세 모드와
     * 겹쳐 실제로 두 번 헷갈렸다 — 실시세 모드가 켜면 이쪽이 오히려 꺼진다.
     * 0이면 가상 참가자가 한 건도 내지 않아 호가창이 그대로 선다.
     */
    long     live_rate = 0;
    long     ref_price = 0; /* 0이면 기본 기준가를 쓴다 */
    const exec_strategy_t *strategy = NULL; /* NULL이면 BEST_PRICE */
    const char *exchange_spec = NULL;       /* NULL이면 프로세스 안에서 돈다 */

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
        if (strcmp(argv[i], "--strategy") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "--strategy 뒤에 best|split|sweep|krx 가 와야 한다\n");
                return 2;
            }
            strategy = strategy_by_name(argv[i + 1]);
            if (strategy == NULL) {
                fprintf(stderr, "--strategy는 best|split|sweep|krx 중 하나여야 한다\n");
                return 2;
            }
            i++;
            continue;
        }
        if (strcmp(argv[i], "--exchange") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "--exchange 뒤에 krx=HOST:PORT,nxt=HOST:PORT 가 와야 한다\n");
                return 2;
            }
            exchange_spec = argv[i + 1];
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
    cfg_buf.strategy = strategy;

    /*
     * 거래소에 먼저 붙는다(T12-03). **하나라도 못 붙으면 뜨지 않는다** — 반쪽만
     * 붙은 채 돌면 SOR이 고른 시장에 따라 주문이 되기도 하고 안 되기도 한다.
     */
    remote_venues_t remote;
    remote_venues_init(&remote, cfg_buf.symbol);
    if (exchange_spec != NULL) {
        if (parse_exchange(exchange_spec, &remote) != ERR_OK) {
            fprintf(stderr,
                    "--exchange는 krx=HOST:PORT,nxt=HOST:PORT 꼴이어야 한다\n");
            return 2;
        }
        if (remote_venues_connect(&remote) != ERR_OK) {
            fprintf(stderr, "거래소에 붙지 못했다. exchanged가 떠 있는지 본다\n");
            return 1;
        }
        cfg_buf.remote = &remote;
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
    if (cfg->remote != NULL) {
        printf("  거래소: 다른 프로세스 (FEP 세션). 가상 참가자와 실시세는 꺼진다\n");
        for (int32_t m = 0; m < MARKET_COUNT; m++) {
            if (remote.link[m].configured) {
                printf("    %s -> %s:%u\n", (m == MARKET_KRX) ? "KRX" : "NXT",
                       remote.link[m].host, (unsigned)remote.link[m].port);
            }
        }
    }
    printf("  SOR 전략 %s (자동 주문만 해당)\n",
           strategy_name(cfg->strategy != NULL ? cfg->strategy
                                               : &STRATEGY_BEST_PRICE));
    /* 호가창이 받는 가격대. 실시세를 심을 때 이 밖의 가격은 버려진다 */
    printf("  다루는 가격대 %d ~ %d원\n", cfg->ref_price - cfg->ref_price * 3 / 10,
           cfg->ref_price + cfg->ref_price * 3 / 10);

    /*
     * 미국 종목의 시작 자금(센트). $100,000 — 국내 1억 원과 얼추 같은 무게다.
     * 같은 숫자를 쓰면 1억 "센트"가 되어 100만 달러짜리 계좌가 된다.
     */
    live_ctx_t live = {.core = core,
                       .cfg = cfg_buf,
                       .per_tick = 0,
                       .us_cash = 10000000,
                       .ticks_on = true,
                       .remote = (exchange_spec != NULL) ? &remote : NULL};
    snprintf(live.symbol, sizeof(live.symbol), "%s", cfg_buf.symbol);
    live.cfg.symbol = live.symbol;
    if (live_rate > 0 && live.remote == NULL) {
        int tick_ms = 0;
        live_pace(live_rate, &tick_ms, &live.per_tick);
        listener_set_idle(ln, on_idle, &live, tick_ms);
        printf("  가상 참가자: 시장마다 초당 %ld건 (%dms마다 %d건)\n", live_rate,
               tick_ms, live.per_tick);
    } else if (live.remote != NULL) {
        /* 틱은 안 내지만 **세션에 숨은 불어넣어야 한다**(on_idle 참조) */
        listener_set_idle(ln, on_idle, &live, LEDGERD_TICK_MIN_MS * 10);
        if (live_rate > 0) {
            printf("  --live는 무시한다 — 흔들 호가창이 이 프로세스에 없다\n");
        }
    }
    fflush(stdout);

    /*
     * **접속을 한 번에 하나씩 끝까지 처리한다.** 호가창이 하나여야 해서다
     * (`ledger_core.h`의 설명). 채널계는 접속 풀을 1로 두고 쓴다.
     */
    int conns = listener_run(ln, on_msg, &live);

    printf("접속 %d건 처리 후 종료\n", conns);
    remote_venues_close(&remote);
    listener_close(ln);
    ledger_core_destroy(live.core);
    return 0;
}
