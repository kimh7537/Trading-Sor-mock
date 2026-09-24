#ifndef MINI_SOR_ACCOUNT_H
#define MINI_SOR_ACCOUNT_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "msg.h"
#include "shm_segment.h"

/*
 * 계좌 잔고 — 공유 메모리에 두고 계좌 단위로 잠근다.
 *
 * ===========================================================================
 * 왜 계좌 단위인가
 * ===========================================================================
 *
 * 원장 전체에 락 하나를 걸면 워커를 여럿 띄운 의미가 사라진다 — 서로 다른 계좌의
 * 주문끼리도 줄을 선다. 계좌마다 락을 두면 **다른 계좌를 만지는 워커는 서로를
 * 기다리지 않는다.** 같은 계좌에 동시에 주문이 몰리는 일은 드물기 때문에 실질적인
 * 경합이 거의 없다.
 *
 * 대가는 락이 계좌 수만큼 생기는 것인데, `pthread_mutex_t`가 40바이트라
 * 계좌 1만 개면 400KB다. 캐시라인 정렬(T3-05)까지 하면 더 늘지만 이 정도는
 * 문제가 되지 않는다.
 *
 * ===========================================================================
 * 워커가 락을 쥔 채 죽으면 (완료 조건이 요구한 문서화)
 * ===========================================================================
 *
 * 사전 fork 풀에서 워커는 죽을 수 있다. T3-04가 자리를 채우지만, **죽은 워커가
 * 쥐고 있던 락은 자리를 채운다고 풀리지 않는다.** 그대로 두면 그 계좌는 영영
 * 막히고, 그 계좌의 주문이 전부 멈춘다.
 *
 * **`PTHREAD_MUTEX_ROBUST`를 쓴다.**
 *
 * 락을 쥔 프로세스가 죽으면 다음에 잠그는 쪽이 `EOWNERDEAD`를 받는다. 그때
 * 락은 이미 이쪽 것이지만 **보호하던 데이터가 온전하다는 보장이 없다** —
 * 죽은 워커가 갱신을 절반만 했을 수 있다.
 *
 * 그래서 두 가지를 같이 둔다.
 *
 *  1. **쓰기 의사 표시(`mutating`)와 직전 값(pre-image).** 값을 바꾸기 전에
 *     표시를 세우고 옛 값을 적어 둔 뒤 바꾸고, 다 되면 표시를 내린다
 *  2. `EOWNERDEAD`를 받으면 표시를 본다. 서 있으면 **직전 값으로 되돌리고**,
 *     내려 있으면 그대로 둔다. 그다음 `pthread_mutex_consistent()`로 락을
 *     쓸 수 있는 상태로 되돌린다
 *
 * `pthread_mutex_consistent()`를 부르지 않고 락을 놓으면 그 뮤텍스는 영구히
 * `ENOTRECOVERABLE`이 된다 — 회수하려다 오히려 못 쓰게 만든다.
 *
 * **왜 되돌리는가.** 죽은 워커가 하던 일은 어차피 응답을 보내지 못했다. 요청한
 * 쪽은 실패로 볼 것이므로 원장도 그 일이 없던 것으로 두는 편이 앞뒤가 맞는다.
 * 절반만 반영된 상태를 굳히는 선택지도 있지만, 그러면 **증거금은 묶였는데 주문은
 * 없는 계좌**가 생긴다.
 *
 * ===========================================================================
 * 잔고 불변조건
 * ===========================================================================
 *
 *   0 <= reserved <= cash
 *
 * `cash`는 예수금, `reserved`는 미체결 주문에 묶인 증거금이다.
 * 주문에 쓸 수 있는 돈은 `cash - reserved`다.
 *
 * 이 조건을 어기는 연산은 **거절하고 아무것도 바꾸지 않는다.** 절반만 반영하면
 * 그 계좌의 잔고를 영영 믿을 수 없다.
 */

/* 계좌번호 길이는 전문 규격(T3-02)과 같다. */
#define ACCT_NO_LEN MSG_ACCOUNT_LEN

/*
 * 계좌 레코드. 공유 메모리에 놓이므로 **포인터를 담지 않는다**(T3-05의 규칙).
 */
