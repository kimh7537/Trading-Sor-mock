/*
 * T12-04 프로세스 경계의 값 — FEP를 지나는 주문은 얼마나 비싼가.
 *
 * ===========================================================================
 * 무엇을 재는가
 * ===========================================================================
 *
 * `bench_pipeline`(T5-05)은 주문 한 건이 원장 **안에서** 지나는 일곱 단계를
 * 나눠 잰다. 그 측정은 `--exchange` 구성에 그대로 쓸 수 없다 — 뒤쪽 네 단계가
 * 이 프로세스에 없기 때문이다. 다른 것을 잰 숫자를 같은 표에 넣으면 안 된다.
 *
 * 그래서 여기서는 **하나만** 잰다.
 *
 *   집행기에 다리 하나를 넘겨 결과가 돌아오기까지
 *
 * 같은 주문을 두 경로로 보낸다.
 *
 *   in-process : 이 프로세스 안의 매칭 엔진을 직접 부른다 (기본 구성)
 *   FEP        : 전문을 만들어 소켓으로 보내고 응답을 기다린다 (`--exchange`)
 *
 * 두 경로의 차이가 곧 **전문 조립 + 소켓 왕복 + 저쪽 프로세스의 처리**다.
 * 그것이 `bench/results/pipeline-*.md`가 "이 숫자에 빠져 있다"고 적어 둔 몫이다.
 *
 * ===========================================================================
 * 체결시키지 않는다
 * ===========================================================================
 *
 * **호가창에 걸리기만 하는 주문**을 쓴다(최우선 매수보다 한참 아래의 매수 지정가).
 * 체결을 섞으면 두 경로가 서로 다른 호가창을 소진하게 되고, 그러면 재는 것이
 * 경계 비용인지 체결 깊이의 차이인지 갈리지 않는다.
 *
 * 그 대신 이 숫자는 **여러 가격대를 먹는 주문의 비용을 말하지 않는다.** 체결이
 * 많은 주문은 양쪽 다 더 걸리고, 경계 비용의 비중은 그만큼 줄어든다.
 *
 * ===========================================================================
 * 이 측정이 덮지 않는 것
 * ===========================================================================
 *
 *  - **같은 기계 안의 loopback이다.** 실제 증권사의 FEP는 회선 건너에 있고
 *    거기서는 이 숫자가 의미를 잃는다. 여기서 말할 수 있는 것은 "프로세스를
 *    나누는 것 자체가 얼마인가"까지다
 *  - **전략 비교(가격)에는 쓸 수 없다.** 원격 경로는 하트비트 때문에 시스템
 *    시각을 읽어 재현되지 않는다. 가격 비교는 in-process로 계속 잰다
 *  - 예열을 빼고 분위수로 적는다. 평균은 꼬리를 감춘다(T1-20과 같은 이유)
 */
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "divergent.h"
#include "errors.h"
#include "executor.h"
#include "match.h"
#include "order_map.h"
#include "remote_venue.h"
#include "strategy.h"

#ifndef EXCHANGED_BIN
#error "CMake가 exchanged 실행 파일 경로를 넣어 줘야 한다"
#endif

#define WARMUP 200
#define MEASURED 2000

/* 거래소와 이쪽 호가창이 **같은 시드로 같은 모양**이어야 비교가 성립한다 */
#define REF_PRICE 70000
#define LIQUIDITY 1000
#define SEED 20260917u
#define SYMBOL "005930"

/* 최우선 매수보다 한참 아래. 걸리기만 하고 체결되지 않는다 */
#define REST_PRICE (REF_PRICE - REF_PRICE / 5)

#define SPAWN_WAIT_MS 5000

/*
 * 논리 주문번호의 시작값.
 *
 * **1부터 세면 안 된다.** 물리 주문번호는 `논리 x 16 + 시장 + 1`이라 작은 논리
 * 번호는 시드 유동성이 이미 쓴 번호와 부딪힌다(`ERR_DUPLICATE`). 그러면 거부된
 * 주문이 섞여 두 경로가 **같은 일을 하지 않게** 된다.
 */
#define ORDER_BASE 1000000

static int64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static int64_t mono_ms(void) { return now_ns() / 1000000; }

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a;
    int64_t y = *(const int64_t *)b;
    return (x < y) ? -1 : ((x > y) ? 1 : 0);
}

