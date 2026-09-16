#ifndef MINI_SOR_FEED_SOURCE_H
#define MINI_SOR_FEED_SOURCE_H

#include "feed.h"
#include "match.h"
#include "order_book.h"

/*
 * 호가창에서 시세 피드 메시지를 만든다.
 *
 * ===========================================================================
 * 왜 `core`가 아니라 여기 있는가
 * ===========================================================================
 *
 * 형식(T5-06의 `core/feed.h`)은 `core`에 있다. 바깥에서 붙는 전략 엔진이
 * 호가창 코드를 링크하지 않고도 시세를 읽을 수 있어야 하기 때문이다.
 *
 * 그런데 **만드는 쪽은 호가창을 알아야 한다.** `book_snapshot()`은
 * `exchange`의 것이고 `core`는 `exchange`를 링크하지 않는다. 그래서 형식과
 * 생성기를 갈라 둔다 — 읽는 쪽은 `core`만, 만드는 쪽은 `exchange`까지.
 *
 * 전략 엔진이 호가창 내부 자료구조를 알 필요가 없다는 완료 조건이
 * 이 가름으로 지켜진다.
 */

/*
 * 호가창 양쪽의 스냅샷을 피드 메시지로 옮긴다.
 *
 * `depth`는 1..FEED_DEPTH_MAX. 호가창에 그만큼 없으면 **없는 단은 0으로
 * 채운다** — 자리를 고정해야 읽는 쪽이 줄 수를 세지 않고 읽는다.
 *
 * 논리 시각을 받지 않는다. 시각과 시퀀스는 **보내는 쪽의 상태**이지 시세의
 * 내용이 아니라서 머리에 들어간다(T3-02와 같은 가름).
 *
 * 성공하면 ERR_OK. 인자가 잘못되면 ERR_INVALID_ARG.
 */
int feed_book_from_engine(const match_engine_t *eng, const char *symbol,
                          market_t market, int32_t depth, feed_book_t *out);

/*
 * 체결 한 건을 피드 메시지로 옮긴다.
 *
 * `taker_side`는 **들어온 주문의 방향**이다. 누가 먼저 움직였는지가 정보라서
 * maker 쪽이 아니라 taker 쪽을 싣는다.
 *
 * 성공하면 ERR_OK.
 */
int feed_trade_from_fill(const fill_t *f, const char *symbol, market_t market,
                         side_t taker_side, feed_trade_t *out);

#endif /* MINI_SOR_FEED_SOURCE_H */
