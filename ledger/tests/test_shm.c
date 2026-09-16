/*
 * T3-05 공유 메모리 세그먼트.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. fork한 자식이 부모와 **같은 메모리**를 본다 — 한쪽이 쓰면 다른 쪽이 읽는다
 *  2. 배치가 문서와 맞는다. 포인터가 아니라 오프셋으로 접근한다
 *  3. 머리말이 형식을 검사한다 (magic·version·크기·겹침)
 *  4. 영역이 캐시라인 경계에 맞는다
 *  5. 초기화가 fork 전에 끝난다
 *  6. 크기 계산이 넘치지 않는다
 *
 * 1번이 이 태스크의 전부다. "mmap이 성공했다"가 아니라 **"두 프로세스가 같은
 * 바이트를 본다"**를 봐야 한다. `MAP_SHARED`를 `MAP_PRIVATE`로 바꾸면 mmap은
 * 여전히 성공하고 부모 안에서의 읽기·쓰기도 멀쩡하다 — 오직 fork 너머에서만
 * 틀어진다. 그래서 진짜 fork로 확인한다.
 */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "errors.h"
#include "shm_segment.h"

#define STEP(fn)                                                            \
    do {                                                                   \
        fprintf(stderr, "[%s]\n", #fn);                                    \
        fn();                                                              \
    } while (0)

/* 시험용 레코드. 계좌 구조는 T3-06의 것이고 여기서는 자리만 쓴다. */
typedef struct {
    uint64_t id;
    int64_t  balance;
    uint32_t touched_by_child;
} probe_rec_t;

static const int32_t COUNTS[SHM_REGION_COUNT] = {8, 16};
static const size_t  SIZES[SHM_REGION_COUNT] = {sizeof(probe_rec_t),
                                                sizeof(probe_rec_t)};

/* --- 1. 두 프로세스가 같은 메모리를 보는가 --- */

/*
 * 완료 조건 1. **양방향으로 확인한다.**
 *
 *  - 부모가 쓴 것을 자식이 읽는다. 단 **fork 뒤에 쓴 것**이어야 한다 —
 *    fork 전 값은 사본이어도 같으므로 공유를 증명하지 못한다
 *  - 자식이 쓴 것을 부모가 읽는다
 *
 * 순서를 파이프로 맞춘다. 잠을 재우면 느린 기계에서 깨진다.
 */
static void test_shared_across_fork(void)
{
    shm_segment_t *seg = shm_create(COUNTS, SIZES);
    assert(seg != NULL);
    assert(shm_validate(seg) == ERR_OK);

    int to_child[2];
    int to_parent[2];
    assert(pipe(to_child) == 0);
    assert(pipe(to_parent) == 0);

    probe_rec_t *r0 = shm_record(seg, SHM_REGION_ACCOUNT, 0);
    probe_rec_t *r1 = shm_record(seg, SHM_REGION_ACCOUNT, 1);
    assert(r0 != NULL && r1 != NULL);

    /* fork 전 값. 이것만으로는 공유를 확인할 수 없다 — 사본이어도 같다. */
    r0->id = 111;
    r0->balance = 1000;

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        close(to_child[1]);
        close(to_parent[0]);

        uint8_t go = 0;
        if (read(to_child[0], &go, 1) != 1) {
            _exit(10);
        }

        /* 부모가 **fork 뒤에** 바꾼 값이 보여야 한다. */
        if (r0->balance != 2000 || r0->id != 111) {
            _exit(11);
        }

        /* 자식이 쓴다. 부모가 이것을 봐야 한다. */
        r1->id = 222;
        r1->balance = -777;
        r1->touched_by_child = 1;

        uint8_t done = 1;
        if (write(to_parent[1], &done, 1) != 1) {
            _exit(12);
        }

        close(to_child[0]);
        close(to_parent[1]);
        shm_destroy(seg); /* 자식도 제 매핑을 푼다 */
        _exit(0);
    }

    close(to_child[0]);
    close(to_parent[1]);

    /* fork 뒤에 바꾼다 — 이 값이 자식에게 보이면 진짜 공유다. */
    r0->balance = 2000;

    uint8_t go = 1;
    assert(write(to_child[1], &go, 1) == 1);

    uint8_t done = 0;
    assert(read(to_parent[0], &done, 1) == 1);

    /* 자식이 쓴 것이 부모에게 보인다. */
    assert(r1->id == 222);
    assert(r1->balance == -777);
    assert(r1->touched_by_child == 1);

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    close(to_child[1]);
    close(to_parent[0]);
    shm_destroy(seg);
}

