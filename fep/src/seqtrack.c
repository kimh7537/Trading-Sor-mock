#include "seqtrack.h"

#include <string.h>

#include "errors.h"

/* --- 보내는 쪽 --- */

void seqstore_init(seqstore_t *st)
{
    if (st == NULL) {
        return;
    }
    /*
     * 슬롯 전체를 0으로 만든다. `len == 0`이 빈 칸이므로 그것만으로 충분하고,
     * 프레임 내용은 `len`이 정하는 만큼만 읽는다.
     */
    memset(st, 0, sizeof(*st));
}

/* seq를 고리의 어느 칸에 둘지. 나머지 연산 하나다. */
static size_t slot_of(uint64_t seq)
{
    return (size_t)(seq % SEQSTORE_KEEP);
}

int seqstore_put(seqstore_t *st, uint64_t seq, const uint8_t *frame, size_t len)
{
    if (st == NULL || frame == NULL) {
        return ERR_NULL_PTR;
    }
    if (seq == 0 || len == 0 || len > SEQSTORE_FRAME_MAX) {
        return ERR_INVALID_ARG;
    }
    /*
     * **번호는 커지기만 한다.** 되돌아간 번호를 받아 주면 고리 안에 과거와
     * 미래가 섞여, `oldest`가 가리키는 구간이 실제로 들고 있는 것과 달라진다.
     * 그러면 없는 것을 있다고 답하게 된다.
     */
    if (st->count > 0 && seq <= st->newest) {
        return ERR_INVALID_ARG;
    }

    seqslot_t *s = &st->slot[slot_of(seq)];
    s->seq = seq;
    s->len = (uint16_t)len;
    memcpy(s->frame, frame, len);

    st->newest = seq;
    if (st->count < SEQSTORE_KEEP) {
        st->count++;
    }
    /*
     * 고리가 차면 가장 오래된 것이 방금 덮였다. 들고 있는 구간은 언제나
     * `[newest - count + 1, newest]`다.
     */
    st->oldest = st->newest - (uint64_t)st->count + 1;

    return ERR_OK;
}

int seqstore_get(const seqstore_t *st, uint64_t seq, uint8_t *out, size_t cap)
{
    if (st == NULL || out == NULL) {
        return ERR_NULL_PTR;
    }
    /*
     * **칸의 번호가 요청한 번호와 같은지만 본다.** 이 하나로 충분하다.
     *
     * `oldest <= seq <= newest`를 먼저 보는 빠른 길을 두지 않는다. 밀려난
     * 번호는 그 칸에 이미 한 바퀴 뒤의 것이 들어앉아 번호가 다르고, 아직
     * 안 보낸 번호도 마찬가지다. 즉 **범위 검사가 걸러 낼 것을 이 검사가
     * 모두 걸러 낸다** — 동작이 같아 변이 검사가 구분하지 못했다(Q2가
     * 살아남았다). T3-08~11에서 되풀이한 판단과 같다.
     *
     * `oldest`·`newest`는 남긴다. `resend_from`이 "어디부터 있는지"를 상대에게
     * 알려 줄 때 쓰는 값이고, 그것은 이 함수의 판정과는 다른 쓰임이다.
     */
    const seqslot_t *s = &st->slot[slot_of(seq)];
    if (s->seq != seq || s->len == 0) {
        return ERR_NOT_FOUND;
    }
    if (cap < s->len) {
        return ERR_INVALID_ARG;
    }

    memcpy(out, s->frame, s->len);
    return (int)s->len;
}

uint64_t seqstore_oldest(const seqstore_t *st)
{
    return (st != NULL && st->count > 0) ? st->oldest : 0;
}

/* --- 받는 쪽 --- */

void seqtrack_init(seqtrack_t *tr, uint64_t first)
{
    if (tr == NULL) {
        return;
    }
    memset(tr, 0, sizeof(*tr));
    tr->expected = (first > 0) ? first : 1;
}

uint64_t seqtrack_expected(const seqtrack_t *tr)
{
    return (tr != NULL) ? tr->expected : 0;
}

bool seqtrack_recovering(const seqtrack_t *tr)
{
    return (tr != NULL) && tr->recovering;
}

seq_verdict_t seqtrack_on(seqtrack_t *tr, uint64_t seq)
{
    if (tr == NULL) {
        return SEQ_DUP; /* 버리는 쪽이 안전하다 */
    }

    if (seq == tr->expected) {
        tr->expected++;
        /*
         * 기다리던 번호가 왔다. 메우기가 끝났다 — 여기서 풀지 않으면
         * 갭이 채워져도 계속 버리게 된다.
         */
        tr->recovering = false;
        return SEQ_OK;
    }

    if (seq < tr->expected) {
        /*
         * **이미 본 것이다. 버린다.** 재전송은 겹쳐 오게 마련이고, 두 번
         * 올리면 체결이 두 번 잡힌다.
         */
        tr->dups++;
        return SEQ_DUP;
    }

    /* seq > expected — 빠진 것이 있다. */
    if (tr->recovering) {
        /*
         * 이미 재전송을 요청해 둔 상태다. 아직 그 차례가 아닌 전문은 버린다 —
         * 상대가 `gap_from`부터 다시 보내 주므로 곧 제 순서로 온다.
         * 여기서 또 요청하면 상대를 요청으로 뒤덮는다.
         */
        return SEQ_WAIT;
    }

    tr->recovering = true;
    tr->gap_from = tr->expected;
    tr->gaps++;
    return SEQ_GAP;
}

int seqtrack_skip_to(seqtrack_t *tr, uint64_t next_seq)
{
    if (tr == NULL) {
        return ERR_NULL_PTR;
    }
    /*
     * **되돌리는 요청은 받지 않는다.** 이미 처리해 위로 올린 전문을 다시
     * 올리게 되고, 그것은 갭보다 나쁘다 — 갭은 빈 것을 알지만 중복은
     * 처리된 것처럼 보인다.
     */
    if (next_seq <= tr->expected) {
        return ERR_INVALID_ARG;
    }

    tr->expected = next_seq;
    tr->recovering = false;
    return ERR_OK;
}
