# 벤치마크는 Release에서만 의미가 있다. Debug에서 돌리면 숫자가 오해를 부른다.
add_executable(bench_match bench_match.c)
target_link_libraries(bench_match PRIVATE exchange)

# clock_gettime은 POSIX다. -std=c11(엄격 모드)에서는 보이지 않으므로 여기서만 연다.
# 엔진 쪽은 시스템 시각을 읽지 않으므로 이 매크로가 필요 없다.
target_compile_definitions(bench_match PRIVATE _POSIX_C_SOURCE=200809L)

# ctest에 넣지 않는다. 20만 건 x 3 시나리오는 CI가 매번 돌릴 일이 아니다.

# 전 구간 성능 측정(T5-05). 원장까지 지나므로 bench에서 처음으로 ledger를 건다.
add_executable(bench_pipeline bench_pipeline.c)
target_link_libraries(bench_pipeline PRIVATE sor ledger)
target_compile_definitions(bench_pipeline PRIVATE _POSIX_C_SOURCE=200809L)

# 이것도 ctest에 넣지 않는다. 저널을 켠 판은 fsync가 2만 번 돈다.

# 전략 비교 하네스. 실험 본체는 라이브러리로 두어 테스트가 직접 부를 수 있게 한다.
add_library(compare STATIC compare.c quality.c)
target_include_directories(compare PUBLIC .)
target_link_libraries(compare PUBLIC sor)

add_executable(compare_strategies compare_strategies.c)
target_link_libraries(compare_strategies PRIVATE compare)

# 집행 품질 리포트(T5-08). 시드 30개면 compare_run 30번이다. ctest에 넣지 않는다.
add_executable(quality_report quality_report.c)
target_link_libraries(quality_report PRIVATE compare)

add_subdirectory(tests)