static int64_t percentile(const int64_t *sorted, size_t n, double p)
{
    if (n == 0) {
        return 0;
    }
    size_t idx = (size_t)(p * (double)(n - 1) + 0.5);
    return sorted[idx];
}

/* --- 거래소 프로세스 --- */

/*
 * 비어 있는 포트를 하나 고른다. 0을 돌려주면 못 고른 것이다.
 *
 * **`assert` 안에서 시스템 호출을 하지 않는다.** 이 파일은 Release(NDEBUG)로
 * 도는데 그러면 `assert(bind(...))`의 `bind`가 통째로 사라진다.
 */
static uint16_t free_port(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return 0;
    }

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    socklen_t len = sizeof(a);
    uint16_t  port = 0;
    if (bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0 &&
        getsockname(fd, (struct sockaddr *)&a, &len) == 0) {
        port = ntohs(a.sin_port);
    }
    close(fd);
    return port;
}

static pid_t spawn_exchange(uint16_t port, const char *market)
{
    char portbuf[8];
    char refbuf[16];
    char liqbuf[16];
    char seedbuf[24];
    snprintf(portbuf, sizeof(portbuf), "%u", (unsigned)port);
    snprintf(refbuf, sizeof(refbuf), "%d", REF_PRICE);
    snprintf(liqbuf, sizeof(liqbuf), "%d", LIQUIDITY);
    snprintf(seedbuf, sizeof(seedbuf), "%u", SEED);

    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            (void)dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
        execl(EXCHANGED_BIN, "exchanged", portbuf, "--market", market,
              "--ref-price", refbuf, "--liquidity", liqbuf, "--seed", seedbuf,
              "--symbol", SYMBOL, (char *)NULL);
        _exit(127);
    }
    return pid;
}

/* --- 잴 주문 --- */

/*
 * 걸리기만 하는 매수 지정가. 번호만 다르고 나머지는 같다 — 비교하려면 두 경로가
 * **같은 일**을 해야 한다.
 */
static void make_order(order_t *o, exec_plan_t *plan, order_id_t id)
{
    memset(o, 0, sizeof(*o));
    o->id = id;
    o->side = SIDE_BUY;
    o->type = ORDER_LIMIT;
    o->price = REST_PRICE;
    o->qty = 10;
    o->ts = (ts_t)id;
    o->market = MARKET_KRX;

    plan_init(plan);
    (void)plan_add_leg(plan, MARKET_KRX, o->qty, REST_PRICE, ORDER_LIMIT);
}

typedef struct {
    int64_t p50;
    int64_t p90;
    int64_t p99;
    int64_t min;
    int32_t rejected;
} result_t;

static int64_t g_samples[MEASURED];

static void summarize(size_t n, int32_t rejected, result_t *out)
{
    qsort(g_samples, n, sizeof(g_samples[0]), cmp_i64);
    out->p50 = percentile(g_samples, n, 0.50);
    out->p90 = percentile(g_samples, n, 0.90);
    out->p99 = percentile(g_samples, n, 0.99);
    out->min = g_samples[0];
    out->rejected = rejected;
}

/* --- in-process --- */

static int run_local(result_t *out)
{
    divergent_config_t d;
    memset(&d, 0, sizeof(d));
    d.scenario = SCENARIO_BALANCED;
    d.seed = SEED;
    d.ref_price = REF_PRICE;
    d.tick_table = TICK_TABLE_KRX;
    d.price_low = REF_PRICE - REF_PRICE * 3 / 10;
    d.price_high = REF_PRICE + REF_PRICE * 3 / 10;
    d.orders_per_market = LIQUIDITY;

    divergent_t *div = divergent_create(&d);
    if (div == NULL) {
        return ERR_INVALID_ARG;
    }

    int32_t         cap = LIQUIDITY + WARMUP + MEASURED + 64;
    match_engine_t *eng = match_engine_create(REF_PRICE, cap);
    if (eng == NULL) {
        divergent_destroy(div);
        return ERR_POOL_EXHAUSTED;
    }

    synth_gen_t *gen = divergent_gen(div, MARKET_KRX);
    for (int32_t i = 0; i < LIQUIDITY; i++) {
        order_t       o;
        exec_result_t res;
        if (synth_next(gen, &o) != ERR_OK) {
            break;
        }
        (void)match_limit(eng, &o, &res);
    }

    venues_t venues;
    memset(&venues, 0, sizeof(venues));
    venues.eng[MARKET_KRX] = eng;

    order_map_t *map = omap_create(WARMUP + MEASURED + 8);
    if (map == NULL) {
        match_engine_destroy(eng);
        divergent_destroy(div);
        return ERR_POOL_EXHAUSTED;
    }

    int32_t rejected = 0;
    for (int32_t i = 0; i < WARMUP + MEASURED; i++) {
        order_t       o;
        exec_plan_t   plan;
        exec_report_t rep;
        make_order(&o, &plan, (order_id_t)(ORDER_BASE + i));

        int64_t t0 = now_ns();
        int     rc = exec_submit(map, &venues, &o, &plan, &rep);
        int64_t dt = now_ns() - t0;

        if (rc != ERR_OK) {
            if (rejected == 0) {
                fprintf(stderr, "in-process 첫 거부: %d건째 %s\n", i + 1,
                        err_str(rc));
            }
            rejected++;
        }
        if (i >= WARMUP) {
            g_samples[i - WARMUP] = dt;
        }
    }

    summarize(MEASURED, rejected, out);

    omap_destroy(map);
    match_engine_destroy(eng);
    divergent_destroy(div);
    return ERR_OK;
}