/* --- 2. 배치 --- */

static void test_layout(void)
{
    shm_segment_t *seg = shm_create(COUNTS, SIZES);
    assert(seg != NULL);

    const shm_header_t *h = shm_header(seg);
    assert(h != NULL);
    assert(h->magic == SHM_MAGIC);
    assert(h->version == SHM_VERSION);
    assert(h->initialized == 1);
    assert(h->total_size == shm_size(seg));

    for (int32_t r = 0; r < SHM_REGION_COUNT; r++) {
        assert(h->rec_count[r] == COUNTS[r]);

        /* 완료 조건 4 — 영역과 레코드가 캐시라인 경계에 맞는다. */
        assert(h->rec_off[r] % SHM_CACHELINE == 0);
        assert(h->rec_size[r] % SHM_CACHELINE == 0);

        /* 레코드 크기는 요청한 크기 이상이어야 한다(올림했으므로). */
        assert(h->rec_size[r] >= SIZES[r]);

        /* 머리말을 덮지 않는다. */
        assert(h->rec_off[r] >= sizeof(shm_header_t));
    }

    /*
     * 레코드 주소가 오프셋 계산과 정확히 맞는다 — **포인터를 저장하지 않고
     * 매번 오프셋에서 만든다**는 규칙이 지켜지는지 본다.
     */
    const uint8_t *base = (const uint8_t *)h;
    for (int32_t r = 0; r < SHM_REGION_COUNT; r++) {
        for (int32_t i = 0; i < h->rec_count[r]; i++) {
            const uint8_t *want =
                base + h->rec_off[r] + (uint64_t)h->rec_size[r] * (uint64_t)i;
            assert((const uint8_t *)shm_record(seg, r, i) == want);

            /* 레코드마다 캐시라인 경계에 있다 — 거짓 공유가 없다. */
            assert(((uintptr_t)want % SHM_CACHELINE) == 0);
        }
    }

    /* 두 영역이 겹치지 않는다. */
    uint64_t a0 = h->rec_off[0];
    uint64_t a1 = a0 + (uint64_t)h->rec_size[0] * (uint64_t)h->rec_count[0];
    uint64_t b0 = h->rec_off[1];
    assert(b0 >= a1);

    /* 세그먼트 끝을 넘지 않는다. */
    uint64_t b1 = b0 + (uint64_t)h->rec_size[1] * (uint64_t)h->rec_count[1];
    assert(b1 <= h->total_size);

    assert(shm_region_base(seg, 0) == shm_record(seg, 0, 0));
    assert(shm_region_base(seg, 1) == shm_record(seg, 1, 0));

    shm_destroy(seg);
}

/* 새 세그먼트는 0으로 차 있다 — 앞선 실행의 찌꺼기가 보이면 안 된다. */
static void test_zeroed(void)
{
    shm_segment_t *seg = shm_create(COUNTS, SIZES);
    assert(seg != NULL);

    const shm_header_t *h = shm_header(seg);
    for (int32_t r = 0; r < SHM_REGION_COUNT; r++) {
        for (int32_t i = 0; i < h->rec_count[r]; i++) {
            const uint8_t *p = shm_record(seg, r, i);
            for (uint32_t b = 0; b < h->rec_size[r]; b++) {
                assert(p[b] == 0);
            }
        }
    }

    shm_destroy(seg);
}

/* --- 3. 경계와 인자 --- */

