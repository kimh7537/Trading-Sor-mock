#include "divergent.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "tick_size.h"

/*
 * 프리셋 표. 시장 하나의 유동성 성격을 네 숫자로 요약한다.
 *
 *  arrival   — 초당 주문 수. 낮을수록 호가가 얇다
 *  decay     — 가격 이격 감쇠. 낮을수록 넓게 퍼져 스프레드가 벌어진다
 *  qty       — 주문 크기 범위. 작을수록 각 호가의 잔량이 적다
 *  ref_shift — 기준가 이동(틱). CROSSED가 이걸로 두 시장을 어긋나게 만든다
 *
 * "유동성이 얇다"를 도착률만으로 표현하지 않는 이유는, 주문 수가 적어도 각 주문이
 * 크면 체결 단가에는 불리하지 않기 때문이다. 세 축을 함께 움직여야 실제로 불리해진다.
 */
typedef struct {
    double  arrival;
    double  decay;
    qty_t   qty_min;
    qty_t   qty_max;
    int32_t ref_shift_ticks;
} market_preset_t;

typedef struct {
    const char     *name;
    market_preset_t krx;
    market_preset_t nxt;
} scenario_preset_t;

/* CROSSED에서 NXT를 밀어 올릴 폭. 두 시장 호가가 확실히 겹치도록 넉넉히 잡는다. */
#define CROSS_SHIFT_TICKS 20

static const scenario_preset_t PRESETS[] = {
    [SCENARIO_BALANCED] = {"BALANCED",
                           {100.0, 0.5, 10, 500, 0},
                           {100.0, 0.5, 10, 500, 0}},
    [SCENARIO_KRX_THIN] = {"KRX_THIN",
                           {20.0, 0.12, 10, 60, 0},
                           {100.0, 0.5, 10, 500, 0}},
    [SCENARIO_NXT_THIN] = {"NXT_THIN",
                           {100.0, 0.5, 10, 500, 0},
                           {20.0, 0.12, 10, 60, 0}},
    [SCENARIO_CROSSED] = {"CROSSED",
                          {100.0, 1.5, 10, 500, 0},
                          {100.0, 1.5, 10, 500, CROSS_SHIFT_TICKS}},
};

#define PRESET_COUNT (sizeof(PRESETS) / sizeof(PRESETS[0]))

struct divergent {
    divergent_config_t cfg;
    synth_gen_t       *gen[MARKET_COUNT];
    price_t            ref[MARKET_COUNT];
};

const char *scenario_str(scenario_t scenario)
{
    if ((size_t)scenario < PRESET_COUNT && PRESETS[scenario].name != NULL) {
        return PRESETS[scenario].name;
    }
    return "알 수 없는 시나리오";
}

int scenario_from_str(const char *name, scenario_t *out)
{
    if (name == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    for (size_t i = 0; i < PRESET_COUNT; i++) {
        if (PRESETS[i].name != NULL && strcmp(PRESETS[i].name, name) == 0) {
            *out = (scenario_t)i;
            return ERR_OK;
        }
    }
    return ERR_NOT_FOUND;
}

/*
 * 시장별 시드를 파생시킨다. 같은 시드를 두 시장에 그대로 주면 두 시장이 글자
 * 그대로 같은 주문을 내서 어떤 시나리오도 차이를 만들지 못한다.
 * splitmix64 마무리 함수로 섞으므로 인접한 기본 시드도 멀리 떨어진다.
 */
static uint64_t derive_seed(uint64_t base, market_t market)
{
    uint64_t x = base + 0x9E3779B97F4A7C15ULL * ((uint64_t)market + 1);
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x != 0 ? x : 1; /* 시드 0은 생성기가 거절한다 */
}

static price_t shifted_ref(price_t base, int32_t shift_ticks)
{
    if (shift_ticks == 0) {
        return base;
    }
    price_t tick = tick_size_of(base);
    assert(tick > 0);

    int64_t raw = (int64_t)base + (int64_t)shift_ticks * (int64_t)tick;
    if (raw < PRICE_MIN) {
        raw = PRICE_MIN;
    }
    if (raw > PRICE_MAX) {
        raw = PRICE_MAX;
    }
    price_t aligned = round_to_tick((price_t)raw, false);
    return aligned != 0 ? aligned : base;
}

divergent_t *divergent_create(const divergent_config_t *cfg)
{
    if (cfg == NULL || cfg->seed == 0 || cfg->orders_per_market <= 0) {
        return NULL;
    }
    if ((size_t)cfg->scenario >= PRESET_COUNT) {
        return NULL;
    }

    divergent_t *div = calloc(1, sizeof(*div));
    if (div == NULL) {
        return NULL;
    }
    div->cfg = *cfg;

    const scenario_preset_t *ps = &PRESETS[cfg->scenario];
    const market_preset_t *per_market[MARKET_COUNT] = {
        [MARKET_KRX] = &ps->krx,
        [MARKET_NXT] = &ps->nxt,
    };

    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        const market_preset_t *mp = per_market[m];

        synth_config_t sc = {0};
        sc.seed = derive_seed(cfg->seed, (market_t)m);
        sc.arrival_per_sec = mp->arrival;
        sc.price_decay = mp->decay;
        sc.qty_min = mp->qty_min;
        sc.qty_max = mp->qty_max;
        sc.ref_price = shifted_ref(cfg->ref_price, mp->ref_shift_ticks);
        sc.price_low = cfg->price_low;
        sc.price_high = cfg->price_high;
        sc.market = (market_t)m;
        sc.start_ts = cfg->start_ts;
        /*
         * 주문번호 공간을 시장별로 나눈다. 두 시장의 주문이 같은 번호를 쓰면
         * 통합 로그에서 어느 시장 주문인지 구분할 수 없다.
         */
        sc.first_id = (order_id_t)1 + (order_id_t)m * (order_id_t)1000000000;

        div->ref[m] = sc.ref_price;
        div->gen[m] = synth_create(&sc);
        if (div->gen[m] == NULL) {
            divergent_destroy(div);
            return NULL;
        }
    }

    return div;
}

