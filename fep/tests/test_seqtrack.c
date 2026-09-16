/*
 * T3-12 FEP — 시퀀스 갭 감지와 재전송 보관.
 *
 * 완료 조건 중 순수 논리 부분을 옮긴다(전 구간은 `test_session.c`가 본다).
 *  1. 정상 순서는 그대로 통과한다
 *  2. **이미 본 번호는 버린다** — 재전송은 겹쳐 온다
 *  3. 갭을 만나면 한 번만 알리고, 메우는 동안 오는 것은 버린다
 *  4. 갭이 채워지면 **다시 흐른다**
 *  5. 보관 고리가 한 바퀴 돈 뒤 밀려난 것은 **없다고 답한다**
 *
 * 5번이 이 태스크에서 가장 조용한 위험이다. 밀려난 것을 "있다"고 답하면
 * 한 바퀴 전의 전문을 다시 보내게 되고, 받는 쪽은 그것을 멀쩡한 전문으로
 * 처리한다 — 갭보다 나쁘다.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "seqtrack.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

/* 32KB짜리라 스택에 두지 않는다. */
static seqstore_t g_st;

/* seq마다 다른 내용을 만든다 — 엉뚱한 칸을 꺼내면 내용이 다르다. */
static size_t make_frame(uint8_t *buf, size_t cap, uint64_t seq, size_t len)
{
    assert(len <= cap);
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(seq * 7 + i);
    }
    return len;
}

static void check_frame(const uint8_t *buf, uint64_t seq, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        assert(buf[i] == (uint8_t)(seq * 7 + i));
    }
}

/* --- 받는 쪽 --- */

/* 1. 정상 순서 */
static void test_in_order(void)
{
    seqtrack_t tr;
    seqtrack_init(&tr, 1);
    assert(seqtrack_expected(&tr) == 1);
    assert(!seqtrack_recovering(&tr));

    for (uint64_t i = 1; i <= 100; i++) {
        assert(seqtrack_on(&tr, i) == SEQ_OK);
        assert(seqtrack_expected(&tr) == i + 1);
        assert(!seqtrack_recovering(&tr));
    }
    assert(tr.gaps == 0);
    assert(tr.dups == 0);
}

/* 2. 중복은 버린다 */
static void test_duplicates(void)
{
    seqtrack_t tr;
    seqtrack_init(&tr, 1);

    assert(seqtrack_on(&tr, 1) == SEQ_OK);
    assert(seqtrack_on(&tr, 2) == SEQ_OK);
    assert(seqtrack_expected(&tr) == 3);

    /* 같은 것이 또 와도 기대값이 흔들리지 않는다. */
    assert(seqtrack_on(&tr, 1) == SEQ_DUP);
    assert(seqtrack_on(&tr, 2) == SEQ_DUP);
    assert(seqtrack_expected(&tr) == 3);
    assert(tr.dups == 2);

    /* 흐름은 그대로 이어진다. */
    assert(seqtrack_on(&tr, 3) == SEQ_OK);
    assert(seqtrack_expected(&tr) == 4);
}

/* 3·4. 갭을 만나고, 메우고, 다시 흐른다 */
static void test_gap_then_recover(void)
{
    seqtrack_t tr;
    seqtrack_init(&tr, 1);

    assert(seqtrack_on(&tr, 1) == SEQ_OK);
    assert(seqtrack_on(&tr, 2) == SEQ_OK);

    /* 3, 4, 5가 빠지고 6이 왔다. */
    assert(seqtrack_on(&tr, 6) == SEQ_GAP);
    assert(seqtrack_recovering(&tr));
    assert(tr.gap_from == 3); /* 3부터 다시 달라고 해야 한다 */
    assert(seqtrack_expected(&tr) == 3);
    assert(tr.gaps == 1);

    /*
     * **메우는 동안 오는 것은 버린다.** 그리고 갭을 또 알리지 않는다 —
     * 알릴 때마다 요청을 보내면 상대를 요청으로 뒤덮는다.
     */
    assert(seqtrack_on(&tr, 7) == SEQ_WAIT);
    assert(seqtrack_on(&tr, 8) == SEQ_WAIT);
    assert(tr.gaps == 1);
    assert(seqtrack_expected(&tr) == 3);

    /* 재전송이 3부터 순서대로 온다. */
    assert(seqtrack_on(&tr, 3) == SEQ_OK);
    assert(!seqtrack_recovering(&tr)); /* 메우기가 끝났다 */
    assert(seqtrack_on(&tr, 4) == SEQ_OK);
    assert(seqtrack_on(&tr, 5) == SEQ_OK);

    /* 아까 버린 6, 7, 8이 다시 오고 이번엔 받아들인다. */
    assert(seqtrack_on(&tr, 6) == SEQ_OK);
    assert(seqtrack_on(&tr, 7) == SEQ_OK);
    assert(seqtrack_on(&tr, 8) == SEQ_OK);
    assert(seqtrack_expected(&tr) == 9);
    assert(tr.gaps == 1);
}

