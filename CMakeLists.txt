cmake_minimum_required(VERSION 3.20)

project(FruitsCar
    VERSION 0.1.0
    DESCRIPTION "FruitsCar phase1 logic core, local host and rule tests"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(FC_WARNING_FLAGS "")
if(MSVC)
    # G1：启用 /W4，并把警告视为错误。
    set(FC_WARNING_FLAGS /W4 /WX /permissive- /utf-8 /EHsc)
else()
    set(FC_WARNING_FLAGS -Wall -Wextra -Werror -Wpedantic)
endif()

function(fc_apply_warnings target)
    target_compile_options(${target} PRIVATE ${FC_WARNING_FLAGS})
endfunction()

# ---------------------------------------------------------------------------
# fc_core：纯 C++17 玩法逻辑，不依赖引擎、不涉及网络套接字、不做文件 IO。
# ---------------------------------------------------------------------------
file(GLOB FC_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/core/src/*.cpp)

add_library(fc_core STATIC ${FC_CORE_SOURCES})
target_include_directories(fc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
target_compile_features(fc_core PUBLIC cxx_std_17)
fc_apply_warnings(fc_core)
if(MSVC)
    target_compile_definitions(fc_core PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# ---------------------------------------------------------------------------
# fc_host：本地宿主进程，固定步长 tick 驱动 + HTTP/JSON 协议 + 日志。
# ---------------------------------------------------------------------------
add_executable(fc_host
    ${CMAKE_CURRENT_SOURCE_DIR}/host/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/host/catalogue.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/host/http_server.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/host/log_writer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/host/autoplay.cpp)
target_link_libraries(fc_host PRIVATE fc_core)
fc_apply_warnings(fc_host)
if(WIN32)
    target_link_libraries(fc_host PRIVATE ws2_32)
endif()

# ---------------------------------------------------------------------------
# fc_tests：独立规则测试可执行文件，直接链接 core。
# tests/ 目录只保留在本地、不纳入仓库，因此仅当存在测试代码时才配置该目标。
# ---------------------------------------------------------------------------
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_main.cpp)
    file(GLOB FC_TEST_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/cases/*.cpp)

    add_executable(fc_tests ${FC_TEST_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/host/autoplay.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/host/catalogue.cpp)
    target_link_libraries(fc_tests PRIVATE fc_core)
    target_include_directories(fc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/host)
    fc_apply_warnings(fc_tests)
    if(MSVC)
        target_compile_definitions(fc_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    message(STATUS "FruitsCar phase1 targets: fc_core fc_host fc_tests")
else()
    message(STATUS "FruitsCar phase1 targets: fc_core fc_host (tests/ not present, fc_tests skipped)")
endif()
