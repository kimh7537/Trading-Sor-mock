#include "account.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "errors.h"

/* --- 저장소 --- */

int acct_store_init(account_store_t *store, shm_segment_t *seg)
{
    if (store == NULL || seg == NULL) {
        return ERR_NULL_PTR;
    }

    const shm_header_t *h = shm_header(seg);
    if (h == NULL) {
        return ERR_NULL_PTR;
    }
    /*
     * 세그먼트가 계좌 레코드를 담을 만큼 넓은지 본다. 좁은 채로 쓰면 이웃
     * 레코드를 밟는다 — 공유 메모리에서 그런 손상은 원인을 찾기 어렵다.
     */
    if (h->rec_size[SHM_REGION_ACCOUNT] < sizeof(account_t)) {
        return ERR_INVALID_ARG;
    }

    store->seg = seg;
    store->capacity = h->rec_count[SHM_REGION_ACCOUNT];

    /*
     * 뮤텍스를 만든다. **fork 전에 여기까지 끝난다**(T3-05의 규칙과 같다).
     *
     * 두 속성이 핵심이다.
     *  - PTHREAD_PROCESS_SHARED: 뮤텍스를 프로세스 사이에서 쓰겠다는 선언.
     *    POSIX는 이것 없이 프로세스를 넘나드는 사용을 정의하지 않는다
     *  - PTHREAD_MUTEX_ROBUST: 쥔 채로 죽은 락을 다음 사람이 회수할 수 있게 한다
     *
     * **주의 — pshared를 빼도 이 리눅스에서는 테스트가 통과한다.** 경합을 만들어
     * 실제로 막아 세우는 시험까지 해 봤지만 구분되지 않았다. robust 속성이 이미
     * 프로세스 간 동작을 요구하기 때문으로 보인다.
     *
     * 그래도 **뺄 수 없다.** 통과하는 것은 이 구현에서의 사정이고, 규격이 보장하는
     * 것이 아니다. "지금 이 기계에서는 된다"에 기대는 코드가 나중에 조용히
     * 깨진다는 것이 이 프로젝트가 되풀이해서 배운 것이다(T3-01의 패딩·엔디언과
     * 같은 종류의 문제다).
     */
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0) {
        return ERR_INVALID_ARG;
    }

    int rc = ERR_OK;
    if (pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED) != 0 ||
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST) != 0) {
        rc = ERR_NOT_SUPPORTED;
    }

    if (rc == ERR_OK) {
        for (int32_t i = 0; i < store->capacity; i++) {
            account_t *a = shm_record(seg, SHM_REGION_ACCOUNT, i);
            if (a == NULL) {
                rc = ERR_INVALID_ARG;
                break;
            }
            memset(a, 0, sizeof(*a));
            if (pthread_mutex_init(&a->lock, &attr) != 0) {
                rc = ERR_NOT_SUPPORTED;
                break;
            }
        }
    }

    pthread_mutexattr_destroy(&attr);
    return rc;
}

void acct_store_destroy(account_store_t *store)
{
    if (store == NULL || store->seg == NULL) {
        return;
    }
    for (int32_t i = 0; i < store->capacity; i++) {
        account_t *a = shm_record(store->seg, SHM_REGION_ACCOUNT, i);
        if (a != NULL) {
            pthread_mutex_destroy(&a->lock);
        }
    }
    store->seg = NULL;
    store->capacity = 0;
}

int32_t acct_capacity(const account_store_t *store)
{
    return (store != NULL) ? store->capacity : 0;
}

account_t *acct_at(account_store_t *store, int32_t index)
{
    if (store == NULL || store->seg == NULL) {
        return NULL;
    }
    if (index < 0 || index >= store->capacity) {
        return NULL;
    }
    return shm_record(store->seg, SHM_REGION_ACCOUNT, index);
}

int acct_find(const account_store_t *store, const char *account_no)
{
    if (store == NULL || store->seg == NULL || account_no == NULL) {
        return ERR_NULL_PTR;
    }

    for (int32_t i = 0; i < store->capacity; i++) {
        const account_t *a = shm_record(store->seg, SHM_REGION_ACCOUNT, i);
        if (a != NULL && a->in_use != 0 &&
            strncmp(a->account_no, account_no, ACCT_NO_LEN) == 0) {
            return i;
        }
    }
    return ERR_NOT_FOUND;
}