/*
 * 메우는 중에 **이미 본 번호**가 오는 경우. 재전송 구간이 우리가 가진 것보다
 * 앞에서 시작하면 이렇게 된다. 버리되 `SEQ_WAIT`이 아니라 `SEQ_DUP`이다 —
 * 둘을 섞으면 지표가 뜻을 잃는다.
 */
static void test_dup_while_recovering(void)
{
    seqtrack_t tr;
    seqtrack_init(&tr, 1);

    assert(seqtrack_on(&tr, 1) == SEQ_OK);
    assert(seqtrack_on(&tr, 2) == SEQ_OK);
    assert(seqtrack_on(&tr, 9) == SEQ_GAP);
    assert(seqtrack_recovering(&tr));

    assert(seqtrack_on(&tr, 1) == SEQ_DUP);
    assert(seqtrack_recovering(&tr)); /* 중복이 메우기를 풀지 않는다 */
    assert(seqtrack_expected(&tr) == 3);
}

/* 새 갭이 앞선 갭보다 뒤에 생기는 경우 — 메우기 중에는 새로 알리지 않는다 */
static void test_second_gap_while_recovering(void)
{
    seqtrack_t tr;
    seqtrack_init(&tr, 1);

    assert(seqtrack_on(&tr, 1) == SEQ_OK);
    assert(seqtrack_on(&tr, 5) == SEQ_GAP);
    assert(tr.gap_from == 2);

    assert(seqtrack_on(&tr, 50) == SEQ_WAIT);
    assert(tr.gap_from == 2); /* 요청 시작점은 그대로 2다 */
    assert(tr.gaps == 1);

    /* 2가 오면 풀리고, 그 뒤 또 갭이 나면 그때 다시 알린다. */
    assert(seqtrack_on(&tr, 2) == SEQ_OK);
    assert(!seqtrack_recovering(&tr));
    assert(seqtrack_on(&tr, 10) == SEQ_GAP);
    assert(tr.gaps == 2);
    assert(tr.gap_from == 3);
}

/* 상대가 "그 앞은 더 없다"고 알려 온 경우 */
static void test_skip_to(void)
{
    seqtrack_t tr;
    seqtrack_init(&tr, 1);

    assert(seqtrack_on(&tr, 1) == SEQ_OK);
    assert(seqtrack_on(&tr, 20) == SEQ_GAP);
    assert(seqtrack_recovering(&tr));

    /* 2~19는 상대도 더 이상 갖고 있지 않다. 20부터 이어라. */
    assert(seqtrack_skip_to(&tr, 20) == ERR_OK);
    assert(seqtrack_expected(&tr) == 20);
    assert(!seqtrack_recovering(&tr));

    assert(seqtrack_on(&tr, 20) == SEQ_OK);
    assert(seqtrack_expected(&tr) == 21);
}

/*
 * **되돌리는 건너뛰기는 받지 않는다.** 받아 주면 이미 처리해 위로 올린
 * 전문을 다시 올리게 된다 — 갭은 빈 것을 알지만 중복은 처리된 것처럼 보인다.
 */
static void test_skip_to_rejects_backwards(void)
{
    seqtrack_t tr;
    seqtrack_init(&tr, 1);

    for (uint64_t i = 1; i <= 10; i++) {
        assert(seqtrack_on(&tr, i) == SEQ_OK);
    }
    assert(seqtrack_expected(&tr) == 11);

    assert(seqtrack_skip_to(&tr, 5) == ERR_INVALID_ARG);
    assert(seqtrack_expected(&tr) == 11); /* 움직이지 않았다 */

    assert(seqtrack_skip_to(&tr, 11) == ERR_INVALID_ARG); /* 제자리도 안 된다 */
    assert(seqtrack_expected(&tr) == 11);

    assert(seqtrack_skip_to(&tr, 12) == ERR_OK);
    assert(seqtrack_expected(&tr) == 12);
}

