cmake_minimum_required(VERSION 3.16)

project(ByteCacheDB VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

include_directories(include)
find_package(Threads REQUIRED)

set(BYTECACHEDB_CORE_SOURCES
    src/client_handler.cpp
    src/command_parser.cpp
    src/metrics.cpp
    src/persistence.cpp
    src/response.cpp
    src/server.cpp
    src/storage_engine.cpp
    src/thread_pool.cpp
    src/ttl_manager.cpp
)

add_library(bytecachedb_core ${BYTECACHEDB_CORE_SOURCES})
target_include_directories(bytecachedb_core PUBLIC include)
target_link_libraries(bytecachedb_core PUBLIC Threads::Threads)

add_executable(bytecachedb src/main.cpp)
target_link_libraries(bytecachedb PRIVATE bytecachedb_core)

enable_testing()

function(add_bytecachedb_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE bytecachedb_core)
    target_include_directories(${test_name} PRIVATE include tests)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

add_bytecachedb_test(test_parser tests/test_parser.cpp)
add_bytecachedb_test(test_storage tests/test_storage.cpp)
add_bytecachedb_test(test_ttl tests/test_ttl.cpp)
add_bytecachedb_test(test_persistence tests/test_persistence.cpp)
add_bytecachedb_test(test_concurrency tests/test_concurrency.cpp)
add_bytecachedb_test(test_tcp_integration tests/test_tcp_integration.cpp)
