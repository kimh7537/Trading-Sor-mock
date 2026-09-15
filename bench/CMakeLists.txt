# 벤치마크는 Release에서만 의미가 있다. Debug에서 돌리면 숫자가 오해를 부른다.
add_executable(bench_match bench_match.c)
target_link_libraries(bench_match PRIVATE exchange)

# clock_gettime은 POSIX다. -std=c11(엄격 모드)에서는 보이지 않으므로 여기서만 연다.
# 엔진 쪽은 시스템 시각을 읽지 않으므로 이 매크로가 필요 없다.
target_compile_definitions(bench_match PRIVATE _POSIX_C_SOURCE=200809L)

# ctest에 넣지 않는다. 20만 건 x 3 시나리오는 CI가 매번 돌릴 일이 아니다.
