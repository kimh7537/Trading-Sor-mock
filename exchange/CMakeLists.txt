add_library(exchange STATIC
    src/book/price_level.c
    src/book/order_book.c
    src/match/match_engine.c
    src/match/match_limit.c
    src/match/match_market.c
    src/match/match_ioc_fok.c
    src/match/match_cancel_modify.c
    src/event.c
    src/rules/market_rules.c
    src/rules/krx.c
    src/rules/nxt.c
    src/liquidity/synthetic.c
    src/liquidity/divergent.c
    src/feed_source.c
)
target_include_directories(exchange PUBLIC include PRIVATE src/match)
target_link_libraries(exchange PUBLIC core m)

# 거래소 프로세스(T12-01). **라이브러리가 아니라 실행 파일만** ledger를 링크한다 —
# 리스너가 거기 있기 때문이고, `exchange` 라이브러리 자체는 core만 의존한 채로 둔다.
# (`ledger` 라이브러리는 core만 링크하므로 순환이 생기지 않는다)
add_executable(exchanged exchanged.c)
target_link_libraries(exchanged PRIVATE exchange ledger)

add_subdirectory(tests)