int acct_open(account_store_t *store, const char *account_no)
{
    if (store == NULL || store->seg == NULL || account_no == NULL) {
        return ERR_NULL_PTR;
    }
    if (account_no[0] == '\0') {
        return ERR_INVALID_ARG;
    }

    int found = acct_find(store, account_no);
    if (found >= 0) {
        return found;
    }

    for (int32_t i = 0; i < store->capacity; i++) {
        account_t *a = shm_record(store->seg, SHM_REGION_ACCOUNT, i);
        if (a != NULL && a->in_use == 0) {
            /* 락은 이미 만들어져 있다. 다시 만들면 대기자를 잃는다. */
            memset(a->account_no, 0, sizeof(a->account_no));
            strncpy(a->account_no, account_no, ACCT_NO_LEN);
            a->account_no[ACCT_NO_LEN] = '\0';
            a->cash = 0;
            a->reserved = 0;
            a->version = 0;
            a->pos_qty = 0;
            a->pos_cost = 0;
            a->pos_reserved = 0;
            a->realized = 0;
            a->mutating = 0;
            a->pre_cash = 0;
            a->pre_reserved = 0;
            a->pre_pos_qty = 0;
            a->pre_pos_cost = 0;
            a->pre_pos_reserved = 0;
            a->pre_realized = 0;
            a->in_use = 1;
            return i;
        }
    }

    return ERR_POOL_EXHAUSTED;
}

/* --- 잠금 --- */

int acct_lock(account_store_t *store, int32_t index, bool *out_recovered)
{
    if (out_recovered != NULL) {
        *out_recovered = false;
    }

    account_t *a = acct_at(store, index);
    if (a == NULL) {
        return ERR_NULL_PTR;
    }

    int rc = pthread_mutex_lock(&a->lock);

    if (rc == EOWNERDEAD) {
        /*
         * 쥔 채로 죽었다. 락은 이미 이쪽 것이지만 데이터가 온전하다는 보장이 없다.
         *
         * 쓰기 중 표시가 서 있으면 **직전 값으로 되돌린다.** 죽은 워커가 하던
         * 일은 응답을 보내지 못했으므로 요청한 쪽은 실패로 본다 — 원장도 그 일이
         * 없던 것으로 두는 편이 앞뒤가 맞는다.
         */
        if (a->mutating != 0) {
            a->cash = a->pre_cash;
            a->reserved = a->pre_reserved;
            a->pos_qty = a->pre_pos_qty;
            a->pos_cost = a->pre_pos_cost;
            a->pos_reserved = a->pre_pos_reserved;
            a->realized = a->pre_realized;
            a->mutating = 0;
            if (out_recovered != NULL) {
                *out_recovered = true;
            }
        }

        /*
         * 이것을 부르지 않고 락을 놓으면 그 뮤텍스는 영구히 ENOTRECOVERABLE이
         * 된다 — 회수하려다 오히려 못 쓰게 만든다.
         */
        if (pthread_mutex_consistent(&a->lock) != 0) {
            pthread_mutex_unlock(&a->lock);
            return ERR_NOT_SUPPORTED;
        }
        return ERR_OK;
    }

    if (rc != 0) {
        /* ENOTRECOVERABLE 등. 이 계좌는 더 쓸 수 없다. */
        return ERR_NOT_SUPPORTED;
    }
    return ERR_OK;
}

int acct_unlock(account_store_t *store, int32_t index)
{
    account_t *a = acct_at(store, index);
    if (a == NULL) {
        return ERR_NULL_PTR;
    }
    return (pthread_mutex_unlock(&a->lock) == 0) ? ERR_OK : ERR_INVALID_ARG;
}

/* --- 잔고 --- */

/* 값을 바꾸기 전에 직전 값을 적어 둔다. */
static void begin_write(account_t *a)
{
    a->pre_cash = a->cash;
    a->pre_reserved = a->reserved;
    a->pre_pos_qty = a->pos_qty;
    a->pre_pos_cost = a->pos_cost;
    a->pre_pos_reserved = a->pos_reserved;
    a->pre_realized = a->realized;
    a->mutating = 1;
}

/* 다 바꿨다. 표시를 내린다. */
static void end_write(account_t *a)
{
    a->mutating = 0;
    a->version++;
}

/* 불변조건. 새 값이 이것을 어기면 반영하지 않는다. */
static bool invariant_ok(int64_t cash, int64_t reserved)
{
    return cash >= 0 && reserved >= 0 && reserved <= cash;
}

