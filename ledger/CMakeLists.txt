# 원장. 소켓·시그널·프로세스는 POSIX다.
# -std=c11(엄격 모드)에서는 보이지 않으므로 여기서 연다. core/exchange/sor는
# 시스템 호출을 하지 않으므로 이 매크로가 필요 없다.
add_library(ledger STATIC
    src/listener.c
    src/worker_pool.c
    src/shm_segment.c
    src/account.c
    src/order_validate.c
)
target_include_directories(ledger PUBLIC include)
# 계좌 락이 pthread 프로세스 공유 뮤텍스다. 링크가 필요하다.
find_package(Threads REQUIRED)
target_link_libraries(ledger PUBLIC core Threads::Threads)
target_compile_definitions(ledger PUBLIC _POSIX_C_SOURCE=200809L)

add_executable(ledgerd ledgerd.c)
target_link_libraries(ledgerd PRIVATE ledger)

add_subdirectory(tests)