typedef struct {
    pthread_mutex_t lock;

    char     account_no[ACCT_NO_LEN + 1];
    uint32_t in_use; /* 0이면 빈 자리 */

    int64_t cash;     /* 예수금(원) */
    int64_t reserved; /* 미체결 주문에 묶인 금액(원) */

    uint64_t version; /* 바뀔 때마다 증가. 디버깅과 회귀 확인용 */

    /*
     * 쓰기 중 표시와 직전 값.
     *
     * 값을 바꾸기 전에 `mutating`을 세우고 여기에 옛 값을 적는다. 다 바꾸면
     * 표시를 내린다. 쥔 채로 죽으면 다음 사람이 이것을 보고 되돌린다.
     */
    /*
     * --- 보유 종목 (T11-01) ---
     *
     * 이 원장은 한 종목짜리라 종목명을 따로 담지 않는다. 종목을 바꾸면 원장이
     * 새로 열리고 보유도 함께 초기화된다 — 채널계가 로그인할 때 다시 실어 준다.
     *
     * **평균 단가를 저장하지 않고 원가 합을 저장한다.** 평균을 저장하면 살 때마다
     * 나눗셈이 들어가 반올림 오차가 쌓이고, 다 팔았을 때 0으로 떨어지지 않는다.
     * 평균이 필요하면 `pos_cost / pos_qty`로 그때 구한다.
     */
    int64_t pos_qty;      /* 보유 수량 */
    int64_t pos_cost;     /* 매입 원가 합 */
    int64_t pos_reserved; /* 미체결 매도 주문에 묶인 수량 */
    int64_t realized;     /* 실현 손익 누계. 판 것에서만 생긴다 */

    uint32_t mutating;
    int64_t  pre_cash;
    int64_t  pre_reserved;
    int64_t  pre_pos_qty;
    int64_t  pre_pos_cost;
    int64_t  pre_pos_reserved;
    int64_t  pre_realized;
} account_t;

/* 계좌 저장소. 세그먼트를 소유하지 않는다 — 열고 닫는 것은 호출부의 일이다. */
typedef struct {
    shm_segment_t *seg;
    int32_t        capacity;
} account_store_t;

/*
 * 저장소를 초기화한다. **fork 전에 부른다** — 뮤텍스를 여기서 만든다.
 *
 * 세그먼트의 계좌 영역을 쓴다. 영역의 레코드 크기가 `sizeof(account_t)`보다
 * 작으면 ERR_INVALID_ARG.
 */
int acct_store_init(account_store_t *store, shm_segment_t *seg);

/*
 * 저장소의 뮤텍스를 전부 파괴한다. **모든 워커가 끝난 뒤 부모가 한 번** 부른다.
 */
void acct_store_destroy(account_store_t *store);

/* 계좌 수 상한. */
int32_t acct_capacity(const account_store_t *store);

/*
 * 계좌를 연다. 이미 있으면 그 자리를, 없으면 빈 자리를 잡아 만든다.
 * 자리 번호를 반환하고, 자리가 없으면 ERR_POOL_EXHAUSTED.
 *
 * **fork 전에 계좌를 다 열어 두는 것을 권한다.** 자리를 잡는 것은 저장소 전체를
 * 훑는 일이라 계좌 단위 락으로 보호되지 않는다.
 */
int acct_open(account_store_t *store, const char *account_no);

/* 계좌를 찾는다. 없으면 ERR_NOT_FOUND. */
int acct_find(const account_store_t *store, const char *account_no);

/* 자리 번호로 레코드를 본다. 범위 밖이면 NULL. */
account_t *acct_at(account_store_t *store, int32_t index);

/*
 * --- 잠금 ---
 *
 * `acct_lock()`은 죽은 주인을 만나면 **직전 값으로 되돌린 뒤** 락을 쓸 수 있게
 * 만들고 ERR_OK를 반환한다. 되돌림이 있었는지 알고 싶으면 `out_recovered`를 준다
 * (NULL이어도 된다).
 *
 * 뮤텍스가 회복 불가 상태면 ERR_NOT_SUPPORTED.
 */
