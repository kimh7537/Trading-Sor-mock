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
function(mini_sor_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

enable_testing()
add_subdirectory(core)
