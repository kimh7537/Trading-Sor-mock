#include "ordmap.h"

#include <assert.h>
#include <string.h>

#include "errors.h"

void ordmap_init(ordmap_t *m)
{
    if (m == NULL) {
        return;
    }
    memset(m, 0, sizeof(*m));
    m->next_seq = 1; /* 0은 "들어온 적 없음"과 헷갈린다 */
}

int32_t ordmap_count(const ordmap_t *m)
{
    return (m != NULL) ? m->count : 0;
}

int32_t ordmap_live_count(const ordmap_t *m)
{
    if (m == NULL) {
        return 0;
    }
    int32_t n = 0;
    for (int32_t i = 0; i < ORDMAP_MAX; i++) {
        /*
         * **판정 보류도 끝나지 않은 주문이다.** 빼면 "남은 게 없다"고 보고한
         * 뒤에 체결이 들어오는 일이 생긴다.
         */
        if (m->ent[i].state == ORD_PENDING || m->ent[i].state == ORD_LIVE ||
            m->ent[i].state == ORD_INDOUBT) {
            n++;
        }
    }
    return n;
}

/*
 * 선형 탐색이다. 헤더의 설명 참조 — 거래소 번호를 우리가 고를 수 없어서
 * T2-09의 산술 조회를 쓸 수 없다.
 */
static const ordent_t *find_cl_const(const ordmap_t *m, uint64_t cl_ord_id)
{
    if (cl_ord_id == 0) {
        return NULL;
    }
    for (int32_t i = 0; i < ORDMAP_MAX; i++) {
        if (m->ent[i].state != ORD_NONE && m->ent[i].cl_ord_id == cl_ord_id) {
            return &m->ent[i];
        }
    }
    return NULL;
}

static ordent_t *find_cl(ordmap_t *m, uint64_t cl_ord_id)
{
    if (cl_ord_id == 0) {
        return NULL;
    }
    for (int32_t i = 0; i < ORDMAP_MAX; i++) {
        if (m->ent[i].state != ORD_NONE && m->ent[i].cl_ord_id == cl_ord_id) {
            return &m->ent[i];
        }
    }
    return NULL;
}

const ordent_t *ordmap_find_by_cl(const ordmap_t *m, uint64_t cl_ord_id)
{
    if (m == NULL) {
        return NULL;
    }
    return find_cl_const(m, cl_ord_id);
}

const ordent_t *ordmap_find_by_exch(const ordmap_t *m, order_id_t exch_id)
{
    if (m == NULL || exch_id == 0) {
        return NULL;
    }
    for (int32_t i = 0; i < ORDMAP_MAX; i++) {
        /*
         * **PENDING도 빈 칸도 걸리지 않는다.** 둘 다 `exch_id`가 0인데 0으로
         * 찾는 일은 위에서 막았다. 그래서 상태를 따로 보지 않는다 —
         * 봐도 결과가 같아 변이 검사가 구분하지 못했다(O12가 살아남았다).
         */
        if (m->ent[i].exch_id == exch_id) {
            return &m->ent[i];
        }
    }
    return NULL;
}

/*
 * 빈 칸을 찾는다. 없으면 **끝난 주문 중 가장 오래된 것**을 비워서 내준다.
 * 끝난 것조차 없으면 NULL — 살아 있는 주문을 밀어내지 않는다.
 */
static ordent_t *take_slot(ordmap_t *m)
{
    ordent_t *victim = NULL;

    for (int32_t i = 0; i < ORDMAP_MAX; i++) {
        if (m->ent[i].state == ORD_NONE) {
            m->count++;
            return &m->ent[i];
        }
        if (m->ent[i].state == ORD_DONE &&
            (victim == NULL || m->ent[i].added_seq < victim->added_seq)) {
            victim = &m->ent[i];
        }
    }

    /*
     * 밀어낼 칸을 여기서 지우지 않는다. `ordmap_add`가 **구조체 전체를 한 번에
     * 대입**하므로 앞 주문의 흔적이 남을 자리가 없다 — 지정하지 않은 멤버는
     * C 규칙에 따라 0이 되고, 나중에 필드가 늘어도 그대로 적용된다.
     *
     * 여기서 한 번 더 지우면 안전해 보이지만 **동작이 같아 변이 검사가
     * 구분하지 못한다**(O4가 살아남았다). T3-08~12에서 되풀이한 판단이다.
     */
    return victim; /* count는 그대로 — 자리 수가 바뀌지 않았다 */
}

