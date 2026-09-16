/*
 * T3-13 FEP — 주문번호 매핑.
 *
 * 완료 조건을 그대로 옮긴다.
 *  1. 양방향 조회 — 우리 번호로, 거래소 번호로
 *  2. **응답 전 취소는 우리 번호로 보낸다**(거래소 번호 자리에 0)
 *  3. 거부된 주문에는 거래소 번호가 없다
 *  4. 같은 번호를 다시 등록하면 **덮지 않고 거절한다**
 *  5. 표가 가득 차면 **끝난 것부터** 밀어내고, 살아 있는 것은 밀어내지 않는다
 *  6. **끝난 주문에 늦게 온 체결**도 제 주문을 찾는다
 *
 * 5번과 6번이 이 태스크의 조용한 위험이다. 살아 있는 주문의 매핑을 잃으면
 * 그 주문의 체결이 어디에도 붙지 않고, 끝난 주문을 곧바로 지우면 늦게 온
 * 체결이 갈 곳을 잃는다.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "ordmap.h"

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

/* ORDMAP_MAX개짜리 표라 스택에 두지 않는다. */
static ordmap_t g_m;

/* --- 1. 양방향 조회 --- */

static void test_round_trip(void)
{
    ordmap_init(&g_m);
    assert(ordmap_count(&g_m) == 0);
    assert(ordmap_live_count(&g_m) == 0);

    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_count(&g_m) == 1);
    assert(ordmap_live_count(&g_m) == 1);

    /* 응답 전 — 우리 번호로는 찾히고, 거래소 번호로는 찾을 게 없다. */
    const ordent_t *e = ordmap_find_by_cl(&g_m, 1001);
    assert(e != NULL);
    assert(e->state == ORD_PENDING);
    assert(e->exch_id == 0);

    assert(ordmap_on_ack(&g_m, 1001, 9001, true) == ERR_OK);

    e = ordmap_find_by_cl(&g_m, 1001);
    assert(e != NULL && e->state == ORD_LIVE && e->exch_id == 9001);

    /* 이제 거래소 번호로도 찾힌다. 체결 통보가 이 길로 들어온다. */
    e = ordmap_find_by_exch(&g_m, 9001);
    assert(e != NULL && e->cl_ord_id == 1001);

    assert(ordmap_find_by_exch(&g_m, 9002) == NULL);
    assert(ordmap_find_by_cl(&g_m, 1002) == NULL);
}

/*
 * 응답 전 주문은 거래소 번호로 **찾히면 안 된다.** 0으로 조회하면 PENDING
 * 항목이 줄줄이 걸리는 실수가 나기 쉽다.
 */
static void test_pending_not_found_by_exch(void)
{
    ordmap_init(&g_m);

    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_add(&g_m, 1002) == ERR_OK);

    assert(ordmap_find_by_exch(&g_m, 0) == NULL);
}

/* --- 2. 응답 전 취소 --- */

static void test_cancel_key_pending_uses_zero(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);

    order_id_t key = 12345; /* 덮어쓰는지 보려고 쓰레기를 넣어 둔다 */
    assert(ordmap_cancel_key(&g_m, 1001, &key) == ERR_OK);
    /*
     * **0이다.** "거래소 번호를 모르니 우리 번호로 찾아라"라는 뜻이고,
     * CANCEL_REQ가 두 번호를 모두 싣기 때문에 성립한다.
     */
    assert(key == 0);

    /* 접수되면 그때부터 거래소 번호를 쓴다. */
    assert(ordmap_on_ack(&g_m, 1001, 9001, true) == ERR_OK);
    assert(ordmap_cancel_key(&g_m, 1001, &key) == ERR_OK);
    assert(key == 9001);
}

static void test_cancel_key_rejects_done_and_unknown(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_on_ack(&g_m, 1001, 9001, true) == ERR_OK);
    assert(ordmap_close(&g_m, 1001) == ERR_OK);

    order_id_t key = 7;
    /* 끝난 주문을 취소하겠다는 것은 호출부가 상태를 잘못 알고 있다는 뜻이다. */
    assert(ordmap_cancel_key(&g_m, 1001, &key) == ERR_NOT_SUPPORTED);
    assert(key == 7); /* 건드리지 않았다 */

    assert(ordmap_cancel_key(&g_m, 4242, &key) == ERR_NOT_FOUND);
    assert(key == 7);
}