static void test_bounds(void)
{
    shm_segment_t *seg = shm_create(COUNTS, SIZES);
    assert(seg != NULL);

    /* 영역 번호 범위 밖. */
    assert(shm_record(seg, -1, 0) == NULL);
    assert(shm_record(seg, SHM_REGION_COUNT, 0) == NULL);
    assert(shm_region_base(seg, -1) == NULL);
    assert(shm_region_base(seg, SHM_REGION_COUNT) == NULL);

    /* 인덱스 범위 밖 — 마지막 유효 인덱스 바로 다음까지 본다. */
    for (int32_t r = 0; r < SHM_REGION_COUNT; r++) {
        assert(shm_record(seg, r, -1) == NULL);
        assert(shm_record(seg, r, COUNTS[r]) == NULL);
        assert(shm_record(seg, r, COUNTS[r] - 1) != NULL);
    }

    assert(shm_record(NULL, 0, 0) == NULL);
    assert(shm_header(NULL) == NULL);
    assert(shm_size(NULL) == 0);
    assert(shm_validate(NULL) == ERR_NULL_PTR);
    shm_destroy(NULL);

    shm_destroy(seg);
}

/*
 * 완료 조건 6 — 크기 계산이 넘치지 않는다.
 *
 * `rec_count * rec_size`를 곱한 **뒤에** 검사하면 이미 늦다. 부호 없는 곱셈은
 * 조용히 감싸 돌아 작은 값이 되어 나오고, 그러면 작은 세그먼트를 잡아 놓고
 * 큰 인덱스로 접근하게 된다.
 */
static void test_size_overflow_rejected(void)
{
    int32_t big_count[SHM_REGION_COUNT] = {INT32_MAX, 1};
    size_t  ok_size[SHM_REGION_COUNT] = {sizeof(probe_rec_t),
                                         sizeof(probe_rec_t)};
    assert(shm_create(big_count, ok_size) == NULL);

    int32_t ok_count[SHM_REGION_COUNT] = {1, 1};
    size_t  big_size[SHM_REGION_COUNT] = {SIZE_MAX / 2, sizeof(probe_rec_t)};
    assert(shm_create(ok_count, big_size) == NULL);

    /* 둘 다 커서 곱하면 감싸 도는 조합. */
    int32_t c2[SHM_REGION_COUNT] = {1 << 20, 1};
    size_t  s2[SHM_REGION_COUNT] = {1 << 20, sizeof(probe_rec_t)};
    assert(shm_create(c2, s2) == NULL);

    /* 0이나 음수는 받지 않는다. */
    int32_t zero_count[SHM_REGION_COUNT] = {0, 1};
    assert(shm_create(zero_count, ok_size) == NULL);

    int32_t neg_count[SHM_REGION_COUNT] = {-1, 1};
    assert(shm_create(neg_count, ok_size) == NULL);

    size_t zero_size[SHM_REGION_COUNT] = {0, sizeof(probe_rec_t)};
    assert(shm_create(ok_count, zero_size) == NULL);

    assert(shm_create(NULL, ok_size) == NULL);
    assert(shm_create(ok_count, NULL) == NULL);

    /* 한도 바로 아래는 만들어진다 — 한도가 지나치게 빡빡하지 않다. */
    int32_t        okc[SHM_REGION_COUNT] = {1024, 1024};
    size_t         oks[SHM_REGION_COUNT] = {64, 64};
    shm_segment_t *seg = shm_create(okc, oks);
    assert(seg != NULL);
    assert(shm_size(seg) <= SHM_SIZE_MAX);
    shm_destroy(seg);
}

/*
 * 완료 조건 3 — 머리말이 형식을 검사한다.
 *
 * 머리말을 직접 망가뜨려 `shm_validate()`가 잡는지 본다. 세그먼트를 나중에
 * 다른 방식(이름 있는 공유 메모리)으로 열게 되면 **남이 만든 바이트**를 읽게
 * 되므로, 이 검사가 그때 첫 번째 방어선이 된다.
 */
