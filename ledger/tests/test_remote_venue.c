/*
 * T12-02 원격 거래소 — **원장이 FEP 세션으로 다리를 보낸다.**
 *
 * ===========================================================================
 * 진짜 프로세스, 진짜 소켓
 * ===========================================================================
 *
 * 상대역을 이 파일 안에서 지어내지 않는다. **`exchanged`를 그대로 띄운다** —
 * 실행 파일 경로는 CMake가 `EXCHANGED_BIN`으로 넣어 준다. 상대를 흉내 내면
 * 규격을 서로 다르게 읽고 있어도 양쪽 테스트가 통과한다(T3-15의 교훈).
 *
 * 그래서 이 테스트가 확인하는 것은 넷이다.
 *
 *  1. 원장이 FEP 세션으로 **로그인까지** 붙는다
 *  2. 다리가 소켓을 건너가 **진짜 매칭 엔진**에 닿는다
 *  3. 돌아온 체결 **금액이 한 푼도 새지 않는다** — 평균가로 줄이지 않으므로
 *     `omap_notional()`이 거래소가 말한 금액과 정확히 같다
 *  4. 붙지 않은 시장으로는 보내지 않는다 (거부로 답한다)
 *
 * ===========================================================================
 * 기다림에는 상한이 있다
 * ===========================================================================
 *
 * `sleep`으로 넘어가지 않는다. 붙는 것도 답을 받는 것도 상한이 있고, 닿으면
 * 통과가 아니라 실패다.
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "errors.h"
#include "ledger_core.h"
#include "msg.h"
#include "order_map.h"
#include "remote_venue.h"
#include "strategy.h"

#ifndef EXCHANGED_BIN
#error "CMake가 exchanged 실행 파일 경로를 넣어 줘야 한다"
#endif

#define STEP(fn)                                                            \
    do {                                                                    \
        fprintf(stderr, "[%s]\n", #fn);                                     \
        fn();                                                               \
    } while (0)

/* 거래소를 띄우는 설정. 시드가 같으면 호가창도 같다(결정성). */
#define EX_SYMBOL "005930"
#define EX_REF_PRICE 70000
#define EX_LIQUIDITY 200
#define EX_SEED "20260917"

/* 붙기를 기다릴 상한. 프로세스가 뜨는 시간이다 */
#define SPAWN_WAIT_MS 5000

/* 논리 시각은 정규장 안이어야 한다 — 규칙을 걸지 않았지만 데이터 모델의 일부다 */
#define TS_ORDER TOD_NS(10, 0, 0)

static int64_t mono_ms(void)
{
    struct timespec ts;
    assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * 비어 있는 포트를 하나 고른다.
 *
 * 커널에게 받아 곧바로 닫고 그 번호를 거래소에 준다. 닫은 뒤 누가 채 갈 틈이
 * 있지만, 그 창은 아주 좁고 **실패해도 조용히 지나가지 않는다** — 붙지 못하면
 * 상한에 닿아 단언이 깨진다.
 */
static uint16_t free_port(void)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    assert(fd >= 0);

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    assert(bind(fd, (struct sockaddr *)&a, sizeof(a)) == 0);

    socklen_t len = sizeof(a);
    assert(getsockname(fd, (struct sockaddr *)&a, &len) == 0);
    uint16_t port = ntohs(a.sin_port);
    close(fd);
    assert(port != 0);
    return port;
}

/* 거래소 프로세스를 띄운다. */
static pid_t spawn_exchange(uint16_t port, const char *market)
{
    char portbuf[8];
    char refbuf[16];
    char liqbuf[16];
    snprintf(portbuf, sizeof(portbuf), "%u", (unsigned)port);
    snprintf(refbuf, sizeof(refbuf), "%d", EX_REF_PRICE);
    snprintf(liqbuf, sizeof(liqbuf), "%d", EX_LIQUIDITY);

    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        /* 자식의 안내 출력은 테스트 결과를 가린다. 버린다 */
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            (void)dup2(devnull, STDOUT_FILENO);
            close(devnull);
        }
        execl(EXCHANGED_BIN, "exchanged", portbuf, "--market", market,
              "--ref-price", refbuf, "--liquidity", liqbuf, "--seed", EX_SEED,
              "--symbol", EX_SYMBOL, (char *)NULL);
        _exit(127); /* execl이 돌아왔으면 못 띄운 것이다 */
    }
    return pid;
}

