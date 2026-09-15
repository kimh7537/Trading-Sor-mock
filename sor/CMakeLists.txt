add_library(sor STATIC
    src/consolidated.c
    src/best_execution.c
    src/strategy.c
    src/strategy_krx_only.c
)
target_include_directories(sor PUBLIC include)
target_link_libraries(sor PUBLIC exchange)

add_subdirectory(tests)
