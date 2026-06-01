cmake_minimum_required(VERSION 3.20)

project(MarketDataServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
    
add_library(marketdata_core INTERFACE)
target_include_directories(marketdata_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(marketdata_core INTERFACE Threads::Threads)

add_executable(marketdata_server src/apps/marketdata_server.cpp)
target_link_libraries(marketdata_server PRIVATE marketdata_core)

add_executable(marketdata_client src/apps/marketdata_client.cpp)
target_link_libraries(marketdata_client PRIVATE marketdata_core)

enable_testing()

add_executable(packet_tests tests/packet_tests.cpp)
target_link_libraries(packet_tests PRIVATE marketdata_core)
add_test(NAME packet_tests COMMAND packet_tests)

add_executable(ring_buffer_tests tests/ring_buffer_tests.cpp)
target_link_libraries(ring_buffer_tests PRIVATE marketdata_core)
add_test(NAME ring_buffer_tests COMMAND ring_buffer_tests)

add_executable(redis_command_tests tests/redis_command_tests.cpp)
target_link_libraries(redis_command_tests PRIVATE marketdata_core)
add_test(NAME redis_command_tests COMMAND redis_command_tests)
