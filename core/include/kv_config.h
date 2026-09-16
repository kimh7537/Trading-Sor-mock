#ifndef MINI_SOR_KV_CONFIG_H
#define MINI_SOR_KV_CONFIG_H

#include <stddef.h>

/*
 * `key = value` 설정 파일 파서.
 *
 * T1-18(divergent)과 T2-04(best_execution)가 같은 파서를 따로 들고 있던 것을
 * 모았다. 규칙은 두 곳의 것을 그대로 옮겼다.
 *
 *  - `#` 뒤는 주석, 빈 줄은 건너뛴다
 *  - `=`가 없거나 키·값이 비면 거절한다
 *  - **모르는 키는 거절한다.** 오타를 조용히 넘기면 그 설정으로 돌린 실험을
 *    나중에 해석할 수 없다
 *  - 값 뒤에 남는 글자가 있으면 거절한다 ("10000원")
 *
 * 적지 않은 키는 건드리지 않는다. 기본값은 호출자가 field에 미리 넣어 둔다.
 * 첫 실패에서 멈추므로, 실패하면 field 일부만 바뀌어 있을 수 있다 —
 * 호출자는 지역 사본에 읽고 성공했을 때만 내보낸다.
 */

/* val을 해석해 field에 쓴다. 성공 ERR_OK, 실패 음수. */
typedef int (*kv_parse_fn)(const char *val, void *field);

typedef struct {
    const char  *key;
    kv_parse_fn  parse;
    void        *field; /* parse가 값을 써 넣을 곳. 소유하지 않는다 */
} kv_entry_t;

/*
 * path를 읽어 table에 따라 채운다.
 * 파일이 없으면 ERR_NOT_FOUND, 형식 위반·모르는 키는 ERR_INVALID_ARG,
 * parse가 돌려준 실패는 그대로 돌려준다.
 */
int kv_config_load(const char *path, const kv_entry_t *table, size_t count);

/* 기본 해석기. field는 각각 int32_t* / int64_t* / uint64_t*. */
int kv_parse_i32(const char *val, void *field);
int kv_parse_i64(const char *val, void *field);
int kv_parse_u64(const char *val, void *field);

#endif /* MINI_SOR_KV_CONFIG_H */
