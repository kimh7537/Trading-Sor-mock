# FEP. epoll·소켓은 POSIX/리눅스다.
# -std=c11(엄격 모드)에서는 보이지 않으므로 여기서 연다.
add_library(fep STATIC
    src/evloop.c
    src/framer.c
)
target_include_directories(fep PUBLIC include)
target_link_libraries(fep PUBLIC core)
target_compile_definitions(fep PUBLIC _POSIX_C_SOURCE=200809L)

add_subdirectory(tests)