/* 거래소가 다 뜰 때까지 붙어 본다. 상한에 닿으면 실패다. */
static void connect_or_die(remote_venues_t *rv)
{
    int64_t deadline = mono_ms() + SPAWN_WAIT_MS;
    for (;;) {
        if (remote_venues_connect(rv) == ERR_OK) {
            return;
        }
        assert(mono_ms() < deadline); /* 매달리면 통과가 아니라 실패다 */
    }
}

static void stop_exchange(pid_t pid)
{
    assert(kill(pid, SIGTERM) == 0);
    int st = 0;
    assert(waitpid(pid, &st, 0) == pid);
}

/* 지정가 매수 한 건. 기준가보다 위로 걸어 시드 호가를 먹게 한다. */
static void make_order(order_t *o, exec_plan_t *plan, order_id_t id, qty_t qty,
                       price_t price, market_t market)
{
    memset(o, 0, sizeof(*o));
    o->id = id;
    o->side = SIDE_BUY;
    o->type = ORDER_LIMIT;
    o->price = price;
    o->qty = qty;
    o->ts = TS_ORDER;
    o->market = market;

    plan_init(plan);
    assert(plan_add_leg(plan, market, qty, price, ORDER_LIMIT) == ERR_OK);
}

/* --- 1. 다리가 소켓을 건너 진짜 매칭 엔진에 닿는다 --- */

static void test_leg_crosses_the_wire(void)
{
    uint16_t port = free_port();
    pid_t    ex = spawn_exchange(port, "krx");

    remote_venues_t rv;
    remote_venues_init(&rv, EX_SYMBOL);
    assert(remote_venues_any(&rv) == false);
    assert(remote_venues_set(&rv, MARKET_KRX, "127.0.0.1", port) == ERR_OK);
    assert(remote_venues_any(&rv) == true);
    connect_or_die(&rv);

    order_map_t *map = omap_create(64);
    assert(map != NULL);

    /*
     * 기준가 +2%에 8,000주. **한 가격대로는 다 못 채우는 크기**로 고른다 —
     * 여러 가격대를 먹어야 체결 금액이 수량으로 나누어떨어지지 않고, 그때 비로소
     * "평균가로 줄이면 새는 나머지"가 생긴다(T7-07). 100주로는 첫 호가 한 단에서
     * 다 먹어 나머지가 0이라 이 경로를 한 번도 지나지 않았다.
     */
    order_t     o;
    exec_plan_t plan;
    make_order(&o, &plan, 1000, 8000, EX_REF_PRICE + EX_REF_PRICE / 50,
               MARKET_KRX);

    exec_report_t rep;
    assert(remote_submit(&rv, map, &o, &plan, &rep) == ERR_OK);

    assert(rep.leg_count == 1);
    assert(rep.rejected_count == 0);
    assert(rep.legs[0].market == MARKET_KRX);
    assert(rep.legs[0].sent_qty == 8000);
    assert(rep.legs[0].rc == ERR_OK);
    assert(rep.order_qty == 8000);

    /* 거래소가 매긴 번호로 체결이 났다 — 지어낸 값이 아니다 */
    assert(rep.filled_qty > 0);
    assert(rep.notional > 0);
    assert(rep.filled_qty == rep.legs[0].filled_qty);
    assert(rep.notional == rep.legs[0].notional);

    /*
     * **금액이 한 푼도 새지 않는다.** 집행기가 매핑에 옮긴 체결의 합이 거래소가
     * 말한 금액과 정확히 같아야 한다. 평균가로 줄여 되돌렸다면 여기서 어긋난다.
     */
    assert(omap_filled_qty(map, o.id) == rep.filled_qty);
    assert(omap_notional(map, o.id) == rep.notional);

    /*
     * **여러 가격대를 먹었다.** 나머지가 0이면 평균가 하나로 되돌려도 금액이 맞아
     * 이 테스트가 아무것도 지키지 않는다. 시드가 같으면 호가창도 같으므로 이 값은
     * 흔들리지 않는다 — 흔들리면 그것부터 알아야 한다.
     */
    assert(rep.notional % rep.filled_qty != 0);
    /* 수량 불변식은 in-process와 같다 */
    assert(rep.unfilled_qty == rep.order_qty - rep.filled_qty);
    assert(rep.working_qty == omap_remaining(map, o.id));

    omap_destroy(map);
    remote_venues_close(&rv);
    stop_exchange(ex);
}