/* --- FEP를 지나는 길 --- */

static int run_remote(result_t *out, pid_t *out_pid)
{
    uint16_t port = free_port();
    if (port == 0) {
        return ERR_IO;
    }
    *out_pid = spawn_exchange(port, "krx");
    if (*out_pid < 0) {
        return ERR_IO;
    }

    remote_venues_t rv;
    remote_venues_init(&rv, SYMBOL);
    if (remote_venues_set(&rv, MARKET_KRX, "127.0.0.1", port) != ERR_OK) {
        return ERR_INVALID_ARG;
    }

    int64_t deadline = mono_ms() + SPAWN_WAIT_MS;
    while (remote_venues_connect(&rv) != ERR_OK) {
        if (mono_ms() >= deadline) {
            return ERR_IO;
        }
    }

    order_map_t *map = omap_create(WARMUP + MEASURED + 8);
    if (map == NULL) {
        remote_venues_close(&rv);
        return ERR_POOL_EXHAUSTED;
    }

    int32_t rejected = 0;
    for (int32_t i = 0; i < WARMUP + MEASURED; i++) {
        order_t       o;
        exec_plan_t   plan;
        exec_report_t rep;
        make_order(&o, &plan, (order_id_t)(ORDER_BASE + i));

        int64_t t0 = now_ns();
        int     rc = remote_submit(&rv, map, &o, &plan, &rep);
        int64_t dt = now_ns() - t0;

        if (rc != ERR_OK) {
            rejected++;
        }
        if (i >= WARMUP) {
            g_samples[i - WARMUP] = dt;
        }
    }

    summarize(MEASURED, rejected, out);

    omap_destroy(map);
    remote_venues_close(&rv);
    return ERR_OK;
}

