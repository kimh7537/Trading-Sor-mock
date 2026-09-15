/*
 * 원장 데몬 — 리스너를 띄운다.
 *
 *   ledgerd [포트]
 *
 * 포트를 안 주거나 0을 주면 커널이 고른 포트를 쓰고 그 번호를 찍는다.
 *
 * **아직 원장이 아니다.** 지금은 전문을 받아 종별과 길이를 검증하고 정해진 응답을
 * 돌려주는 껍데기다. 계좌 잔고(T3-06)와 주문 검증(T3-07)이 들어오면 이 훅이
 * 그쪽으로 넘긴다. 껍데기를 먼저 세우는 이유는 **전문이 실제로 오가는 것**을
 * 확인하고 나서 그 위에 논리를 얹기 위해서다.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "errors.h"
#include "listener.h"

/* 요청 종별에 맞는 응답을 돌려준다. 아직 원장을 보지 않는다. */
static int handle(const wire_header_t *hdr, const uint8_t *body, uint8_t *out,
                  size_t out_cap, void *ctx)
{
    (void)ctx;

    msg_type_t reply = msg_reply_type(hdr->type);
    if (reply == MSG_UNKNOWN) {
        return 0; /* 체결 통보처럼 응답이 없는 종별 */
    }

    /*
     * 응답의 시퀀스와 논리 시각은 **요청이 들고 온 것을 그대로 쓴다.**
     * 원장이 자기 시각을 만들면 같은 입력에 다른 출력이 나온다 —
     * 결정성의 경계를 리스너 안쪽으로 넘기지 않는다.
     */
    wire_header_t h;
    memset(&h, 0, sizeof(h));
    h.type = (uint8_t)reply;
    h.body_len = (uint32_t)msg_body_len((uint8_t)reply);
    h.seq = hdr->seq;
    h.ts = hdr->ts;

    int n = wire_encode_header(&h, out, out_cap);
    if (n < 0) {
        return -1;
    }

    uint8_t *b = out + n;
    size_t   cap = out_cap - (size_t)n;
    int      m = -1;

    switch (hdr->type) {
    case MSG_ORDER_REQ: {
        msg_order_req_t req;
        if (msg_decode_order_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_order_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.cl_ord_id = req.cl_ord_id;
        ack.order_id = req.cl_ord_id; /* T3-06까지는 그대로 돌려준다 */
        ack.status = STATUS_NEW;
        ack.reason = ERR_OK;
        ack.price = req.price;
        m = msg_encode_order_ack(&ack, b, cap);
        break;
    }
    case MSG_CANCEL_REQ: {
        msg_cancel_req_t req;
        if (msg_decode_cancel_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_cancel_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.order_id = req.order_id;
        ack.cl_ord_id = req.cl_ord_id;
        ack.status = STATUS_CANCELED;
        ack.reason = ERR_OK;
        m = msg_encode_cancel_ack(&ack, b, cap);
        break;
    }
    case MSG_MODIFY_REQ: {
        msg_modify_req_t req;
        if (msg_decode_modify_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_modify_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.order_id = req.order_id;
        ack.cl_ord_id = req.cl_ord_id;
        ack.status = STATUS_NEW;
        ack.reason = ERR_OK;
        ack.price = req.new_price;
        m = msg_encode_modify_ack(&ack, b, cap);
        break;
    }
    case MSG_QUERY_REQ: {
        msg_query_req_t req;
        if (msg_decode_query_req(body, hdr->body_len, &req) < 0) {
            return -1;
        }
        msg_query_ack_t ack;
        memset(&ack, 0, sizeof(ack));
        ack.order_id = req.order_id;
        ack.status = STATUS_NEW;
        m = msg_encode_query_ack(&ack, b, cap);
        break;
    }
    default:
        return 0;
    }

    if (m < 0) {
        return -1;
    }
    return n + m;
}

int main(int argc, char **argv)
{
    uint16_t port = 0;

    if (argc >= 2) {
        long v = strtol(argv[1], NULL, 10);
        if (v < 0 || v > 65535) {
            fprintf(stderr, "포트는 0~65535이어야 한다\n");
            return 2;
        }
        port = (uint16_t)v;
    }

    if (listener_install_signals() != ERR_OK) {
        fprintf(stderr, "시그널 핸들러를 걸 수 없다\n");
        return 1;
    }

    listener_t *ln = listener_open(port, 64);
    if (ln == NULL) {
        fprintf(stderr, "포트 %u 를 열 수 없다\n", (unsigned)port);
        return 1;
    }

    /* 포트 0을 줬으면 커널이 고른 번호를 알려 준다. */
    printf("ledgerd 포트 %u 에서 대기 (SIGTERM/SIGINT로 종료)\n",
           (unsigned)listener_port(ln));
    fflush(stdout);

    int conns = listener_run(ln, handle, NULL);

    printf("접속 %d건 처리 후 종료\n", conns);
    listener_close(ln);

    return 0;
}