int acct_lock(account_store_t *store, int32_t index, bool *out_recovered);
int acct_unlock(account_store_t *store, int32_t index);

/*
 * --- 잔고 연산 ---
 *
 * 전부 **스스로 잠그고 푼다.** 호출부가 잠금을 잊는 실수를 없앤다.
 * 불변조건을 어기면 ERR_INVALID_QTY로 거절하고 아무것도 바꾸지 않는다.
 */

/* 입금. amount > 0. */
int acct_deposit(account_store_t *store, int32_t index, int64_t amount);

/* 출금. 쓸 수 있는 돈(cash - reserved)을 넘으면 거절한다. */
int acct_withdraw(account_store_t *store, int32_t index, int64_t amount);

/* 주문 증거금을 묶는다. 쓸 수 있는 돈을 넘으면 거절한다. */
int acct_reserve(account_store_t *store, int32_t index, int64_t amount);

/* 묶은 증거금을 푼다(주문 취소·거부). 묶인 것보다 많이 풀 수 없다. */
int acct_release(account_store_t *store, int32_t index, int64_t amount);

/*
 * 체결 정산. 묶여 있던 amount를 풀면서 예수금에서도 뺀다.
 * 묶인 것보다 많으면 거절한다.
 */
int acct_settle(account_store_t *store, int32_t index, int64_t amount);

/* 주문에 쓸 수 있는 돈. 없는 계좌면 0. */
int64_t acct_available(account_store_t *store, int32_t index);

/* 예수금·묶인 금액을 한 번에 읽는다. 잠그고 읽는다. */
int acct_snapshot(account_store_t *store, int32_t index, int64_t *out_cash,
                  int64_t *out_reserved);

/*
 * --- 보유 종목 연산 (T11-01) ---
 *
 * 잔고 연산과 같은 약속을 따른다 — 스스로 잠그고, 불변조건을 어기면 아무것도
 * 바꾸지 않고 거절한다.
 *
 * 불변조건:  0 <= pos_reserved <= pos_qty,  pos_cost >= 0
 */

/* 매수 체결. 보유가 늘고 원가가 쌓인다. */
int acct_buy_fill(account_store_t *store, int32_t index, int64_t qty,
                  int64_t price);

/*
 * 매도 주문 접수 — 팔 수량을 묶는다.
 *
 * **없는 주식을 팔 수 없다.** 쓸 수 있는 수량(pos_qty - pos_reserved)을 넘으면
 * ERR_INVALID_QTY다. 이 검사가 없으면 공매도가 되고, 그것은 이 시뮬레이터가
 * 다루는 시장의 규칙이 아니다.
 */
int acct_sell_reserve(account_store_t *store, int32_t index, int64_t qty);

/* 묶은 수량을 푼다(주문 취소·거부·미체결 잔량). */
int acct_sell_release(account_store_t *store, int32_t index, int64_t qty);

/*
 * 매도 체결. 묶인 수량에서 덜어 내고 실현 손익을 쌓는다.
 *
 * 실현 손익 = (체결가 - 평균 단가) x 수량. 원가는 **판 몫만큼 비례해서** 덜어 낸다 —
 * 그래야 다 팔았을 때 원가가 정확히 0이 된다.
 */
int acct_sell_fill(account_store_t *store, int32_t index, int64_t qty,
                   int64_t price);

/*
 * 보유를 통째로 실어 준다(로그인 적재).
 *
 * 원장은 메모리에만 있어서 다시 뜨면 보유가 사라진다. 기록을 들고 있는 채널계가
 * 로그인할 때 이것으로 되살린다. **묶인 수량은 싣지 않는다** — 서버가 꺼져 있던
 * 동안 그 매도 주문은 어느 시장에도 없었으므로 되살리지 않는다.
 */
int acct_seed_position(account_store_t *store, int32_t index, int64_t qty,
                       int64_t cost, int64_t realized);

/* 보유 수량·원가·묶인 수량·실현 손익을 한 번에 읽는다. */
int acct_position(account_store_t *store, int32_t index, int64_t *out_qty,
                  int64_t *out_cost, int64_t *out_reserved,
                  int64_t *out_realized);

#endif /* MINI_SOR_ACCOUNT_H */