int ordmap_add(ordmap_t *m, uint64_t cl_ord_id)
{
    if (m == NULL) {
        return ERR_NULL_PTR;
    }
    if (cl_ord_id == 0) {
        return ERR_INVALID_ARG; /* 0은 "없음"의 뜻으로 쓴다 */
    }
    /*
     * **덮어쓰지 않는다.** 같은 번호를 두 주문에 쓰는 것은 호출부의 버그이고,
     * 덮으면 앞 주문의 체결이 갈 곳을 잃는다. 끝난 주문이어도 마찬가지다 —
     * 늦게 오는 체결이 새 주문에 붙으면 그 편이 훨씬 나쁘다.
     */
    if (find_cl(m, cl_ord_id) != NULL) {
        return ERR_DUPLICATE;
    }

    ordent_t *e = take_slot(m);
    if (e == NULL) {
        return ERR_POOL_EXHAUSTED;
    }

    /*
     * **한 번의 대입으로 칸 전체를 정의한다.** 밀려난 칸을 재사용할 때 앞
     * 주문의 값이 한 톨도 남지 않는 것이 이 한 줄에 달려 있다.
     */
    *e = (ordent_t){.state = ORD_PENDING,
                    .cl_ord_id = cl_ord_id,
                    .exch_id = 0,
                    .added_seq = m->next_seq++};

    return ERR_OK;
}

int ordmap_on_ack(ordmap_t *m, uint64_t cl_ord_id, order_id_t exch_id,
                  bool accepted)
{
    if (m == NULL) {
        return ERR_NULL_PTR;
    }

    ordent_t *e = find_cl(m, cl_ord_id);
    if (e == NULL) {
        return ERR_NOT_FOUND;
    }

    if (!accepted) {
        /*
         * 거부에는 거래소 번호가 없다. 실려 온 값이 있어도 쓰지 않는다 —
         * 거부된 주문의 번호로 체결이 올 수는 없으므로 기록할 이유가 없고,
         * 기록하면 그 번호가 다른 주문과 겹칠 여지만 생긴다.
         */
        e->state = ORD_DONE;
        e->exch_id = 0;
        return ERR_OK;
    }

    if (exch_id == 0) {
        return ERR_INVALID_ARG; /* 번호 없이 접수될 수는 없다 */
    }
    /*
     * 이미 다른 번호로 접수된 주문에 또 접수 응답이 오면 둘 중 하나가 틀렸다.
     * **덮어쓰지 않고 알린다** — 어느 쪽이 맞는지 이 계층은 모른다.
     * 같은 번호로 다시 오는 것(재전송, T3-12)은 받아 준다.
     */
    if (e->exch_id != 0 && e->exch_id != exch_id) {
        return ERR_DUPLICATE;
    }

    e->state = ORD_LIVE;
    e->exch_id = exch_id;

    return ERR_OK;
}

int ordmap_close(ordmap_t *m, uint64_t cl_ord_id)
{
    if (m == NULL) {
        return ERR_NULL_PTR;
    }

    ordent_t *e = find_cl(m, cl_ord_id);
    if (e == NULL) {
        return ERR_NOT_FOUND;
    }

    /*
     * 상태만 바꾸고 **거래소 번호는 남긴다.** 늦게 오는 체결이 이 번호로
     * 들어와야 제 주문을 찾는다 — 취소한 줄 알았던 주문에 체결이 하나 더
     * 붙는 일은 실제로 일어난다.
     */
    e->state = ORD_DONE;

    return ERR_OK;
}

int ordmap_cancel_key(const ordmap_t *m, uint64_t cl_ord_id,
                      order_id_t *out_exch_id)
{
    if (m == NULL || out_exch_id == NULL) {
        return ERR_NULL_PTR;
    }

    const ordent_t *e = find_cl_const(m, cl_ord_id);
    if (e == NULL) {
        return ERR_NOT_FOUND;
    }
    if (e->state == ORD_DONE) {
        return ERR_NOT_SUPPORTED; /* 호출부가 상태를 잘못 알고 있다 */
    }

    /*
     * PENDING이면 0이다. **0은 "우리 번호로 찾아라"라는 뜻**이고, `CANCEL_REQ`가
     * 두 번호를 모두 싣기 때문에 성립한다(헤더의 설명 참조).
     */
    *out_exch_id = e->exch_id;

    return ERR_OK;
}

