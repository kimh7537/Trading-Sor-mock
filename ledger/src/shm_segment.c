#include "shm_segment.h"

#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#include "errors.h"

struct shm_segment {
    uint8_t *base; /* 매핑의 시작. 이 프로세스에서만 유효하다 */
    uint64_t size;
};

/* n을 a의 배수로 올린다. a는 2의 거듭제곱. */
static uint64_t align_up(uint64_t n, uint64_t a)
{
    return (n + a - 1) & ~(a - 1);
}

shm_segment_t *shm_create(const int32_t rec_count[SHM_REGION_COUNT],
                          const size_t rec_size[SHM_REGION_COUNT])
{
    if (rec_count == NULL || rec_size == NULL) {
        return NULL;
    }

    uint32_t padded[SHM_REGION_COUNT];
    uint64_t off[SHM_REGION_COUNT];

    uint64_t head = align_up(sizeof(shm_header_t), SHM_CACHELINE);
    uint64_t cur = head;

    for (int32_t r = 0; r < SHM_REGION_COUNT; r++) {
        if (rec_count[r] <= 0 || rec_size[r] == 0) {
            return NULL;
        }
        /*
         * 레코드 크기를 캐시라인 배수로 올린다. 그래야 이웃한 레코드가 같은
         * 캐시라인에 걸치지 않는다 — 서로 다른 워커의 쓰기가 서로를 느리게 만드는
         * 거짓 공유를 없앤다. 메모리를 조금 더 쓰는 것이 그 대가다.
         */
        uint64_t p = align_up((uint64_t)rec_size[r], SHM_CACHELINE);
        if (p > UINT32_MAX) {
            return NULL;
        }
        padded[r] = (uint32_t)p;

        /*
         * 넘침을 **곱하기 전에** 막는다. 곱한 뒤에 보면 이미 늦다 —
         * 부호 없는 곱셈은 조용히 감싸 돌아 작은 값이 되어 나온다.
         *
         * **지금 타입 범위에서는 이 검사가 실제로 발화하지 않는다.** rec_count가
         * int32(<= 2^31), p가 uint32(<= 2^32)라 곱이 2^63을 넘지 못하고, 아래의
         * `cur > SHM_SIZE_MAX`가 모든 도달 가능한 입력을 먼저 잡는다.
         * 변이를 넣어도 테스트가 구분하지 못하는 이유가 이것이다.
         *
         * 그래도 남긴다 — rec_count가 int64로 넓어지는 순간 살아나는 방어이고,
         * 없앴다가 그때 다시 넣는 것을 기억할 방법이 없다.
         */
        if ((uint64_t)rec_count[r] > SHM_SIZE_MAX / p) {
            return NULL;
        }

        off[r] = cur;
        cur += p * (uint64_t)rec_count[r];
        cur = align_up(cur, SHM_CACHELINE);

        if (cur > SHM_SIZE_MAX) {
            return NULL;
        }
    }

    /*
     * MAP_SHARED가 핵심이다. MAP_PRIVATE면 fork한 자식이 **제 사본**을 갖게 되어
     * 한쪽의 쓰기가 다른 쪽에 보이지 않는다 — 공유 메모리가 아니라 그냥 메모리다.
     */
    void *p = mmap(NULL, (size_t)cur, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        return NULL;
    }

    shm_segment_t *seg = calloc(1, sizeof(*seg));
    if (seg == NULL) {
        munmap(p, (size_t)cur);
        return NULL;
    }
    seg->base = p;
    seg->size = cur;

    /*
     * 레코드 영역을 0으로 민다. `MAP_ANONYMOUS`가 이미 0을 주므로 **지금은
     * 이 줄을 지워도 아무 차이가 없다** — 변이 검사에서 살아남는 이유다.
     *
     * 그래도 남긴다. 이름 있는 세그먼트로 바꾸면 이미 있던 세그먼트를 여는
     * 경우가 생기고, 그때는 앞선 실행의 찌꺼기가 그대로 보인다. 커널이 0을
     * 준다는 사실은 **매핑 방식의 성질**이지 이 코드의 계약이 아니다.
     */
    memset(seg->base + head, 0, (size_t)(cur - head));

    /*
     * 머리말을 채운다. **fork 전에 여기까지 끝난다** — 자식은 완성된 세그먼트만
     * 본다. 그래서 초기화 경합이 없다.
     */
    shm_header_t *h = (shm_header_t *)seg->base;
    memset(h, 0, sizeof(*h));
    h->magic = SHM_MAGIC;
    h->version = SHM_VERSION;
    h->total_size = cur;
    for (int32_t r = 0; r < SHM_REGION_COUNT; r++) {
        h->rec_count[r] = rec_count[r];
        h->rec_size[r] = padded[r];
        h->rec_off[r] = off[r];
    }

    /* 마지막에 세운다. 이 값이 서 있으면 나머지가 다 준비됐다는 뜻이다. */
    h->initialized = 1;

    return seg;
}

