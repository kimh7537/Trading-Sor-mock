# 전략 엔진용 주문 SDK (T5-07).
#
# **core만 건다.** exchange/sor/ledger를 걸면 바깥 전략 엔진이 시뮬레이터
# 내부를 통째로 끌고 가야 한다. 붙이는 쪽이 알아야 할 것은 전문 형식뿐이다.
add_library(order_sdk STATIC order_sdk.c)
target_include_directories(order_sdk PUBLIC .)
target_link_libraries(order_sdk PUBLIC core)

add_subdirectory(tests)
