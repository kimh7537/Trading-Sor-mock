add_library(core STATIC
    src/errors.c
    src/tick_size.c
    src/order_pool.c
    src/order_index.c
    src/journal.c
    src/snapshot.c
    src/recon.c
    src/wire.c
    src/msg.c
)
target_include_directories(core PUBLIC include)

# 저널이 fsync/fileno를 쓴다. -std=c11(엄격 모드)에서는 보이지 않으므로 연다.
# fep가 epoll 때문에 같은 방식을 쓰고 있다.
target_compile_definitions(core PUBLIC _POSIX_C_SOURCE=200809L)

add_subdirectory(tests)
