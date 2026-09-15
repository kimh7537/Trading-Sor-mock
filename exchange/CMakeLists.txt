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
)
target_include_directories(exchange PUBLIC include PRIVATE src/match)
target_link_libraries(exchange PUBLIC core m)

add_subdirectory(tests)
