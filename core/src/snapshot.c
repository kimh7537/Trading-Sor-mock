#include "snapshot.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "errors.h"
#include "wire.h"

/* 재생 중에 "어디까지 건너뛸지"를 들고 다니는 상태. */
typedef struct {
    const snap_apply_fn *fns;
    uint64_t             from_seq; /* 이 번호 이하는 이미 스냅샷에 있다 */
    int                  applied;
} replay_ctx_t;

int snapshot_write(const char *path, uint64_t upto_seq, const uint8_t *data,
                   uint32_t len)
{
    if (path == NULL || (data == NULL && len > 0)) {
        return ERR_NULL_PTR;
    }
    if (len > SNAPSHOT_MAX) {
        return ERR_INVALID_ARG;
    }

    /*
     * 같은 디렉터리에 임시 파일을 만든다. **다른 곳에 만들면 `rename`이 파일
     * 시스템을 건너뛰게 되어 원자성을 잃는다.**
     */
    char tmp[4096];
    int  n = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof(tmp)) {
        return ERR_INVALID_ARG;
    }

    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        return ERR_IO;
    }

    static uint8_t whole[SNAPSHOT_HEADER_LEN + SNAPSHOT_MAX];
    memset(whole, 0, SNAPSHOT_HEADER_LEN);
    wire_put_u32(whole + 0, SNAPSHOT_MAGIC);
    wire_put_u32(whole + 4, len);
    wire_put_u64(whole + 8, upto_seq);
    if (len > 0) {
        memcpy(whole + SNAPSHOT_HEADER_LEN, data, len);
    }

    /* CRC는 머리와 본문 모두를 덮는다(저널과 같은 이유). */
    uint8_t crcbuf[4];
    wire_put_u32(crcbuf, journal_crc32(whole, SNAPSHOT_HEADER_LEN + len));

    size_t total = SNAPSHOT_HEADER_LEN + len;
    bool   ok = fwrite(whole, 1, total, f) == total &&
              fwrite(crcbuf, 1, sizeof(crcbuf), f) == sizeof(crcbuf) &&
              fflush(f) == 0 && fsync(fileno(f)) == 0;
    /*
     * `fsync`를 빼도 테스트는 통과한다(변이 S12). 차이가 드러나려면 전원이
     * 끊겨야 하는데 테스트가 그것을 만들 수 없다. **그래도 뺄 수 없다** —
     * `rename`이 원자적인 것은 "이름을 바꾸는 일"이지 "내용이 디스크에 닿는
     * 일"이 아니다. 밀어 놓지 않고 이름만 바꾸면 빈 파일이 새 스냅샷이 된다.
     */

    if (fclose(f) != 0) {
        ok = false;
    }
    if (!ok) {
        /*
         * 쓰다 실패했다. 반쪽 임시 파일을 남기지 않는다.
         *
         * 이 자리는 테스트로 오게 할 수 없다 — 디스크가 차거나 장치가 죽어야
         * 한다. 변이 S11로 확인했다. **그래도 지우지 않는다**: 원래 자리는
         * `rename`이 지키지만 임시 파일은 아무도 안 치운다.
         */
        remove(tmp);
        return ERR_IO;
    }

    /*
     * **여기서 원자성이 생긴다.** 이 줄 앞에서 죽으면 옛 스냅샷이 그대로 있고,
     * 뒤에서 죽으면 새 스냅샷이 온전히 있다. 반쪽인 적이 없다.
     */
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return ERR_IO;
    }

    return ERR_OK;
}