/* --- 보내는 쪽 --- */

static void test_store_put_get(void)
{
    seqstore_init(&g_st);
    assert(seqstore_oldest(&g_st) == 0);

    uint8_t frame[70];
    uint8_t got[SEQSTORE_FRAME_MAX];

    for (uint64_t i = 1; i <= 10; i++) {
        size_t n = make_frame(frame, sizeof(frame), i, sizeof(frame));
        assert(seqstore_put(&g_st, i, frame, n) == ERR_OK);
    }
    assert(seqstore_oldest(&g_st) == 1);

    for (uint64_t i = 1; i <= 10; i++) {
        int n = seqstore_get(&g_st, i, got, sizeof(got));
        assert(n == (int)sizeof(frame));
        check_frame(got, i, (size_t)n);
    }

    /* 보낸 적 없는 번호. */
    assert(seqstore_get(&g_st, 11, got, sizeof(got)) == ERR_NOT_FOUND);
    assert(seqstore_get(&g_st, 0, got, sizeof(got)) == ERR_NOT_FOUND);
}

/*
 * 5. **고리가 한 바퀴 돈 뒤.** 밀려난 것은 없다고 답해야 한다.
 * 있다고 답하면 한 바퀴 전의 전문을 다시 보낸다.
 */
static void test_store_wraps(void)
{
    seqstore_init(&g_st);

    uint8_t frame[64];
    uint8_t got[SEQSTORE_FRAME_MAX];

    /* 고리 크기의 세 배를 넣는다. */
    const uint64_t last = SEQSTORE_KEEP * 3;
    for (uint64_t i = 1; i <= last; i++) {
        size_t n = make_frame(frame, sizeof(frame), i, sizeof(frame));
        assert(seqstore_put(&g_st, i, frame, n) == ERR_OK);
    }

    /* 들고 있는 것은 마지막 SEQSTORE_KEEP개뿐이다. */
    uint64_t oldest = last - SEQSTORE_KEEP + 1;
    assert(seqstore_oldest(&g_st) == oldest);

    for (uint64_t i = oldest; i <= last; i++) {
        int n = seqstore_get(&g_st, i, got, sizeof(got));
        assert(n == (int)sizeof(frame));
        /* **내용까지 확인한다.** 칸은 맞는데 한 바퀴 전 내용이면 여기서 걸린다. */
        check_frame(got, i, (size_t)n);
    }

    /* 밀려난 것은 전부 없다고 답한다. */
    for (uint64_t i = 1; i < oldest; i++) {
        assert(seqstore_get(&g_st, i, got, sizeof(got)) == ERR_NOT_FOUND);
    }
    /* 아직 안 보낸 것도 마찬가지다. */
    assert(seqstore_get(&g_st, last + 1, got, sizeof(got)) == ERR_NOT_FOUND);
}

/*
 * 번호를 건너뛰며 넣는 경우. 고리 칸이 겹치므로 **칸의 번호를 확인해야**
 * 한 바퀴 전의 것을 꺼내지 않는다.
 */
static void test_store_sparse(void)
{
    seqstore_init(&g_st);

    uint8_t frame[32];
    uint8_t got[SEQSTORE_FRAME_MAX];

    /* 1, 3, 5, ... 로 넣는다. */
    uint64_t last = 0;
    for (uint64_t i = 1; i <= SEQSTORE_KEEP; i += 2) {
        size_t n = make_frame(frame, sizeof(frame), i, sizeof(frame));
        assert(seqstore_put(&g_st, i, frame, n) == ERR_OK);
        last = i;
    }

    /* 넣은 것은 나오고, 안 넣은 것은 구간 안이어도 안 나온다. */
    for (uint64_t i = seqstore_oldest(&g_st); i <= last; i++) {
        int n = seqstore_get(&g_st, i, got, sizeof(got));
        if (i % 2 == 1) {
            assert(n == (int)sizeof(frame));
            check_frame(got, i, (size_t)n);
        } else {
            assert(n == ERR_NOT_FOUND);
        }
    }
}

