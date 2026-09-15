#include "market_rules.h"

/*
 * 세션 이름표. 규칙 구현(krx.c, nxt.c)은 각자 파일에 있고, 여기에는 시장과 무관한
 * 공통 조각만 둔다.
 */
#define SESSION_LIST(X)                \
    X(SESSION_CLOSED, "장 마감")       \
    X(SESSION_PRE, "프리마켓")         \
    X(SESSION_PRE_BREAK, "오전 휴장")  \
    X(SESSION_REGULAR, "정규장")       \
    X(SESSION_POST_BREAK, "오후 휴장") \
    X(SESSION_AFTER, "애프터마켓")

const char *session_str(session_t session)
{
#define SESSION_NAME_CASE(name, text) \
    case name:                        \
        return text;

    switch (session) {
        SESSION_LIST(SESSION_NAME_CASE)
    }

#undef SESSION_NAME_CASE

    return "알 수 없는 세션";
}