/* --- 3. 거부된 주문 --- */

static void test_rejected_has_no_exchange_id(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);

    /* 거부인데 번호가 실려 와도 쓰지 않는다. */
    assert(ordmap_on_ack(&g_m, 1001, 9001, false) == ERR_OK);

    const ordent_t *e = ordmap_find_by_cl(&g_m, 1001);
    assert(e != NULL && e->state == ORD_DONE);
    assert(e->exch_id == 0);

    /* 그 번호로는 아무것도 찾히지 않는다. */
    assert(ordmap_find_by_exch(&g_m, 9001) == NULL);
    assert(ordmap_live_count(&g_m) == 0);
}

/* 접수인데 번호가 0이면 받지 않는다 */
static void test_accept_without_id_rejected(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);

    assert(ordmap_on_ack(&g_m, 1001, 0, true) == ERR_INVALID_ARG);

    const ordent_t *e = ordmap_find_by_cl(&g_m, 1001);
    assert(e != NULL && e->state == ORD_PENDING); /* 그대로다 */

    assert(ordmap_on_ack(&g_m, 4242, 9001, true) == ERR_NOT_FOUND);
}

/*
 * 같은 번호로 다시 오는 접수 응답은 받아 준다(T3-12의 재전송).
 * **다른 번호로 오면 받지 않는다** — 둘 중 하나가 틀렸고 이 계층은 모른다.
 */
static void test_reack(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_on_ack(&g_m, 1001, 9001, true) == ERR_OK);

    assert(ordmap_on_ack(&g_m, 1001, 9001, true) == ERR_OK); /* 재전송 */
    assert(ordmap_on_ack(&g_m, 1001, 9999, true) == ERR_DUPLICATE);

    const ordent_t *e = ordmap_find_by_cl(&g_m, 1001);
    assert(e != NULL && e->exch_id == 9001); /* 덮이지 않았다 */
}

/* --- 4. 같은 번호 재등록 --- */

static void test_duplicate_add_rejected(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_on_ack(&g_m, 1001, 9001, true) == ERR_OK);

    assert(ordmap_add(&g_m, 1001) == ERR_DUPLICATE);
    assert(ordmap_count(&g_m) == 1);

    /* 앞 주문이 그대로다. */
    const ordent_t *e = ordmap_find_by_cl(&g_m, 1001);
    assert(e != NULL && e->state == ORD_LIVE && e->exch_id == 9001);

    /*
     * 끝난 주문이어도 같은 번호는 받지 않는다. 늦게 오는 체결이 새 주문에
     * 붙으면 그 편이 훨씬 나쁘다.
     */
    assert(ordmap_close(&g_m, 1001) == ERR_OK);
    assert(ordmap_add(&g_m, 1001) == ERR_DUPLICATE);
}

/* --- 5. 표가 가득 찼을 때 --- */

/* 살아 있는 주문으로 가득 차면 거절한다. 밀어내지 않는다. */
static void test_full_of_live_rejects(void)
{
    ordmap_init(&g_m);

    for (int i = 0; i < ORDMAP_MAX; i++) {
        assert(ordmap_add(&g_m, (uint64_t)(1000 + i)) == ERR_OK);
    }
    assert(ordmap_count(&g_m) == ORDMAP_MAX);
    assert(ordmap_live_count(&g_m) == ORDMAP_MAX);

    assert(ordmap_add(&g_m, 999999) == ERR_POOL_EXHAUSTED);

    /* **아무것도 잃지 않았다.** */
    assert(ordmap_live_count(&g_m) == ORDMAP_MAX);
    for (int i = 0; i < ORDMAP_MAX; i++) {
        assert(ordmap_find_by_cl(&g_m, (uint64_t)(1000 + i)) != NULL);
    }
}