int snapshot_read(const char *path, uint64_t *out_seq, uint8_t *out,
                  uint32_t cap)
{
    if (path == NULL || out_seq == NULL) {
        return ERR_NULL_PTR;
    }

    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return ERR_NOT_FOUND;
    }

    static uint8_t whole[SNAPSHOT_HEADER_LEN + SNAPSHOT_MAX];
    int            rc = ERR_IO;

    if (fread(whole, 1, SNAPSHOT_HEADER_LEN, f) != SNAPSHOT_HEADER_LEN) {
        goto done; /* 머리조차 없다 */
    }
    if (wire_get_u32(whole + 0) != SNAPSHOT_MAGIC) {
        goto done;
    }

    {
        /* 한도를 버퍼 크기에서 끌어온다(T5-01에서 배운 대로). */
        const size_t room = sizeof(whole) - SNAPSHOT_HEADER_LEN;
        uint32_t     len = wire_get_u32(whole + 4);

        /*
         * **길이가 깨졌으면 그 값을 믿고 읽으면 안 된다.** 없으면 `fread`가
         * 버퍼 뒤를 넘어 쓴다.
         *
         * 이 검사는 지워도 테스트가 통과한다(변이 S5). 겉보기 결과가 똑같기
         * 때문이다 — 넘쳐 쓴 뒤에도 CRC가 안 맞아 결국 ERR_IO다. 넘치는 주체가
         * libc 안이라 ASan도 가로채지 못한다. 저널의 J3와 같은 자리다.
         * **확인할 수 없지만 지우면 안 되는 검사다.**
         */
        if (len > room) {
            goto done;
        }
        if (len > 0 && fread(whole + SNAPSHOT_HEADER_LEN, 1, len, f) != len) {
            goto done; /* 쓰다 죽은 것이다 */
        }

        uint8_t crcbuf[4];
        if (fread(crcbuf, 1, sizeof(crcbuf), f) != sizeof(crcbuf)) {
            goto done;
        }
        if (journal_crc32(whole, SNAPSHOT_HEADER_LEN + len) !=
            wire_get_u32(crcbuf)) {
            goto done;
        }

        if (len > cap || (len > 0 && out == NULL)) {
            rc = ERR_INVALID_ARG;
            goto done;
        }

        if (len > 0) {
            memcpy(out, whole + SNAPSHOT_HEADER_LEN, len);
        }
        *out_seq = wire_get_u64(whole + 8);
        rc = (int)len;
    }

done:
    fclose(f);
    return rc;
}

static int on_journal_rec(const jrec_t *rec, void *ctx)
{
    replay_ctx_t *r = ctx;

    /*
     * **스냅샷에 이미 들어 있는 것은 건너뛴다.** 이것을 빼먹으면 같은 주문이
     * 두 번 반영되고, 그 틀림은 장이 끝난 뒤 잔고가 안 맞을 때에야 드러난다.
     */
    if (rec->seq <= r->from_seq) {
        return 0;
    }

    if (r->fns->apply != NULL) {
        int rc = r->fns->apply(rec, r->fns->ctx);
        if (rc != 0) {
            return rc;
        }
    }
    r->applied++;
    return 0;
}

int recover(const char *snapshot_path, const char *journal_path,
            const snap_apply_fn *fns, recover_result_t *out)
{
    if (journal_path == NULL || fns == NULL) {
        return ERR_NULL_PTR;
    }

    recover_result_t res;
    memset(&res, 0, sizeof(res));

    static uint8_t body[SNAPSHOT_MAX];
    uint64_t       upto = 0;

    if (snapshot_path != NULL) {
        int n = snapshot_read(snapshot_path, &upto, body, sizeof(body));
        if (n >= 0) {
            if (fns->load != NULL) {
                int rc = fns->load(body, (uint32_t)n, fns->ctx);
                if (rc != 0) {
                    return rc;
                }
            }
            res.used_snapshot = true;
            res.from_seq = upto;
        } else if (n != ERR_NOT_FOUND) {
            /*
             * 있었는데 못 썼다. **버리고 저널만으로 간다** — 저널이 진실이므로
             * 느릴 뿐 틀리지 않는다. 다만 그 사실을 숨기지 않는다.
             */
            res.snapshot_bad = true;
            /*
             * `snapshot_read`는 성공할 때만 `*out_seq`를 건드린다. 그래서
             * 여기서 `upto`는 아직 0이다 — 되돌리는 대입은 아무 일도 하지
             * 않아 변이 검사에서 살아남았다(S10). **같은 뜻을 대입 대신
             * 검사로 적는다.** 저 약속이 깨지면 여기서 터진다.
             */
            assert(upto == 0);
        }
    }

    journal_t *j = journal_open_read(journal_path);
    if (j == NULL) {
        /*
         * 저널이 없다. 스냅샷만 있으면 그것까지가 아는 전부다 —
         * 없는 것을 지어내지 않는다.
         */
        if (out != NULL) {
            *out = res;
        }
        return res.used_snapshot ? ERR_OK : ERR_NOT_FOUND;
    }

    replay_ctx_t rctx = {.fns = fns, .from_seq = upto, .applied = 0};
    bool         torn = false;
    int          rc = journal_replay(j, on_journal_rec, &rctx, &torn);
    journal_close(j);

    if (rc < 0) {
        return rc;
    }

    res.replayed = rctx.applied;
    res.journal_torn = torn;

    if (out != NULL) {
        *out = res;
    }
    return ERR_OK;
}
