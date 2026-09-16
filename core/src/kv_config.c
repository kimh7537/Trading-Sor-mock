#include "kv_config.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"

static char *kv_trim(char *s)
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

static const kv_entry_t *kv_find(const kv_entry_t *table, size_t count,
                                 const char *key)
{
    for (size_t i = 0; i < count; i++) {
        if (strcmp(table[i].key, key) == 0) {
            return &table[i];
        }
    }
    return NULL;
}

int kv_config_load(const char *path, const kv_entry_t *table, size_t count)
{
    if (path == NULL || (table == NULL && count > 0)) {
        return ERR_NULL_PTR;
    }

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return ERR_NOT_FOUND;
    }

    char line[512];
    int  rc = ERR_OK;

    while (fgets(line, sizeof(line), f) != NULL) {
        char *hash = strchr(line, '#');
        if (hash != NULL) {
            *hash = '\0';
        }
        char *body = kv_trim(line);
        if (*body == '\0') {
            continue;
        }

        char *eq = strchr(body, '=');
        if (eq == NULL) {
            rc = ERR_INVALID_ARG;
            break;
        }
        *eq = '\0';
        char *key = kv_trim(body);
        char *val = kv_trim(eq + 1);
        if (*key == '\0' || *val == '\0') {
            rc = ERR_INVALID_ARG;
            break;
        }

        const kv_entry_t *e = kv_find(table, count, key);
        if (e == NULL) {
            rc = ERR_INVALID_ARG; /* 오타를 조용히 넘기지 않는다 */
            break;
        }
        assert(e->parse != NULL && e->field != NULL);
        rc = e->parse(val, e->field);
        if (rc != ERR_OK) {
            break;
        }
    }

    fclose(f);
    return rc;
}

int kv_parse_i64(const char *val, void *field)
{
    char *end = NULL;

    errno = 0;
    long long v = strtoll(val, &end, 10);
    if (end == val || *end != '\0' || errno == ERANGE) {
        return ERR_INVALID_ARG;
    }
    *(int64_t *)field = (int64_t)v;
    return ERR_OK;
}

int kv_parse_i32(const char *val, void *field)
{
    int64_t v = 0;
    int     rc = kv_parse_i64(val, &v);

    if (rc != ERR_OK) {
        return rc;
    }
    if (v < INT32_MIN || v > INT32_MAX) {
        return ERR_INVALID_ARG;
    }
    *(int32_t *)field = (int32_t)v;
    return ERR_OK;
}

int kv_parse_u64(const char *val, void *field)
{
    char *end = NULL;

    if (*val == '-') {
        return ERR_INVALID_ARG; /* strtoull은 음수를 조용히 뒤집는다 */
    }
    errno = 0;
    unsigned long long v = strtoull(val, &end, 10);
    if (end == val || *end != '\0' || errno == ERANGE) {
        return ERR_INVALID_ARG;
    }
    *(uint64_t *)field = (uint64_t)v;
    return ERR_OK;
}