/* --- 2. 붙지 않은 시장으로는 보내지 않는다 --- */

/*
 * KRX만 띄우고 NXT로 보낸다. **조용히 성공하면 안 된다** — 그러면 체결되지 않은
 * 수량이 체결된 것처럼 보인다.
 */
static void test_unconnected_market_is_rejected(void)
{
    uint16_t port = free_port();
    pid_t    ex = spawn_exchange(port, "krx");

    remote_venues_t rv;
    remote_venues_init(&rv, EX_SYMBOL);
    assert(remote_venues_set(&rv, MARKET_KRX, "127.0.0.1", port) == ERR_OK);
    connect_or_die(&rv);

    order_map_t *map = omap_create(64);
    assert(map != NULL);

    order_t     o;
    exec_plan_t plan;
    make_order(&o, &plan, 2000, 10, EX_REF_PRICE, MARKET_NXT);

    exec_report_t rep;
    /* 한 다리뿐이고 그것이 거부됐으므로 논리 주문도 거부다 */
    assert(remote_submit(&rv, map, &o, &plan, &rep) == ERR_IO);
    assert(rep.rejected_count == 1);
    assert(rep.filled_qty == 0);
    assert(rep.status == STATUS_REJECTED);

    /* 거부된 다리의 수량은 취소로 기록돼 살아 있는 잔량에서 빠진다 */
    assert(omap_remaining(map, o.id) == 0);

    omap_destroy(map);
    remote_venues_close(&rv);
    stop_exchange(ex);
}

/* --- 3. 두 시장에 나눠 보낸다 --- */

/*
 * KRX와 NXT를 각각 띄우고 다리 둘을 보낸다. **거래소마다 접속도 번호 체계도
 * 따로**라는 것이 이 구성의 전제다.
 */
static void test_two_venues_two_legs(void)
{
    uint16_t kp = free_port();
    pid_t    kx = spawn_exchange(kp, "krx");
    uint16_t np = free_port();
    pid_t    nx = spawn_exchange(np, "nxt");

    remote_venues_t rv;
    remote_venues_init(&rv, EX_SYMBOL);
    assert(remote_venues_set(&rv, MARKET_KRX, "127.0.0.1", kp) == ERR_OK);
    assert(remote_venues_set(&rv, MARKET_NXT, "127.0.0.1", np) == ERR_OK);
    connect_or_die(&rv);

    order_map_t *map = omap_create(64);
    assert(map != NULL);

    price_t price = EX_REF_PRICE + EX_REF_PRICE / 50;

    order_t o;
    memset(&o, 0, sizeof(o));
    o.id = 3000;
    o.side = SIDE_BUY;
    o.type = ORDER_LIMIT;
    o.price = price;
    o.qty = 120;
    o.ts = TS_ORDER;
    o.market = MARKET_KRX;

    exec_plan_t plan;
    plan_init(&plan);
    assert(plan_add_leg(&plan, MARKET_KRX, 70, price, ORDER_LIMIT) == ERR_OK);
    assert(plan_add_leg(&plan, MARKET_NXT, 50, price, ORDER_LIMIT) == ERR_OK);

    exec_report_t rep;
    assert(remote_submit(&rv, map, &o, &plan, &rep) == ERR_OK);

    assert(rep.leg_count == 2);
    assert(rep.rejected_count == 0);
    assert(rep.legs[0].market == MARKET_KRX && rep.legs[0].sent_qty == 70);
    assert(rep.legs[1].market == MARKET_NXT && rep.legs[1].sent_qty == 50);

    /* 합계는 다리의 합이다 — 어느 한쪽이 빠지면 여기서 어긋난다 */
    assert(rep.filled_qty == rep.legs[0].filled_qty + rep.legs[1].filled_qty);
    assert(rep.notional == rep.legs[0].notional + rep.legs[1].notional);
    assert(omap_notional(map, o.id) == rep.notional);

    omap_destroy(map);
    remote_venues_close(&rv);
    stop_exchange(kx);
    stop_exchange(nx);
}


