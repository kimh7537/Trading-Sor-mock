add_library(exchange STATIC
    src/book/price_level.c
    src/book/order_book.c
    src/match/match_engine.c
    src/match/match_limit.c
    src/match/match_market.c
)
target_include_directories(exchange PUBLIC include PRIVATE src/match)
target_link_libraries(exchange PUBLIC core)

add_subdirectory(tests)