/* --- T3-14: 끊겼을 때의 판정 --- */

int32_t ordmap_on_disconnect(ordmap_t *m)
{
    if (m == NULL) {
        return 0;
    }

    int32_t moved = 0;
    for (int32_t i = 0; i < ORDMAP_MAX; i++) {
        /*
         * **응답을 못 받은 것만 옮긴다.** 접수된(LIVE) 주문은 거래소에 있다는
         * 것을 아는 주문이라 보류할 이유가 없고, 끝난(DONE) 주문도 마찬가지다.
         * 이미 보류 중인 것은 그대로 둔다 — 조회가 또 끊겼다는 뜻이다.
         */
        if (m->ent[i].state == ORD_PENDING) {
            m->ent[i].state = ORD_INDOUBT;
            moved++;
        }
    }
    return moved;
}

int ordmap_on_query_result(ordmap_t *m, uint64_t cl_ord_id, order_id_t exch_id,
                           bool live)
{
    if (m == NULL) {
        return ERR_NULL_PTR;
    }

    ordent_t *e = find_cl(m, cl_ord_id);
    if (e == NULL) {
        /*
         * 우리가 보낸 적 없는 주문을 거래소가 알고 있다. 심각한 일이지만
         * **이 계층이 판단할 일이 아니다** — 알리고 위로 넘긴다.
         */
        return ERR_NOT_FOUND;
    }

    if (live) {
        if (exch_id == 0) {
            return ERR_INVALID_ARG; /* 살아 있다면서 번호가 없을 수는 없다 */
        }
        e->state = ORD_LIVE;
        e->exch_id = exch_id;
    } else {
        /*
         * 거래소가 알고는 있으나 살아 있지 않다(체결 완료·취소·거부).
         * 번호는 실려 온 대로 남긴다 — **늦게 오는 체결이 찾아올 수 있다.**
         */
        e->state = ORD_DONE;
        if (exch_id != 0) {
            e->exch_id = exch_id;
        }
    }

    return ERR_OK;
}

int32_t ordmap_finish_query(ordmap_t *m)
{
    if (m == NULL) {
        return 0;
    }

    int32_t resolved = 0;
    for (int32_t i = 0; i < ORDMAP_MAX; i++) {
        /*
         * 조회가 끝났는데도 보류로 남아 있다 = **거래소가 모르는 주문**이다.
         * 곧 전문이 닿지 않았다는 뜻이므로 주문은 없다.
         *
         * 이 결론은 "응답이 끝났다"를 알아야만 낼 수 있다. 그래서 이 함수는
         * 마지막 표시를 실제로 받았을 때만 불러야 한다 — 헤더의 설명 참조.
         *
         * 거래소 번호는 **이미 0이다.** 보류는 PENDING에서만 오고 PENDING은
         * 번호를 받은 적이 없다. 그래서 여기서 0을 넣는 것은 아무 일도 하지
         * 않는다 — 실제로 그 대입을 지워도 변이 검사가 구분하지 못했다(D3).
         *
         * 무의미한 대입 대신 **불변조건을 검사한다.** 언젠가 접수된 주문까지
         * 보류로 옮기는 변경이 들어오면 여기서 걸린다. 그때는 번호가 남아
         * 있어서 없는 주문에 체결이 붙는다.
         */
        if (m->ent[i].state == ORD_INDOUBT) {
            assert(m->ent[i].exch_id == 0);
            m->ent[i].state = ORD_DONE;
            resolved++;
        }
    }
    return resolved;
}

int32_t ordmap_indoubt_count(const ordmap_t *m)
{
    if (m == NULL) {
        return 0;
    }
    int32_t n = 0;
    for (int32_t i = 0; i < ORDMAP_MAX; i++) {
        if (m->ent[i].state == ORD_INDOUBT) {
            n++;
        }
    }
    return n;
}

const ordent_t *ordmap_next_indoubt(const ordmap_t *m, int32_t *cursor)
{
    if (m == NULL || cursor == NULL || *cursor < 0) {
        return NULL;
    }
    for (int32_t i = *cursor; i < ORDMAP_MAX; i++) {
        if (m->ent[i].state == ORD_INDOUBT) {
            *cursor = i + 1;
            return &m->ent[i];
        }
    }
    *cursor = ORDMAP_MAX;
    return NULL;
}
