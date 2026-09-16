#include "framer.h"

#include <string.h>

#include "errors.h"

void framer_init(framer_t *fr)
{
    if (fr == NULL) {
        return;
    }
    memset(fr, 0, sizeof(*fr));
}

size_t framer_pending(const framer_t *fr)
{
    return (fr != NULL) ? (fr->len - fr->off) : 0;
}

bool framer_broken(const framer_t *fr)
{
    return (fr != NULL) && fr->err != ERR_OK;
}

int framer_push(framer_t *fr, const uint8_t *data, size_t n)
{
    if (fr == NULL || (data == NULL && n > 0)) {
        return ERR_NULL_PTR;
    }
    if (fr->err != ERR_OK) {
        return ERR_NOT_SUPPORTED; /* 더 받아 봐야 소용없다 */
    }
    if (n == 0) {
        return ERR_OK;
    }

    /*
     * 뒤가 모자라면 앞의 빈자리를 쓴다.
     *
     * "다 꺼냈으면 곧바로 앞으로 되돌린다"는 빠른 길을 따로 두지 않는다.
     * 그 경우에도 여기 `memmove`가 **0바이트를 옮기므로** 버는 것이 없고,
     * 동작이 똑같아서 **변이 검사가 있고 없고를 구분하지 못한다.**
     * 확인할 수 없는 중복 경로는 두지 않는다.
     */
    if (fr->len + n > sizeof(fr->buf) && fr->off > 0) {
        memmove(fr->buf, fr->buf + fr->off, fr->len - fr->off);
        fr->len -= fr->off;
        fr->off = 0;
    }

    /*
     * 그래도 모자라면 받을 수 없다. 버퍼는 전문 하나가 통째로 들어갈 만큼 크므로
     * 여기 오는 것은 꺼내지 않고 넣기만 했다는 뜻이다 — 호출부의 잘못이다.
     */
    if (fr->len + n > sizeof(fr->buf)) {
        return ERR_POOL_EXHAUSTED;
    }

    memcpy(fr->buf + fr->len, data, n);
    fr->len += n;

    return ERR_OK;
}

int framer_next(framer_t *fr, wire_header_t *hdr, const uint8_t **body)
{
    if (fr == NULL || hdr == NULL || body == NULL) {
        return ERR_NULL_PTR;
    }
    if (fr->err != ERR_OK) {
        return fr->err; /* 한 번 어긋나면 계속 같은 값을 준다 */
    }

    size_t avail = fr->len - fr->off;
    if (avail < WIRE_HEADER_LEN) {
        return 0; /* 헤더도 아직 다 안 왔다 */
    }

    /*
     * **헤더를 먼저 본다.** body_len이 한도를 넘는지는 바디를 기다리기 전에
     * 알 수 있다. 기다린 다음 보면 그 사이에 쓰레기를 버퍼만큼 받아 둔 뒤다.
     */
    wire_header_t h;
    int           rc = wire_decode_header(fr->buf + fr->off, avail, &h);
    if (rc < 0) {
        fr->err = rc; /* magic·version·길이 — 어느 쪽이든 스트림을 버린다 */
        return rc;
    }

    size_t total = WIRE_HEADER_LEN + (size_t)h.body_len;
    if (avail < total) {
        return 0; /* 바디가 아직 다 안 왔다 */
    }

    *hdr = h;
    *body = fr->buf + fr->off + WIRE_HEADER_LEN;
    fr->off += total;

    return 1;
}