void divergent_destroy(divergent_t *div)
{
    if (div == NULL) {
        return;
    }
    for (int32_t m = 0; m < MARKET_COUNT; m++) {
        synth_destroy(div->gen[m]);
    }
    free(div);
}

synth_gen_t *divergent_gen(divergent_t *div, market_t market)
{
    if (div == NULL || (int32_t)market < 0 || (int32_t)market >= MARKET_COUNT) {
        return NULL;
    }
    return div->gen[market];
}

price_t divergent_ref_price(const divergent_t *div, market_t market)
{
    if (div == NULL || (int32_t)market < 0 || (int32_t)market >= MARKET_COUNT) {
        return 0;
    }
    return div->ref[market];
}

/* --- 설정 파일 --- */

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    char *end = s + strlen(s);
    while (end > s) {
        char c = end[-1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            end--;
        } else {
            break;
        }
    }
    *end = '\0';
    return s;
}

/* 정수 하나. 남는 글자가 있으면 실패다 — "10000원" 같은 값을 걸러낸다. */
static int parse_i64(const char *s, int64_t *out)
{
    char *end = NULL;
    long long v = strtoll(s, &end, 10);

    if (end == s || *end != '\0') {
        return ERR_INVALID_ARG;
    }
    *out = (int64_t)v;
    return ERR_OK;
}

static int parse_u64(const char *s, uint64_t *out)
{
    char *end = NULL;

    if (*s == '-') {
        return ERR_INVALID_ARG;
    }
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s || *end != '\0') {
        return ERR_INVALID_ARG;
    }
    *out = (uint64_t)v;
    return ERR_OK;
}

int divergent_load(const char *path, divergent_config_t *out)
{
    if (path == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return ERR_NOT_FOUND;
    }

    divergent_config_t cfg = {0};
    char line[512];
    int rc = ERR_OK;

    while (fgets(line, sizeof(line), f) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *body = trim(line);
        if (*body == '\0') {
            continue;
        }

        char *eq = strchr(body, '=');
        if (eq == NULL) {
            rc = ERR_INVALID_ARG;
            break;
        }
        *eq = '\0';
        char *key = trim(body);
        char *val = trim(eq + 1);
        if (*key == '\0' || *val == '\0') {
            rc = ERR_INVALID_ARG;
            break;
        }

        int64_t n = 0;
        if (strcmp(key, "scenario") == 0) {
            rc = scenario_from_str(val, &cfg.scenario);
            if (rc == ERR_NOT_FOUND) {
                rc = ERR_INVALID_ARG;
            }
        } else if (strcmp(key, "seed") == 0) {
            rc = parse_u64(val, &cfg.seed);
        } else if (strcmp(key, "ref_price") == 0) {
            rc = parse_i64(val, &n);
            cfg.ref_price = (price_t)n;
        } else if (strcmp(key, "price_low") == 0) {
            rc = parse_i64(val, &n);
            cfg.price_low = (price_t)n;
        } else if (strcmp(key, "price_high") == 0) {
            rc = parse_i64(val, &n);
            cfg.price_high = (price_t)n;
        } else if (strcmp(key, "start_ts") == 0) {
            rc = parse_i64(val, &n);
            cfg.start_ts = (ts_t)n;
        } else if (strcmp(key, "orders_per_market") == 0) {
            rc = parse_i64(val, &n);
            cfg.orders_per_market = (int32_t)n;
        } else {
            /* 오타 난 키를 조용히 넘기면 그 설정으로 돌린 실험을 해석할 수 없다. */
            rc = ERR_INVALID_ARG;
        }

        if (rc != ERR_OK) {
            break;
        }
    }

    fclose(f);

    if (rc == ERR_OK) {
        *out = cfg;
    }
    return rc;
}