void shm_destroy(shm_segment_t *seg)
{
    if (seg == NULL) {
        return;
    }
    if (seg->base != NULL) {
        munmap(seg->base, (size_t)seg->size);
    }
    free(seg);
}

const shm_header_t *shm_header(const shm_segment_t *seg)
{
    if (seg == NULL || seg->base == NULL) {
        return NULL;
    }
    return (const shm_header_t *)seg->base;
}

uint64_t shm_size(const shm_segment_t *seg)
{
    return (seg != NULL) ? seg->size : 0;
}

void *shm_record(shm_segment_t *seg, int32_t region, int32_t index)
{
    if (seg == NULL || seg->base == NULL) {
        return NULL;
    }
    if (region < 0 || region >= SHM_REGION_COUNT) {
        return NULL;
    }

    const shm_header_t *h = (const shm_header_t *)seg->base;
    if (index < 0 || index >= h->rec_count[region]) {
        return NULL;
    }

    /* 오프셋에서 주소를 만든다. 주소를 세그먼트에 되돌려 넣지 않는다. */
    return seg->base + h->rec_off[region] +
           (uint64_t)h->rec_size[region] * (uint64_t)index;
}

void *shm_region_base(shm_segment_t *seg, int32_t region)
{
    return shm_record(seg, region, 0);
}

int shm_validate(const shm_segment_t *seg)
{
    if (seg == NULL || seg->base == NULL) {
        return ERR_NULL_PTR;
    }

    const shm_header_t *h = (const shm_header_t *)seg->base;

    if (h->magic != SHM_MAGIC) {
        return ERR_INVALID_ARG;
    }
    if (h->version != SHM_VERSION) {
        /* 판이 다르면 배치가 다르다. 해석하지 않는다(T3-01과 같은 규칙). */
        return ERR_NOT_SUPPORTED;
    }
    if (h->initialized != 1) {
        return ERR_INVALID_ARG; /* 채우다 만 세그먼트 */
    }
    if (h->total_size != seg->size || h->total_size > SHM_SIZE_MAX) {
        return ERR_INVALID_ARG;
    }

    for (int32_t r = 0; r < SHM_REGION_COUNT; r++) {
        if (h->rec_count[r] <= 0 || h->rec_size[r] == 0) {
            return ERR_INVALID_ARG;
        }
        /* 영역이 캐시라인 경계에 있어야 한다. */
        if (h->rec_off[r] % SHM_CACHELINE != 0 ||
            h->rec_size[r] % SHM_CACHELINE != 0) {
            return ERR_INVALID_ARG;
        }
        /* 머리말을 덮으면 안 된다. */
        if (h->rec_off[r] < sizeof(shm_header_t)) {
            return ERR_INVALID_ARG;
        }
        /* 영역이 세그먼트 밖으로 삐져나가면 안 된다. */
        uint64_t span = (uint64_t)h->rec_size[r] * (uint64_t)h->rec_count[r];
        if (h->rec_off[r] + span > h->total_size) {
            return ERR_INVALID_ARG;
        }
    }

    /* 영역끼리 겹치면 안 된다. */
    for (int32_t a = 0; a < SHM_REGION_COUNT; a++) {
        uint64_t a0 = h->rec_off[a];
        uint64_t a1 = a0 + (uint64_t)h->rec_size[a] * (uint64_t)h->rec_count[a];
        for (int32_t b = a + 1; b < SHM_REGION_COUNT; b++) {
            uint64_t b0 = h->rec_off[b];
            uint64_t b1 =
                b0 + (uint64_t)h->rec_size[b] * (uint64_t)h->rec_count[b];
            if (a0 < b1 && b0 < a1) {
                return ERR_INVALID_ARG;
            }
        }
    }

    return ERR_OK;
}
