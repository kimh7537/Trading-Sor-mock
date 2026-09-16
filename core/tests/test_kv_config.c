/*
 * T2-XX key=value 파서 공통화.
 *
 * 형식 규칙(주석·빈 줄·모르는 키·값 끝 글자)은 test_divergent.c와
 * test_be_weights.c가 이미 쓰는 쪽에서 검증한다. 여기서는 공통 함수로
 * 옮기면서 새로 생긴 것만 본다.
 *  1. 표에 따라 필드가 채워지고, 적지 않은 키는 기본값이 남는다
 *  2. 해석기가 돌려준 실패가 그대로 올라온다
 *  3. 정수 범위 — 넘치면 잘라 넣지 않고 거절한다
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "errors.h"
#include "kv_config.h"

#define CFG_PATH "test_kv_config.cfg"

static void write_cfg(const char *text)
{
    FILE *f = fopen(CFG_PATH, "w");
    assert(f != NULL);
    fputs(text, f);
    fclose(f);
}

static int always_fail(const char *val, void *field)
{
    (void)val;
    (void)field;
    return ERR_INVALID_PRICE;
}

static void test_table(void)
{
    int32_t  a = 7;
    int64_t  b = 0;
    uint64_t c = 0;
    const kv_entry_t table[] = {
        {"a", kv_parse_i32, &a},
        {"b", kv_parse_i64, &b},
        {"c", kv_parse_u64, &c},
    };

    write_cfg("# 주석\n\n  b = -5  # 끝 주석\nc=18446744073709551615\r\n");
    assert(kv_config_load(CFG_PATH, table, 3) == ERR_OK);
    assert(a == 7); /* 적지 않았으니 그대로 */
    assert(b == -5);
    assert(c == UINT64_MAX);

    write_cfg("a = 1\nd = 2\n");
    assert(kv_config_load(CFG_PATH, table, 3) == ERR_INVALID_ARG);

    assert(kv_config_load("없는파일.cfg", table, 3) == ERR_NOT_FOUND);
    assert(kv_config_load(NULL, table, 3) == ERR_NULL_PTR);
}

static void test_parse_error_propagates(void)
{
    int32_t          x = 0;
    const kv_entry_t table[] = {{"x", always_fail, &x}};

    write_cfg("x = 1\n");
    assert(kv_config_load(CFG_PATH, table, 1) == ERR_INVALID_PRICE);
}

static void test_ranges(void)
{
    int32_t  i32 = 0;
    int64_t  i64 = 0;
    uint64_t u64 = 0;

    assert(kv_parse_i32("2147483647", &i32) == ERR_OK && i32 == INT32_MAX);
    assert(kv_parse_i32("-2147483648", &i32) == ERR_OK && i32 == INT32_MIN);
    assert(kv_parse_i32("2147483648", &i32) == ERR_INVALID_ARG);
    assert(kv_parse_i32("-2147483649", &i32) == ERR_INVALID_ARG);
    assert(i32 == INT32_MIN); /* 실패하면 쓰지 않는다 */

    assert(kv_parse_i64("9223372036854775808", &i64) == ERR_INVALID_ARG);
    assert(kv_parse_u64("18446744073709551616", &u64) == ERR_INVALID_ARG);
    assert(kv_parse_u64("-1", &u64) == ERR_INVALID_ARG);

    assert(kv_parse_i32("40점", &i32) == ERR_INVALID_ARG);
    assert(kv_parse_i32("", &i32) == ERR_INVALID_ARG);
}

int main(void)
{
    test_table();
    test_parse_error_propagates();
    test_ranges();
    remove(CFG_PATH);
    printf("test_kv_config: OK\n");
    return 0;
}