/*
 * 잠그고, 새 값을 만들고, 불변조건을 확인하고, 반영한다.
 *
 * 연산마다 이 뼈대를 되풀이하지 않으려고 한 군데에 모았다 — 되풀이하면
 * 한 곳에서 `end_write`를 빠뜨리는 식의 실수가 난다.
 */
static int apply(account_store_t *store, int32_t index, int64_t d_cash,
                 int64_t d_reserved, int64_t amount)
{
    if (amount <= 0) {
        return ERR_INVALID_QTY;
    }

    account_t *a = acct_at(store, index);
    if (a == NULL) {
        return ERR_NULL_PTR;
    }

    int rc = acct_lock(store, index, NULL);
    if (rc != ERR_OK) {
        return rc;
    }

    if (a->in_use == 0) {
        acct_unlock(store, index);
        return ERR_NOT_FOUND;
    }

    int64_t new_cash = a->cash + d_cash;
    int64_t new_reserved = a->reserved + d_reserved;

    if (!invariant_ok(new_cash, new_reserved)) {
        /* **아무것도 바꾸지 않았다.** 거절은 여기서 끝난다. */
        acct_unlock(store, index);
        return ERR_INVALID_QTY;
    }

    begin_write(a);
    a->cash = new_cash;
    a->reserved = new_reserved;
    end_write(a);

    acct_unlock(store, index);
    return ERR_OK;
}

int acct_deposit(account_store_t *store, int32_t index, int64_t amount)
{
    return apply(store, index, amount, 0, amount);
}

int acct_withdraw(account_store_t *store, int32_t index, int64_t amount)
{
    /* 묶인 돈은 못 뺀다 — reserved <= cash 조건이 그것을 막는다. */
    return apply(store, index, -amount, 0, amount);
}

int acct_reserve(account_store_t *store, int32_t index, int64_t amount)
{
    return apply(store, index, 0, amount, amount);
}

int acct_release(account_store_t *store, int32_t index, int64_t amount)
{
    return apply(store, index, 0, -amount, amount);
}

int acct_settle(account_store_t *store, int32_t index, int64_t amount)
{
    /* 묶인 것을 풀면서 예수금에서도 뺀다. 둘이 한 번에 움직인다. */
    return apply(store, index, -amount, -amount, amount);
}

int acct_snapshot(account_store_t *store, int32_t index, int64_t *out_cash,
                  int64_t *out_reserved)
{
    account_t *a = acct_at(store, index);
    if (a == NULL) {
        return ERR_NULL_PTR;
    }

    int rc = acct_lock(store, index, NULL);
    if (rc != ERR_OK) {
        return rc;
    }

    if (a->in_use == 0) {
        acct_unlock(store, index);
        return ERR_NOT_FOUND;
    }

    if (out_cash != NULL) {
        *out_cash = a->cash;
    }
    if (out_reserved != NULL) {
        *out_reserved = a->reserved;
    }

    acct_unlock(store, index);
    return ERR_OK;
}

int64_t acct_available(account_store_t *store, int32_t index)
{
    int64_t cash = 0;
    int64_t reserved = 0;

    if (acct_snapshot(store, index, &cash, &reserved) != ERR_OK) {
        return 0;
    }
    return cash - reserved;
}

/* --- 보유 종목 (T11-01) --- */

/* 보유 불변조건. 새 값이 이것을 어기면 반영하지 않는다. */
static bool pos_ok(int64_t qty, int64_t cost, int64_t reserved)
{
    return qty >= 0 && cost >= 0 && reserved >= 0 && reserved <= qty;
}

/*
 * 잠그고, 계산하고, 불변조건을 확인하고, 반영한다.
 *
 * 잔고 쪽 `apply()`와 같은 뼈대다. 합치지 않은 이유는 매도 체결이 **읽은 값으로
 * 실현 손익을 계산해야** 해서 단순한 증감이 아니기 때문이다.
 */
typedef enum { POS_BUY, POS_RESERVE, POS_RELEASE, POS_SELL, POS_SEED } pos_op_t;

