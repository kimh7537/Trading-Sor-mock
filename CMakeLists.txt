cmake_minimum_required(VERSION 3.20)
project(mini_sor C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror -Wshadow -Wconversion)

option(ENABLE_ASAN "AddressSanitizer + UBSan 빌드" OFF)
if(ENABLE_ASAN)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# 테스트 하나 = 독립 실행 가능한 main(). 프레임워크 없이 assert 기반.
#
# 테스트에서는 NDEBUG를 강제로 끈다. Release 빌드에서 assert가 사라지면 테스트가
# 아무것도 검증하지 않고 통과한다 — "통과했다"가 거짓말이 되는 상황을 막는다.
# 이중 해제 감지처럼 assert가 터지는 것 자체가 기대 동작인 테스트도 있다.
function(mini_sor_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE ${ARGN})
    target_compile_options(${name} PRIVATE -UNDEBUG)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

enable_testing()
add_subdirectory(core)
add_subdirectory(exchange)
add_subdirectory(sor)
add_subdirectory(ledger)
add_subdirectory(fep)
add_subdirectory(bench)
