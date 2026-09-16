#include "journal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "errors.h"
#include "wire.h"

struct journal {
    FILE   *fp;
    bool    writable;
    int64_t count;
    /* 재생용 버퍼. 레코드마다 할당하지 않는다. */
    uint8_t buf[JOURNAL_PAYLOAD_MAX];
};

/*
 * CRC32 (IEEE). 표를 만들지 않고 비트로 돈다.
 *
 * ponytail: 바이트당 8번 도는 값이라 표(1KB)를 두면 몇 배 빠르다. 저널이
 * 병목으로 재이면 그때 바꾼다 — 아직 재 본 적이 없다(CLAUDE.md).
 */
uint32_t journal_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            uint32_t mask = (uint32_t)(-(int32_t)(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static journal_t *open_mode(const char *path, const char *mode, bool writable)
{
    if (path == NULL) {
        return NULL;
    }
    journal_t *j = calloc(1, sizeof(*j));
    if (j == NULL) {
        return NULL;
    }
    j->fp = fopen(path, mode);
    if (j->fp == NULL) {
        free(j);
        return NULL;
    }
    j->writable = writable;
    return j;
}

journal_t *journal_create(const char *path)
{
    /* "ab" — 덧붙이기만 한다. 앞을 고칠 방법을 아예 두지 않는다. */
    return open_mode(path, "ab", true);
}

journal_t *journal_open_read(const char *path)
{
    return open_mode(path, "rb", false);
}

void journal_close(journal_t *j)
{
    if (j == NULL) {
        return;
    }
    if (j->fp != NULL) {
        fclose(j->fp);
    }
    free(j);
}

int64_t journal_count(const journal_t *j)
{
    return (j != NULL) ? j->count : 0;
}

int journal_sync(journal_t *j)
{
    if (j == NULL || j->fp == NULL) {
        return ERR_NULL_PTR;
    }
    if (fflush(j->fp) != 0) {
        return ERR_IO;
    }
    /*
     * `fflush`는 커널까지만 민다. 전원이 끊기면 커널 버퍼에 있던 것은 없던
     * 일이 되므로 `fsync`까지 간다.
     */
    if (fsync(fileno(j->fp)) != 0) {
        return ERR_IO;
    }
    return ERR_OK;
}

int journal_append(journal_t *j, uint8_t type, uint64_t seq, int64_t ts,
                   const uint8_t *payload, uint32_t len)
{
    if (j == NULL || j->fp == NULL) {
        return ERR_NULL_PTR;
    }
    if (!j->writable) {
        return ERR_NOT_SUPPORTED;
    }
    if (payload == NULL && len > 0) {
        return ERR_NULL_PTR;
    }
    if (len > JOURNAL_PAYLOAD_MAX) {
        return ERR_INVALID_ARG;
    }

    static uint8_t rec[JOURNAL_HEADER_LEN + JOURNAL_PAYLOAD_MAX +
                       JOURNAL_CRC_LEN];

    memset(rec, 0, JOURNAL_HEADER_LEN);
    wire_put_u32(rec + 0, JOURNAL_MAGIC);
    wire_put_u8(rec + 4, type);
    /* 5..7은 채움. 0으로 둔다 */
    wire_put_u32(rec + 8, len);
    wire_put_u64(rec + 12, seq);
    wire_put_i64(rec + 20, ts);

    if (len > 0) {
        memcpy(rec + JOURNAL_HEADER_LEN, payload, len);
    }

    /*
     * CRC는 **머리와 실을 것 모두**를 덮는다. 실을 것만 덮으면 길이 필드가
     * 깨졌을 때 못 잡는데, 길이가 깨지는 것이 가장 위험하다 — 그 값을 믿고
     * 읽으면 엉뚱한 자리까지 레코드로 삼는다.
     */
    uint32_t crc = journal_crc32(rec, JOURNAL_HEADER_LEN + len);
    wire_put_u32(rec + JOURNAL_HEADER_LEN + len, crc);

    size_t total = JOURNAL_HEADER_LEN + len + JOURNAL_CRC_LEN;

    /*
     * **한 번에 낸다.** 나눠 쓰면 그 사이에 죽었을 때 찢어질 자리가 하나 더
     * 는다. 그래도 한 번의 `fwrite`가 원자적이라는 보장은 없다 — 그래서
     * 재생 쪽이 찢어진 꼬리를 다룬다.
     */
    if (fwrite(rec, 1, total, j->fp) != total) {
        return ERR_IO;
    }

    int rc = journal_sync(j);
    if (rc != ERR_OK) {
        return rc;
    }

    j->count++;
    return ERR_OK;
}

int journal_replay(journal_t *j, journal_fn fn, void *ctx, bool *out_truncated)
{
    if (j == NULL || j->fp == NULL) {
        return ERR_NULL_PTR;
    }
    if (out_truncated != NULL) {
        *out_truncated = false;
    }

    if (fseek(j->fp, 0, SEEK_SET) != 0) {
        return ERR_IO;
    }

    static uint8_t whole[JOURNAL_HEADER_LEN + JOURNAL_PAYLOAD_MAX];
    int            played = 0;

    for (;;) {
        size_t got = fread(whole, 1, JOURNAL_HEADER_LEN, j->fp);

        if (got == 0) {
            break; /* 깨끗하게 끝났다 */
        }
        if (got < JOURNAL_HEADER_LEN) {
            /* 머리조차 다 못 읽었다. 찢어진 꼬리다. */
            if (out_truncated != NULL) {
                *out_truncated = true;
            }
            break;
        }

        if (wire_get_u32(whole + 0) != JOURNAL_MAGIC) {
            /*
             * 남의 파일이거나 앞쪽이 망가졌다. **되맞추려 들지 않는다** —
             * 앞이 어긋났으면 뒤의 경계도 믿을 수 없다(T3-09의 판단과 같다).
             */
            if (out_truncated != NULL) {
                *out_truncated = true;
            }
            break;
        }

        /*
         * **길이가 깨졌으면 그 값을 믿고 읽으면 안 된다.**
         *
         * 한도를 버퍼 크기에서 바로 끌어온다. 상수를 따로 쓰면 버퍼를 키울 때
         * 한쪽만 고치는 일이 생긴다.
         *
         * 이 검사가 없으면 `fread`가 버퍼 뒤를 넘어 쓴다. 그런데 그 넘침은
         * **테스트로도 ASan으로도 보이지 않는다** — 쓰는 주체가 libc 안이라
         * ASan이 가로채지 못하고, 넘쳐 쓴 뒤에도 CRC가 안 맞아 겉보기 결과가
         * 똑같기 때문이다. 변이 검사에서 실제로 살아남았다(J3).
         * **확인할 수 없는 검사지만 지우면 안 되는 것**이다.
         */
        const size_t room = sizeof(whole) - JOURNAL_HEADER_LEN;
        uint32_t     len = wire_get_u32(whole + 8);
        if (len > room) {
            if (out_truncated != NULL) {
                *out_truncated = true;
            }
            break;
        }

        if (len > 0 &&
            fread(whole + JOURNAL_HEADER_LEN, 1, len, j->fp) != len) {
            if (out_truncated != NULL) {
                *out_truncated = true;
            }
            break;
        }

        uint8_t crcbuf[JOURNAL_CRC_LEN];
        if (fread(crcbuf, 1, sizeof(crcbuf), j->fp) != sizeof(crcbuf)) {
            if (out_truncated != NULL) {
                *out_truncated = true;
            }
            break;
        }

        if (journal_crc32(whole, JOURNAL_HEADER_LEN + len) !=
            wire_get_u32(crcbuf)) {
            if (out_truncated != NULL) {
                *out_truncated = true;
            }
            break;
        }

        /* 콜백이 보는 동안 살아 있어야 하므로 따로 담아 둔다. */
        if (len > 0) {
            memcpy(j->buf, whole + JOURNAL_HEADER_LEN, len);
        }

        jrec_t rec;
        rec.type = wire_get_u8(whole + 4);
        rec.len = len;
        rec.seq = wire_get_u64(whole + 12);
        rec.ts = wire_get_i64(whole + 20);
        rec.payload = j->buf;

        if (fn != NULL) {
            int rc = fn(&rec, ctx);
            if (rc != 0) {
                return rc; /* 호출부가 멈추라고 했다 */
            }
        }
        played++;
    }

    return played;
}
