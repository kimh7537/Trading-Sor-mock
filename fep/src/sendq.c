#include "sendq.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "errors.h"

void sendq_init(sendq_t *q)
{
    if (q == NULL) {
        return;
    }
    q->off = 0;
    q->len = 0;
    /*
     * `buf`는 지우지 않는다. 256KB를 0으로 채워도 얻는 것이 없다 — 아직 안 보낸
     * 구간은 `off`와 `len`이 정하고, 그 밖은 아무도 읽지 않는다.
     */
}

size_t sendq_pending(const sendq_t *q)
{
    return (q != NULL) ? (q->len - q->off) : 0;
}

bool sendq_want_write(const sendq_t *q)
{
    return sendq_pending(q) > 0;
}

int sendq_push(sendq_t *q, const uint8_t *frame, size_t n)
{
    if (q == NULL || frame == NULL) {
        return ERR_NULL_PTR;
    }
    if (n == 0 || n > SENDQ_CAP) {
        return ERR_INVALID_ARG;
    }

    /*
     * 뒤가 모자라면 앞의 빈자리를 쓴다(T3-09의 프레이머와 같은 모양).
     *
     * "다 보냈으면 곧바로 앞으로 되돌린다"를 `flush` 쪽에 두지 않는다.
     * 그 경우 여기 `memmove`가 0바이트를 옮기므로 동작이 똑같고, T3-09에서
     * 배운 대로 **동작이 같은 중복 경로는 변이 검사가 구분하지 못한다.**
     */
    if (q->len + n > SENDQ_CAP && q->off > 0) {
        memmove(q->buf, q->buf + q->off, q->len - q->off);
        q->len -= q->off;
        q->off = 0;
    }

    /*
     * **전부 아니면 전무.** 반쪽 전문이 나가면 받는 쪽은 접속을 끊는다.
     * 여기서 거절하면 호출부가 무엇을 할지 정한다 — 헤더의 설명 참조.
     */
    if (q->len + n > SENDQ_CAP) {
        return ERR_POOL_EXHAUSTED;
    }

    memcpy(q->buf + q->len, frame, n);
    q->len += n;

    return ERR_OK;
}

int sendq_flush(sendq_t *q, int fd)
{
    if (q == NULL) {
        return ERR_NULL_PTR;
    }
    if (fd < 0) {
        return ERR_INVALID_ARG;
    }

    size_t left = q->len - q->off;

    /*
     * **보낼 게 없으면 `write`를 부르지 않는다.**
     *
     * 리눅스에서 `write(fd, p, 0)`은 0을 돌려주므로 이 검사가 있으나 없으나
     * 결과가 같다 — 변이 검사가 구분하지 못한다(S9가 살아남았다). 그래도
     * 지우지 않는다. POSIX는 **일반 파일이 아닌 대상에 길이 0으로 쓰는 것을
     * "규정하지 않음"으로 둔다.** 통과하는 것은 이 커널의 사정이지 규격의
     * 보장이 아니다. T3-06의 `PTHREAD_PROCESS_SHARED`와 같은 자리다.
     */
    if (left == 0) {
        return 0;
    }

    ssize_t w = write(fd, q->buf + q->off, left);
    if (w < 0) {
        /*
         * **EAGAIN은 에러가 아니다.** 커널 송신 버퍼가 찼다는 뜻이고, 자리가
         * 나면 `EV_WRITE`가 뜬다. 0을 돌려주면 호출부는 "이번엔 못 보냈다"로
         * 읽고 큐를 그대로 둔다.
         *
         * EINTR도 마찬가지다 — 시그널에 끊겼을 뿐 아무것도 나가지 않았다.
         */
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        /*
         * 나머지(EPIPE·ECONNRESET·EBADF)는 이 접속이 끝났다는 뜻이다.
         * EAGAIN과 **반드시 구분해야 한다** — 하나는 기다리면 되고 하나는
         * 다시 붙어야 한다. 구분하지 않으면 끊긴 접속에 영원히 재시도한다.
         *
         * SIGPIPE로 죽지 않는 것은 `evloop_install_signals()`가 무시로
         * 걸어 두기 때문이다.
         */
        return ERR_IO;
    }

    q->off += (size_t)w;

    return (int)w;
}