/* 끝난 것이 있으면 그중 가장 오래된 것부터 밀어낸다. */
static void test_full_evicts_oldest_done(void)
{
    ordmap_init(&g_m);

    for (int i = 0; i < ORDMAP_MAX; i++) {
        assert(ordmap_add(&g_m, (uint64_t)(1000 + i)) == ERR_OK);
    }

    /* 셋을 끝낸다. 들어온 차례는 1000 < 1500 < 2000이다. */
    assert(ordmap_close(&g_m, 2000) == ERR_OK);
    assert(ordmap_close(&g_m, 1000) == ERR_OK);
    assert(ordmap_close(&g_m, 1500) == ERR_OK);

    /* 가장 먼저 들어온 1000이 먼저 밀려난다 — 닫은 순서가 아니라 들어온 순서다. */
    assert(ordmap_add(&g_m, 999001) == ERR_OK);
    assert(ordmap_find_by_cl(&g_m, 1000) == NULL);
    assert(ordmap_find_by_cl(&g_m, 1500) != NULL);
    assert(ordmap_find_by_cl(&g_m, 2000) != NULL);

    assert(ordmap_add(&g_m, 999002) == ERR_OK);
    assert(ordmap_find_by_cl(&g_m, 1500) == NULL);
    assert(ordmap_find_by_cl(&g_m, 2000) != NULL);

    assert(ordmap_add(&g_m, 999003) == ERR_OK);
    assert(ordmap_find_by_cl(&g_m, 2000) == NULL);

    /* 이제 끝난 것이 없으므로 거절한다. */
    assert(ordmap_add(&g_m, 999004) == ERR_POOL_EXHAUSTED);
    assert(ordmap_count(&g_m) == ORDMAP_MAX);
}

/*
 * **"가장 오래된 것"과 "처음 만난 것"은 다르다.**
 *
 * 위 테스트는 표를 순서대로 채워서 색인 순서와 들어온 순서가 같았다. 그러면
 * 그냥 처음 만난 끝난 주문을 밀어내도 통과한다 — 실제로 그 변이가 살아남았다
 * (O3). 둘을 어긋나게 만들어야 비로소 갈린다.
 *
 * 한 칸을 밀어내고 그 자리에 새 주문을 넣으면 **낮은 색인에 가장 나중에 들어온
 * 주문**이 앉는다. 그 상태에서 고르게 하면 순서를 보는지 색인을 보는지 갈린다.
 */
static void test_evicts_by_age_not_by_index(void)
{
    ordmap_init(&g_m);

    for (int i = 0; i < ORDMAP_MAX; i++) {
        assert(ordmap_add(&g_m, (uint64_t)(1000 + i)) == ERR_OK);
    }

    /* 0번 칸을 비우고 그 자리에 가장 나중 주문을 앉힌다. */
    assert(ordmap_close(&g_m, 1000) == ERR_OK);
    assert(ordmap_add(&g_m, 900001) == ERR_OK);
    assert(ordmap_find_by_cl(&g_m, 1000) == NULL);

    /*
     * 이제 끝난 주문이 둘이다.
     *   - 900001: 0번 칸(가장 낮은 색인), **가장 나중에 들어왔다**
     *   - 마지막 주문: 가장 높은 색인, **더 먼저 들어왔다**
     */
    uint64_t last_cl = (uint64_t)(1000 + ORDMAP_MAX - 1);
    assert(ordmap_close(&g_m, 900001) == ERR_OK);
    assert(ordmap_close(&g_m, last_cl) == ERR_OK);

    assert(ordmap_add(&g_m, 900002) == ERR_OK);

    /* **더 먼저 들어온 쪽**이 밀려난다. 색인을 봤다면 900001이 밀려났을 것이다. */
    assert(ordmap_find_by_cl(&g_m, last_cl) == NULL);
    assert(ordmap_find_by_cl(&g_m, 900001) != NULL);
}

/*
 * 밀려난 자리에 **앞 주문의 흔적이 남으면 안 된다.** 남으면 그 거래소 번호로
 * 들어온 체결이 엉뚱한 주문에 붙는다 — T3-12의 고리 버퍼와 같은 위험이다.
 */
static void test_evicted_slot_is_clean(void)
{
    ordmap_init(&g_m);

    for (int i = 0; i < ORDMAP_MAX; i++) {
        assert(ordmap_add(&g_m, (uint64_t)(1000 + i)) == ERR_OK);
        assert(ordmap_on_ack(&g_m, (uint64_t)(1000 + i),
                             (order_id_t)(50000 + i), true) == ERR_OK);
    }
    assert(ordmap_close(&g_m, 1000) == ERR_OK);

    assert(ordmap_add(&g_m, 999001) == ERR_OK);

    /* 밀려난 주문의 거래소 번호는 더 이상 아무것도 가리키지 않는다. */
    assert(ordmap_find_by_exch(&g_m, 50000) == NULL);
    assert(ordmap_find_by_cl(&g_m, 1000) == NULL);

    /* 새 주문은 깨끗하게 시작한다. */
    const ordent_t *e = ordmap_find_by_cl(&g_m, 999001);
    assert(e != NULL && e->state == ORD_PENDING && e->exch_id == 0);
}

