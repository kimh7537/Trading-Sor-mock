add_library(exchange STATIC
    src/book/price_level.c
)
target_include_directories(exchange PUBLIC include)
target_link_libraries(exchange PUBLIC core)

add_subdirectory(tests)
