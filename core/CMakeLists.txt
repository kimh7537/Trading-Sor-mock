add_library(core STATIC
    src/errors.c
    src/tick_size.c
    src/order_pool.c
)
target_include_directories(core PUBLIC include)

add_subdirectory(tests)