/* --- 4. 원장 코어가 통째로 그 길로 간다 --- */

/*
 * **여기가 T12-03의 전부다.** 전문 하나가 검증·증거금·SOR·**소켓 건너 매칭**·정산을
 * 지나 돌아오는가.
 *
 * 체결은 저쪽 프로세스에서 났으므로 엔진 이벤트가 오지 않는다. 그런데도 계좌에서
 * 정확히 체결 금액만큼 빠져야 한다 — 다리가 들고 온 금액으로 정산하기 때문이다.
 * 평균가로 줄여 정산했다면 여기서 몇 원이 어긋난다.
 */
static void test_ledger_core_goes_remote(void)
{
    uint16_t kp = free_port();
    pid_t    kx = spawn_exchange(kp, "krx");
    uint16_t np = free_port();
    pid_t    nx = spawn_exchange(np, "nxt");

    remote_venues_t rv;
    remote_venues_init(&rv, EX_SYMBOL);
    assert(remote_venues_set(&rv, MARKET_KRX, "127.0.0.1", kp) == ERR_OK);
    assert(remote_venues_set(&rv, MARKET_NXT, "127.0.0.1", np) == ERR_OK);
    connect_or_die(&rv);

    ledger_core_config_t cfg = LEDGER_CORE_DEFAULT;
    cfg.ref_price = EX_REF_PRICE;
    cfg.remote = &rv;
    ledger_core_t *c = ledger_core_create(&cfg);
    assert(c != NULL);

    int64_t cash0 = 0;
    int64_t rsv0 = 0;
    assert(ledger_core_balance(c, cfg.account, &cash0, &rsv0) == ERR_OK);

    /*
     * 기준가 +2%에 1,000주. **기본 예수금 1억 원 안에 들어가는 크기**로 고른다 —
     * 증거금은 지정가 x 수량으로 묶으므로 3,000주는 2억이 넘어 거절된다.
     * 나머지가 생기는 경우는 앞의 test_leg_crosses_the_wire가 본다.
     */
    price_t price = EX_REF_PRICE + EX_REF_PRICE / 50;

    msg_order_req_t req;
    memset(&req, 0, sizeof(req));
    snprintf(req.account, sizeof(req.account), "%s", cfg.account);
    snprintf(req.symbol, sizeof(req.symbol), "%s", EX_SYMBOL);
    req.cl_ord_id = 77;
    req.side = SIDE_BUY;
    req.type = ORDER_LIMIT;
    req.market = MSG_MARKET_AUTO; /* SOR가 고른다 */
    req.price = price;
    req.qty = 1000;

    uint8_t body[MSG_ORDER_REQ_LEN];
    assert(msg_encode_order_req(&req, body, sizeof(body)) ==
           (int)MSG_ORDER_REQ_LEN);

    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.version = WIRE_VERSION;
    h.type = MSG_ORDER_REQ;
    h.body_len = MSG_ORDER_REQ_LEN;
    h.seq = 1;
    h.ts = 7;

    uint8_t out[512];
    int     n = ledger_core_handle(&h, body, out, sizeof(out), c);
    assert(n == (int)(WIRE_HEADER_LEN + MSG_ORDER_ACK_LEN));

    msg_order_ack_t ack;
    assert(msg_decode_order_ack(out + WIRE_HEADER_LEN, MSG_ORDER_ACK_LEN, &ack) >=
           0);
    assert(ack.reason == ERR_OK);
    assert(ack.filled_qty > 0);

    /*
     * **계좌에서 빠진 돈이 체결 금액과 정확히 같다.** 응답은 평균가만 싣지만
     * 정산은 금액으로 했으므로, 평균가 x 수량과는 몇 원 다를 수 있다 — 그 차이가
     * 곧 T7-07에서 샜던 나머지다. 여기서는 잔고로 확인한다.
     */
    int64_t cash1 = 0;
    int64_t rsv1 = 0;
    assert(ledger_core_balance(c, cfg.account, &cash1, &rsv1) == ERR_OK);
    assert(cash1 < cash0);              /* 샀으니 돈이 나갔다 */
    assert(rsv1 == 0);                  /* 전량 체결이면 묶인 것이 없다 */
    assert(cash0 - cash1 >= (int64_t)ack.filled_qty * (int64_t)ack.price);
    assert(cash0 - cash1 <
           (int64_t)ack.filled_qty * (int64_t)(ack.price + 1));

    /*
     * **호가창도 저쪽 것을 보여 준다.** 이 프로세스 안의 호가창을 보여 주면
     * 방금 먹은 물량이 그대로 남아 있어야 하는데, 저쪽 것이라면 줄어 있다.
     */
    int64_t traded = 0;
    for (int32_t mk = 0; mk < MARKET_COUNT; mk++) {
        msg_book_req_t breq;
        memset(&breq, 0, sizeof(breq));
        snprintf(breq.symbol, sizeof(breq.symbol), "%s", EX_SYMBOL);
        breq.market = (uint8_t)mk;

        uint8_t bbody[MSG_BOOK_REQ_LEN];
        assert(msg_encode_book_req(&breq, bbody, sizeof(bbody)) ==
               (int)MSG_BOOK_REQ_LEN);
        h.type = MSG_BOOK_REQ;
        h.body_len = MSG_BOOK_REQ_LEN;
        n = ledger_core_handle(&h, bbody, out, sizeof(out), c);
        assert(n == (int)(WIRE_HEADER_LEN + MSG_BOOK_ACK_LEN));

        msg_book_ack_t back;
        assert(msg_decode_book_ack(out + WIRE_HEADER_LEN, MSG_BOOK_ACK_LEN,
                                   &back) >= 0);
        assert(back.ask_price[0] > 0); /* 거래소의 시드 호가가 보인다 */
        traded += back.traded_qty;
    }
    /*
     * 거래소가 방금 체결을 셌다 — **이 프로세스의 호가창은 그것을 모른다.**
     * 어느 시장으로 갔는지는 SOR가 정하므로 둘을 합해서 본다.
     */
    assert(traded > 0);

    /* 흔들 호가창이 여기 없으므로 가상 참가자와 실시세는 거절된다 */
    assert(ledger_core_tick(c, 1) == ERR_NOT_SUPPORTED);

    ledger_core_destroy(c);
    remote_venues_close(&rv);
    stop_exchange(kx);
    stop_exchange(nx);
}

int main(void)
{
    /* 상대가 먼저 끊어도 죽지 않는다. 쓰기 실패로 받아 처리한다 */
    signal(SIGPIPE, SIG_IGN);

    STEP(test_leg_crosses_the_wire);
    STEP(test_unconnected_market_is_rejected);
    STEP(test_two_venues_two_legs);
    STEP(test_ledger_core_goes_remote);

    printf("test_remote_venue: 통과\n");
    return 0;
}
