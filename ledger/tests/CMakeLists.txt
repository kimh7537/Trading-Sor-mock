mini_sor_add_test(test_listener ledger)
mini_sor_add_test(test_worker_pool ledger)
mini_sor_add_test(test_shm ledger)
mini_sor_add_test(test_account ledger)
mini_sor_add_test(test_order_validate ledger)
mini_sor_add_test(test_ledger_core ledger_core)

# 원격 거래소(T12-02)는 **진짜 `exchanged` 프로세스**를 띄워 확인한다.
# 상대역을 흉내 내면 규격을 서로 다르게 읽고 있어도 양쪽 테스트가 통과한다.
mini_sor_add_test(test_remote_venue ledger_core)
target_compile_definitions(test_remote_venue PRIVATE
    EXCHANGED_BIN="$<TARGET_FILE:exchanged>")
add_dependencies(test_remote_venue exchanged)