/* --- 6. 끝난 주문에 늦게 온 체결 --- */

static void test_late_fill_finds_closed_order(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_on_ack(&g_m, 1001, 9001, true) == ERR_OK);
    assert(ordmap_close(&g_m, 1001) == ERR_OK);

    /*
     * 취소한 줄 알았던 주문에 체결이 하나 더 붙는 일은 실제로 일어난다.
     * **닫았다고 지우지 않았으므로** 그 체결이 제 주문을 찾는다.
     */
    const ordent_t *e = ordmap_find_by_exch(&g_m, 9001);
    assert(e != NULL);
    assert(e->cl_ord_id == 1001);
    assert(e->state == ORD_DONE); /* 끝난 주문임을 호출부가 알 수 있다 */

    assert(ordmap_live_count(&g_m) == 0);
    assert(ordmap_count(&g_m) == 1);
}

/* ===================================================================== */
/* --- T3-14: 세션 단절 시 미응답 주문 판정 --- */
/* ===================================================================== */

/*
 * 끊긴 순간 **응답 못 받은 것만** 판정 보류가 된다.
 * 접수된 주문은 거래소에 있다는 것을 아는 주문이라 보류할 이유가 없다.
 */
static void test_disconnect_marks_only_pending(void)
{
    ordmap_init(&g_m);

    assert(ordmap_add(&g_m, 1001) == ERR_OK); /* 응답 전 */
    assert(ordmap_add(&g_m, 1002) == ERR_OK);
    assert(ordmap_on_ack(&g_m, 1002, 9002, true) == ERR_OK); /* 접수됨 */
    assert(ordmap_add(&g_m, 1003) == ERR_OK);
    assert(ordmap_on_ack(&g_m, 1003, 0, false) == ERR_OK); /* 거부됨 */
    assert(ordmap_add(&g_m, 1004) == ERR_OK);              /* 응답 전 */

    assert(ordmap_on_disconnect(&g_m) == 2); /* 1001, 1004 */

    assert(ordmap_find_by_cl(&g_m, 1001)->state == ORD_INDOUBT);
    assert(ordmap_find_by_cl(&g_m, 1004)->state == ORD_INDOUBT);
    assert(ordmap_find_by_cl(&g_m, 1002)->state == ORD_LIVE); /* 그대로 */
    assert(ordmap_find_by_cl(&g_m, 1003)->state == ORD_DONE); /* 그대로 */

    assert(ordmap_indoubt_count(&g_m) == 2);
    /* **판정 보류도 끝나지 않은 주문이다.** 1001, 1002, 1004. */
    assert(ordmap_live_count(&g_m) == 3);
}

/*
 * 조회로 판정한다. 세 가지가 한 번에 갈린다 —
 * 살아 있었다 / 이미 끝나 있었다 / **거래소가 모른다**.
 */
static void test_query_resolves_three_ways(void)
{
    ordmap_init(&g_m);

    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_add(&g_m, 1002) == ERR_OK);
    assert(ordmap_add(&g_m, 1003) == ERR_OK);
    assert(ordmap_on_disconnect(&g_m) == 3);

    /* 다시 붙어 당일 전체를 조회한다. 응답이 둘 온다. */
    assert(ordmap_on_query_result(&g_m, 1001, 9001, true) == ERR_OK);
    assert(ordmap_on_query_result(&g_m, 1002, 9002, false) == ERR_OK);

    /* 아직 끝나지 않았으므로 1003은 그대로 보류다. */
    assert(ordmap_indoubt_count(&g_m) == 1);
    assert(ordmap_find_by_cl(&g_m, 1003)->state == ORD_INDOUBT);

    /* 마지막 표시를 받았다. 이제 비로소 "없다"를 결론 낼 수 있다. */
    assert(ordmap_finish_query(&g_m) == 1);

    assert(ordmap_find_by_cl(&g_m, 1001)->state == ORD_LIVE);
    assert(ordmap_find_by_cl(&g_m, 1001)->exch_id == 9001);
    /* 끝난 주문의 번호는 남긴다 — 늦게 오는 체결이 찾아올 수 있다. */
    assert(ordmap_find_by_cl(&g_m, 1002)->state == ORD_DONE);
    assert(ordmap_find_by_cl(&g_m, 1002)->exch_id == 9002);
    /* 없는 주문에는 번호가 없다. */
    assert(ordmap_find_by_cl(&g_m, 1003)->state == ORD_DONE);
    assert(ordmap_find_by_cl(&g_m, 1003)->exch_id == 0);

    assert(ordmap_indoubt_count(&g_m) == 0);
    assert(ordmap_live_count(&g_m) == 1); /* 1001만 살아 있다 */
}