static int pos_apply(account_store_t *store, int32_t index, pos_op_t op,
                     int64_t qty, int64_t price, int64_t realized_seed)
{
    account_t *a = acct_at(store, index);
    if (a == NULL) {
        return ERR_NULL_PTR;
    }
    if (op != POS_SEED && qty <= 0) {
        return ERR_INVALID_QTY;
    }
    if (op == POS_SEED && (qty < 0 || price < 0)) {
        return ERR_INVALID_QTY;
    }

    int rc = acct_lock(store, index, NULL);
    if (rc != ERR_OK) {
        return rc;
    }
    if (a->in_use == 0) {
        acct_unlock(store, index);
        return ERR_NOT_FOUND;
    }

    int64_t new_qty = a->pos_qty;
    int64_t new_cost = a->pos_cost;
    int64_t new_res = a->pos_reserved;
    int64_t new_realized = a->realized;

    switch (op) {
    case POS_BUY:
        new_qty += qty;
        new_cost += price * qty;
        break;
    case POS_RESERVE:
        /* 없는 주식을 팔 수 없다. 쓸 수 있는 수량을 넘으면 여기서 막힌다. */
        if (qty > a->pos_qty - a->pos_reserved) {
            acct_unlock(store, index);
            return ERR_INVALID_QTY;
        }
        new_res += qty;
        break;
    case POS_RELEASE:
        if (qty > a->pos_reserved) {
            acct_unlock(store, index);
            return ERR_INVALID_QTY;
        }
        new_res -= qty;
        break;
    case POS_SELL: {
        if (qty > a->pos_qty || qty > a->pos_reserved) {
            acct_unlock(store, index);
            return ERR_INVALID_QTY;
        }
        /*
         * 원가를 **판 몫만큼 비례해서** 덜어 낸다. 마지막 한 주를 팔면 남은 원가를
         * 통째로 덜어 내, 다 팔았을 때 원가가 정확히 0이 된다 — 나눗셈 나머지가
         * 쌓여 "0주인데 원가가 남은" 계좌가 생기지 않는다.
         */
        int64_t cost_out = (qty == a->pos_qty) ? a->pos_cost
                                               : (a->pos_cost * qty) / a->pos_qty;
        new_realized += price * qty - cost_out;
        new_cost -= cost_out;
        new_qty -= qty;
        new_res -= qty;
        break;
    }
    case POS_SEED:
        new_qty = qty;
        new_cost = price; /* 적재는 원가 '합'을 받는다 */
        new_res = 0;      /* 꺼져 있던 동안의 매도 주문은 되살리지 않는다 */
        new_realized = realized_seed;
        break;
    }

    if (!pos_ok(new_qty, new_cost, new_res)) {
        acct_unlock(store, index);
        return ERR_INVALID_QTY;
    }

    begin_write(a);
    a->pos_qty = new_qty;
    a->pos_cost = new_cost;
    a->pos_reserved = new_res;
    a->realized = new_realized;
    end_write(a);

    acct_unlock(store, index);
    return ERR_OK;
}

int acct_buy_fill(account_store_t *store, int32_t index, int64_t qty,
                  int64_t price)
{
    if (price <= 0) {
        return ERR_INVALID_PRICE;
    }
    return pos_apply(store, index, POS_BUY, qty, price, 0);
}

int acct_sell_reserve(account_store_t *store, int32_t index, int64_t qty)
{
    return pos_apply(store, index, POS_RESERVE, qty, 0, 0);
}

int acct_sell_release(account_store_t *store, int32_t index, int64_t qty)
{
    return pos_apply(store, index, POS_RELEASE, qty, 0, 0);
}

int acct_sell_fill(account_store_t *store, int32_t index, int64_t qty,
                   int64_t price)
{
    if (price <= 0) {
        return ERR_INVALID_PRICE;
    }
    return pos_apply(store, index, POS_SELL, qty, price, 0);
}

int acct_seed_position(account_store_t *store, int32_t index, int64_t qty,
                       int64_t cost, int64_t realized)
{
    return pos_apply(store, index, POS_SEED, qty, cost, realized);
}

int acct_position(account_store_t *store, int32_t index, int64_t *out_qty,
                  int64_t *out_cost, int64_t *out_reserved,
                  int64_t *out_realized)
{
    account_t *a = acct_at(store, index);
    if (a == NULL) {
        return ERR_NULL_PTR;
    }

    int rc = acct_lock(store, index, NULL);
    if (rc != ERR_OK) {
        return rc;
    }
    if (a->in_use == 0) {
        acct_unlock(store, index);
        return ERR_NOT_FOUND;
    }

    if (out_qty != NULL) {
        *out_qty = a->pos_qty;
    }
    if (out_cost != NULL) {
        *out_cost = a->pos_cost;
    }
    if (out_reserved != NULL) {
        *out_reserved = a->pos_reserved;
    }
    if (out_realized != NULL) {
        *out_realized = a->realized;
    }

    acct_unlock(store, index);
    return ERR_OK;
}