/* 번호는 커지기만 한다 */
static void test_store_rejects_backwards(void)
{
    seqstore_init(&g_st);

    uint8_t frame[16];
    size_t  n = make_frame(frame, sizeof(frame), 5, sizeof(frame));

    assert(seqstore_put(&g_st, 5, frame, n) == ERR_OK);
    assert(seqstore_put(&g_st, 5, frame, n) == ERR_INVALID_ARG); /* 제자리 */
    assert(seqstore_put(&g_st, 4, frame, n) == ERR_INVALID_ARG); /* 뒤로 */
    assert(seqstore_put(&g_st, 6, frame, n) == ERR_OK);          /* 앞으로 */

    /* 거절된 것이 상태를 건드리지 않았다. */
    assert(seqstore_oldest(&g_st) == 5);
    uint8_t got[SEQSTORE_FRAME_MAX];
    assert(seqstore_get(&g_st, 4, got, sizeof(got)) == ERR_NOT_FOUND);
}

static void test_store_args(void)
{
    seqstore_init(&g_st);

    uint8_t frame[SEQSTORE_FRAME_MAX + 1];
    uint8_t got[SEQSTORE_FRAME_MAX];
    memset(frame, 0xA5, sizeof(frame));

    assert(seqstore_put(NULL, 1, frame, 8) == ERR_NULL_PTR);
    assert(seqstore_put(&g_st, 1, NULL, 8) == ERR_NULL_PTR);
    assert(seqstore_put(&g_st, 0, frame, 8) == ERR_INVALID_ARG);
    assert(seqstore_put(&g_st, 1, frame, 0) == ERR_INVALID_ARG);
    /* 한도를 넘으면 **조용히 자르지 않고 거절한다** — 반쪽 전문이 나가면 안 된다. */
    assert(seqstore_put(&g_st, 1, frame, SEQSTORE_FRAME_MAX + 1) ==
           ERR_INVALID_ARG);
    assert(seqstore_oldest(&g_st) == 0); /* 아무것도 안 들어갔다 */

    assert(seqstore_put(&g_st, 1, frame, SEQSTORE_FRAME_MAX) == ERR_OK);
    assert(seqstore_get(NULL, 1, got, sizeof(got)) == ERR_NULL_PTR);
    assert(seqstore_get(&g_st, 1, NULL, sizeof(got)) == ERR_NULL_PTR);
    /* 받을 자리가 모자라면 넘치지 않고 거절한다. */
    assert(seqstore_get(&g_st, 1, got, SEQSTORE_FRAME_MAX - 1) ==
           ERR_INVALID_ARG);

    assert(seqstore_oldest(NULL) == 0);
    seqstore_init(NULL); /* 죽지 않는다 */
}

static void test_track_args(void)
{
    seqtrack_t tr;

    /* 0을 주면 1로 본다 — 0은 유효한 시퀀스가 아니다(T3-11). */
    seqtrack_init(&tr, 0);
    assert(seqtrack_expected(&tr) == 1);

    seqtrack_init(&tr, 500);
    assert(seqtrack_expected(&tr) == 500);
    assert(seqtrack_on(&tr, 500) == SEQ_OK);

    assert(seqtrack_expected(NULL) == 0);
    assert(!seqtrack_recovering(NULL));
    assert(seqtrack_on(NULL, 1) == SEQ_DUP); /* 버리는 쪽이 안전하다 */
    assert(seqtrack_skip_to(NULL, 1) == ERR_NULL_PTR);
    seqtrack_init(NULL, 1); /* 죽지 않는다 */
}

int main(void)
{
    STEP(test_in_order);
    STEP(test_duplicates);
    STEP(test_gap_then_recover);
    STEP(test_dup_while_recovering);
    STEP(test_second_gap_while_recovering);
    STEP(test_skip_to);
    STEP(test_skip_to_rejects_backwards);
    STEP(test_store_put_get);
    STEP(test_store_wraps);
    STEP(test_store_sparse);
    STEP(test_store_rejects_backwards);
    STEP(test_store_args);
    STEP(test_track_args);
    return 0;
}
