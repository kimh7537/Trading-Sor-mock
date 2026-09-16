mini_sor_add_test(test_types core)
mini_sor_add_test(test_tick_size core)
mini_sor_add_test(test_order_pool core)

# assert가 터지면 통과하도록 테스트가 SIGABRT를 직접 받는다. 파일 주석 참조.
mini_sor_add_test(test_order_pool_double_free core)
mini_sor_add_test(test_order_index core)
mini_sor_add_test(test_wire core)
mini_sor_add_test(test_msg core)
mini_sor_add_test(test_journal core)
mini_sor_add_test(test_snapshot core)
mini_sor_add_test(test_recon core)
mini_sor_add_test(test_fault_inject core)
mini_sor_add_test(test_feed core)