static void test_validate_rejects_corruption(void)
{
    shm_segment_t *seg = shm_create(COUNTS, SIZES);
    assert(seg != NULL);
    assert(shm_validate(seg) == ERR_OK);

    shm_header_t *h = (shm_header_t *)shm_header(seg);

    uint32_t save32 = h->magic;
    h->magic = 0xDEADBEEF;
    assert(shm_validate(seg) == ERR_INVALID_ARG);
    h->magic = save32;
    assert(shm_validate(seg) == ERR_OK);

    /* 판이 다르면 배치가 다르다 — 해석하지 않는다. 코드도 구분한다. */
    save32 = h->version;
    h->version = SHM_VERSION + 1;
    assert(shm_validate(seg) == ERR_NOT_SUPPORTED);
    h->version = save32;
    assert(shm_validate(seg) == ERR_OK);

    /* 채우다 만 세그먼트. */
    save32 = h->initialized;
    h->initialized = 0;
    assert(shm_validate(seg) == ERR_INVALID_ARG);
    h->initialized = save32;
    assert(shm_validate(seg) == ERR_OK);

    /* 크기가 실제 매핑과 어긋난다. */
    uint64_t save64 = h->total_size;
    h->total_size = save64 + SHM_CACHELINE;
    assert(shm_validate(seg) == ERR_INVALID_ARG);
    h->total_size = save64;
    assert(shm_validate(seg) == ERR_OK);

    /* 영역이 캐시라인 경계에서 벗어난다. */
    save64 = h->rec_off[1];
    h->rec_off[1] = save64 + 1;
    assert(shm_validate(seg) == ERR_INVALID_ARG);
    h->rec_off[1] = save64;
    assert(shm_validate(seg) == ERR_OK);

    /*
     * **레코드 크기가 캐시라인 배수가 아니다.**
     *
     * 위의 오프셋 어긋남은 경계 검사(영역이 세그먼트를 넘는가)에도 걸리므로,
     * 정렬 검사를 통째로 지워도 통과한다. 크기를 1 줄이면 영역이 **더 작아져서**
     * 경계 검사는 멀쩡하고 겹치지도 않는다 — 오직 정렬 검사만 이것을 잡는다.
     */
    uint32_t save_size = h->rec_size[1];
    h->rec_size[1] = save_size - 1;
    assert(shm_validate(seg) == ERR_INVALID_ARG);
    h->rec_size[1] = save_size;
    assert(shm_validate(seg) == ERR_OK);

    /* 영역이 세그먼트 밖으로 삐져나간다. */
    save64 = h->rec_off[1];
    h->rec_off[1] = h->total_size;
    assert(shm_validate(seg) == ERR_INVALID_ARG);
    h->rec_off[1] = save64;
    assert(shm_validate(seg) == ERR_OK);

    /* 두 영역이 겹친다. */
    save64 = h->rec_off[1];
    h->rec_off[1] = h->rec_off[0];
    assert(shm_validate(seg) == ERR_INVALID_ARG);
    h->rec_off[1] = save64;
    assert(shm_validate(seg) == ERR_OK);

    /* 영역이 머리말을 덮는다. */
    save64 = h->rec_off[0];
    h->rec_off[0] = 0;
    assert(shm_validate(seg) == ERR_INVALID_ARG);
    h->rec_off[0] = save64;
    assert(shm_validate(seg) == ERR_OK);

    shm_destroy(seg);
}

/*
 * 완료 조건 5 — **초기화가 fork 전에 끝난다.**
 *
 * 자식은 `initialized`가 서 있고 검증을 통과하는 세그먼트만 본다. 자식이
 * "아직 준비 안 됨"을 만날 수 있다면 기다리는 코드가 필요했을 것이다.
 */
static void test_initialized_before_fork(void)
{
    shm_segment_t *seg = shm_create(COUNTS, SIZES);
    assert(seg != NULL);

    pid_t pid = fork();
    assert(pid >= 0);

    if (pid == 0) {
        const shm_header_t *h = shm_header(seg);
        if (h == NULL || h->initialized != 1) {
            _exit(20);
        }
        if (shm_validate(seg) != ERR_OK) {
            _exit(21);
        }
        /* 레코드도 바로 쓸 수 있다. */
        probe_rec_t *r = shm_record(seg, SHM_REGION_ORDER, 3);
        if (r == NULL) {
            _exit(22);
        }
        r->id = 999;
        shm_destroy(seg);
        _exit(0);
    }

    int status = 0;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);

    /* 자식이 쓴 것이 남아 있다. */
    probe_rec_t *r = shm_record(seg, SHM_REGION_ORDER, 3);
    assert(r != NULL && r->id == 999);

    shm_destroy(seg);
}

int main(void)
{
    STEP(test_shared_across_fork);
    STEP(test_layout);
    STEP(test_zeroed);
    STEP(test_bounds);
    STEP(test_size_overflow_rejected);
    STEP(test_validate_rejects_corruption);
    STEP(test_initialized_before_fork);
    return 0;
}