static void cpu_model(char *buf, size_t cap)
{
    snprintf(buf, cap, "%s", "unknown");
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f == NULL) {
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "model name", 10) != 0) {
            continue;
        }
        char *colon = strchr(line, ':');
        if (colon != NULL) {
            char *p = colon + 1;
            while (*p == ' ') {
                p++;
            }
            size_t n = strlen(p);
            while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == ' ')) {
                p[--n] = '\0';
            }
            snprintf(buf, cap, "%s", p);
        }
        break;
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *date = (argc > 1) ? argv[1] : "unknown";
    const char *out_path = (argc > 2) ? argv[2] : NULL;

    /* 거래소가 먼저 끊어도 죽지 않는다 */
    signal(SIGPIPE, SIG_IGN);

    result_t local;
    result_t remote;
    memset(&local, 0, sizeof(local));
    memset(&remote, 0, sizeof(remote));

    int rc = run_local(&local);
    if (rc != ERR_OK) {
        fprintf(stderr, "in-process 판 실패: %s\n", err_str(rc));
        return 1;
    }

    pid_t ex = -1;
    rc = run_remote(&remote, &ex);
    if (ex > 0) {
        (void)kill(ex, SIGTERM);
        int st = 0;
        (void)waitpid(ex, &st, 0);
    }
    if (rc != ERR_OK) {
        fprintf(stderr, "FEP 판 실패: %s\n", err_str(rc));
        return 1;
    }

    char cpu[128];
    cpu_model(cpu, sizeof(cpu));

    FILE *f = stdout;
    if (out_path != NULL) {
        f = fopen(out_path, "w");
        if (f == NULL) {
            fprintf(stderr, "결과 파일을 못 엽니다: %s\n", out_path);
            return 1;
        }
    }

    double ratio = (local.p50 > 0) ? (double)remote.p50 / (double)local.p50 : 0.0;

    fprintf(f, "# 프로세스 경계의 값 (T12-04)\n\n");
    fprintf(f,
            "주문 한 건이 **집행기에 들어가 결과가 돌아오기까지**를 두 경로로 잰다.\n"
            "`bench_pipeline`(T5-05)의 일곱 단계 표와 **다른 것을 잰 숫자다** — 그쪽은\n"
            "원장 안의 단계를 나눈 것이고, 여기는 프로세스를 나눈 값을 본다.\n\n");

    fprintf(f, "## 측정 조건\n\n");
    fprintf(f, "- 날짜: %s\n", date);
    fprintf(f, "- CPU: %s\n", cpu);
    fprintf(f, "- 빌드: Release, 단일 스레드\n");
    fprintf(f, "- 예열 %d건(측정 제외), 측정 %d건\n", WARMUP, MEASURED);
    fprintf(f, "- 유동성: %d건, 시나리오 BALANCED, 시드 %u (양쪽 같다)\n", LIQUIDITY,
            SEED);
    fprintf(f,
            "- 주문: 매수 지정가 %d원 x 10주 — **체결되지 않고 호가창에 걸리기만"
            " 한다**\n",
            REST_PRICE);
    fprintf(f, "- 거래소: 같은 기계의 다른 프로세스, TCP loopback\n\n");

    fprintf(f, "## 결과\n\n");
    fprintf(f, "| | in-process | FEP 경유 | 배수 |\n");
    fprintf(f, "|---|---:|---:|---:|\n");
    fprintf(f, "| p50 | %lld ns | %lld ns | %.0f배 |\n", (long long)local.p50,
            (long long)remote.p50, ratio);
    fprintf(f, "| p90 | %lld ns | %lld ns | |\n", (long long)local.p90,
            (long long)remote.p90);
    fprintf(f, "| p99 | %lld ns | %lld ns | |\n", (long long)local.p99,
            (long long)remote.p99);
    fprintf(f, "| 최소 | %lld ns | %lld ns | |\n", (long long)local.min,
            (long long)remote.min);
    fprintf(f, "| 거부 | %d건 | %d건 | |\n\n", local.rejected, remote.rejected);

    fprintf(f, "## 읽는 법\n\n");
    fprintf(f,
            "프로세스를 나누는 값은 **p50 기준 %lld ns**다(%lld -> %lld). 그 안에\n"
            "전문 조립·소켓 왕복·저쪽 프로세스의 매칭이 전부 들어 있다.\n\n",
            (long long)(remote.p50 - local.p50), (long long)local.p50,
            (long long)remote.p50);
    fprintf(f,
            "`pipeline-2026-09-16.md`가 \"이 숫자에 전문 송수신과 프로세스 경계"
            " 비용은\n빠져 있다\"고 적어 둔 몫이 이만큼이다. 원장 안의 일곱 단계를"
            " 다 합친 것보다\n크다면, 이 구성에서 고칠 곳은 **원장 안이 아니다.**\n\n");

    fprintf(f, "## 이 숫자가 말하지 않는 것\n\n");
    fprintf(f,
            "- **체결이 많은 주문**은 양쪽 다 더 걸리고 경계 비용의 비중은 줄어든다.\n"
            "  여기서는 걸리기만 하는 주문으로 경계만 떼어 냈다\n"
            "- **같은 기계 안의 loopback이다.** 회선 건너의 FEP에서는 이 숫자가\n"
            "  의미를 잃는다 — 말할 수 있는 것은 프로세스를 나누는 값까지다\n"
            "- **전략 비교(가격)에는 쓸 수 없다.** 원격 경로는 하트비트 때문에\n"
            "  시스템 시각을 읽어 재현되지 않는다. 가격 비교는 in-process로 잰다\n");

    if (out_path != NULL) {
        fclose(f);
        fprintf(stderr, "결과를 %s에 적었습니다\n", out_path);
    }
    return 0;
}