/* 판정된 주문은 거래소 번호로 다시 찾힌다 — 체결 통보가 붙을 수 있다. */
static void test_resolved_order_receives_fills(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_on_disconnect(&g_m) == 1);

    /* 보류 중에는 번호가 없으므로 체결이 붙을 데가 없다. */
    assert(ordmap_find_by_exch(&g_m, 9001) == NULL);

    assert(ordmap_on_query_result(&g_m, 1001, 9001, true) == ERR_OK);

    const ordent_t *e = ordmap_find_by_exch(&g_m, 9001);
    assert(e != NULL && e->cl_ord_id == 1001 && e->state == ORD_LIVE);
}

/*
 * **조회가 또 끊기면 보류가 그대로 남는다.**
 * 여기서 `finish_query`를 부르면 아직 안 온 주문을 없다고 판정한다 —
 * 그래서 마지막 표시를 받았을 때만 부른다.
 */
static void test_disconnect_during_query_keeps_indoubt(void)
{
    ordmap_init(&g_m);

    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_add(&g_m, 1002) == ERR_OK);
    assert(ordmap_on_disconnect(&g_m) == 2);

    /* 하나만 답을 받고 끊겼다. */
    assert(ordmap_on_query_result(&g_m, 1001, 9001, true) == ERR_OK);

    /* 마지막 표시를 못 받았으므로 `finish_query`를 부르지 않는다. */
    int32_t again = ordmap_on_disconnect(&g_m);
    /*
     * 1001은 이 접속에서 접수 확인을 받았으므로 다시 보류가 된다 —
     * 그 확인은 끊긴 접속의 것이고, 접수 자체는 거래소가 답한 사실이라
     * 살아 있다. 여기서 옮겨지는 것은 **새로 PENDING이 된 것**뿐이다.
     */
    assert(again == 0);

    /* 1002는 여전히 보류다. **잃어버리지 않았다.** */
    assert(ordmap_indoubt_count(&g_m) == 1);
    assert(ordmap_find_by_cl(&g_m, 1002)->state == ORD_INDOUBT);
    assert(ordmap_find_by_cl(&g_m, 1001)->state == ORD_LIVE);

    /* 다시 붙어 조회를 끝까지 받으면 그때 판정된다. */
    assert(ordmap_on_query_result(&g_m, 1002, 9002, true) == ERR_OK);
    assert(ordmap_finish_query(&g_m) == 0);
    assert(ordmap_indoubt_count(&g_m) == 0);
}

/* 보류 중인 것을 하나씩 꺼내 보고할 수 있다. */
static void test_iterate_indoubt(void)
{
    ordmap_init(&g_m);

    for (uint64_t i = 1; i <= 5; i++) {
        assert(ordmap_add(&g_m, 1000 + i) == ERR_OK);
    }
    /* 둘은 접수 확인을 받아 둔다. */
    assert(ordmap_on_ack(&g_m, 1002, 9002, true) == ERR_OK);
    assert(ordmap_on_ack(&g_m, 1004, 9004, true) == ERR_OK);

    assert(ordmap_on_disconnect(&g_m) == 3); /* 1001, 1003, 1005 */

    int32_t         cursor = 0;
    uint64_t        seen[8];
    int32_t         n = 0;
    const ordent_t *e;
    while ((e = ordmap_next_indoubt(&g_m, &cursor)) != NULL) {
        assert(n < 8);
        seen[n++] = e->cl_ord_id;
    }
    assert(n == 3);

    /* 셋이 정확히 그 셋이다. */
    bool got1001 = false, got1003 = false, got1005 = false;
    for (int32_t i = 0; i < n; i++) {
        if (seen[i] == 1001) got1001 = true;
        if (seen[i] == 1003) got1003 = true;
        if (seen[i] == 1005) got1005 = true;
    }
    assert(got1001 && got1003 && got1005);
}

