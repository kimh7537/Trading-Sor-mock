add_library(sor STATIC
    src/consolidated.c
    src/best_execution.c
    src/strategy.c
    src/strategy_krx_only.c
    src/strategy_best_price.c
    src/strategy_split.c
    src/strategy_sweep.c
    src/order_map.c
    src/executor.c
    src/routing_log.c
    src/execution_quality.c
)
target_include_directories(sor PUBLIC include)
target_link_libraries(sor PUBLIC exchange)

add_subdirectory(tests)
