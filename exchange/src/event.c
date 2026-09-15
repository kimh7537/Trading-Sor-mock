#include "event.h"

#include <stddef.h>

/*
 * 이벤트 종류와 이름을 한 목록에서 함께 만든다. errors.h의 X 매크로와 같은 이유다 —
 * 종류를 추가하고 이름을 빠뜨리는 일을 구조적으로 막는다.
 */
#define EVENT_TYPE_LIST(X)                            \
    X(EVENT_ACCEPTED, "ACCEPTED")                     \
    X(EVENT_EXECUTED, "EXECUTED")                     \
    X(EVENT_PARTIALLY_EXECUTED, "PARTIALLY_EXECUTED") \
    X(EVENT_CANCELED, "CANCELED")                     \
    X(EVENT_REJECTED, "REJECTED")                     \
    X(EVENT_MODIFIED, "MODIFIED")

void event_emit(const event_sink_t *sink, const order_event_t *ev)
{
    if (sink == NULL || sink->fn == NULL || ev == NULL) {
        return;
    }
    sink->fn(ev, sink->ctx);
}

const char *event_type_str(event_type_t type)
{
#define EVENT_NAME_CASE(name, text) \
    case name:                      \
        return text;

    switch (type) {
        EVENT_TYPE_LIST(EVENT_NAME_CASE)
    }

#undef EVENT_NAME_CASE

    /* 로그 경로에서 널 검사를 강요하지 않는다. errors.h의 err_str()과 같은 방침. */
    return "알 수 없는 이벤트";
}