/* 거래소가 우리가 모르는 주문을 답해 오면 알린다 */
static void test_query_result_unknown_order(void)
{
    ordmap_init(&g_m);
    assert(ordmap_add(&g_m, 1001) == ERR_OK);
    assert(ordmap_on_disconnect(&g_m) == 1);

    assert(ordmap_on_query_result(&g_m, 4242, 9999, true) == ERR_NOT_FOUND);
    /* 우리 상태는 건드려지지 않았다. */
    assert(ordmap_indoubt_count(&g_m) == 1);

    /* 살아 있다면서 번호가 없을 수는 없다. */
    assert(ordmap_on_query_result(&g_m, 1001, 0, true) == ERR_INVALID_ARG);
    assert(ordmap_find_by_cl(&g_m, 1001)->state == ORD_INDOUBT);
}

static void test_indoubt_args(void)
{
    int32_t cursor = 0;

    assert(ordmap_on_disconnect(NULL) == 0);
    assert(ordmap_finish_query(NULL) == 0);
    assert(ordmap_indoubt_count(NULL) == 0);
    assert(ordmap_on_query_result(NULL, 1, 1, true) == ERR_NULL_PTR);
    assert(ordmap_next_indoubt(NULL, &cursor) == NULL);

    ordmap_init(&g_m);
    assert(ordmap_next_indoubt(&g_m, NULL) == NULL);
    cursor = -1;
    assert(ordmap_next_indoubt(&g_m, &cursor) == NULL);
}

/* --- 인자 --- */

static void test_args(void)
{
    ordmap_init(&g_m);
    order_id_t key = 0;

    assert(ordmap_add(NULL, 1) == ERR_NULL_PTR);
    assert(ordmap_add(&g_m, 0) == ERR_INVALID_ARG); /* 0은 "없음"이다 */

    assert(ordmap_on_ack(NULL, 1, 1, true) == ERR_NULL_PTR);
    assert(ordmap_close(NULL, 1) == ERR_NULL_PTR);
    assert(ordmap_close(&g_m, 4242) == ERR_NOT_FOUND);

    assert(ordmap_cancel_key(NULL, 1, &key) == ERR_NULL_PTR);
    assert(ordmap_cancel_key(&g_m, 1, NULL) == ERR_NULL_PTR);

    assert(ordmap_find_by_cl(NULL, 1) == NULL);
    assert(ordmap_find_by_cl(&g_m, 0) == NULL);
    assert(ordmap_find_by_exch(NULL, 1) == NULL);
    assert(ordmap_find_by_exch(&g_m, 0) == NULL);

    assert(ordmap_count(NULL) == 0);
    assert(ordmap_live_count(NULL) == 0);

    ordmap_init(NULL); /* 죽지 않는다 */
}

int main(void)
{
    STEP(test_round_trip);
    STEP(test_pending_not_found_by_exch);
    STEP(test_cancel_key_pending_uses_zero);
    STEP(test_cancel_key_rejects_done_and_unknown);
    STEP(test_rejected_has_no_exchange_id);
    STEP(test_accept_without_id_rejected);
    STEP(test_reack);
    STEP(test_duplicate_add_rejected);
    STEP(test_full_of_live_rejects);
    STEP(test_full_evicts_oldest_done);
    STEP(test_evicts_by_age_not_by_index);
    STEP(test_evicted_slot_is_clean);
    STEP(test_late_fill_finds_closed_order);
    STEP(test_disconnect_marks_only_pending);
    STEP(test_query_resolves_three_ways);
    STEP(test_resolved_order_receives_fills);
    STEP(test_disconnect_during_query_keeps_indoubt);
    STEP(test_iterate_indoubt);
    STEP(test_query_result_unknown_order);
    STEP(test_indoubt_args);
    STEP(test_args);
    return 0;
}
