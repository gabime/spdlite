cmake_minimum_required(VERSION 3.23)

include(cmake/utils.cmake)

spdlite_extract_version()
project(spdlite VERSION ${SPDLITE_VERSION} LANGUAGES CXX)
message(STATUS "Build spdlite: ${SPDLITE_VERSION}")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json so clangd / clang-tidy / IDEs see the same flags CMake uses.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Build options ----
option(SPDLITE_BUILD_EXAMPLE    "Build spdlite example"                  ON)
option(SPDLITE_BUILD_TESTS      "Build spdlite unit tests (doctest)"     OFF)
option(SPDLITE_BUILD_BENCH      "Build spdlite benchmarks"               OFF)

# Header files (for IDE visibility only - not compiled)
set(SPDLITE_HEADERS
    include/spdlite/common.h
    include/spdlite/formatter.h
    include/spdlite/logger.h
    include/spdlite/sinks/file_sink.h
    include/spdlite/sinks/rotating_file_sink.h
    include/spdlite/sinks/null_sink.h
    include/spdlite/sinks/stdout_sink.h
    include/spdlite/sinks/console_sink.h
    include/spdlite/sinks/shared_sink.h)

# spdlite is header-only (fmt bundled)
add_library(spdlite INTERFACE)
target_include_directories(spdlite INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(spdlite INTERFACE cxx_std_20)
if(MSVC)
    target_compile_options(spdlite INTERFACE /utf-8)
endif()

# Example
if(SPDLITE_BUILD_EXAMPLE)
    add_subdirectory(example)
endif()

# Benchmarks
if(SPDLITE_BUILD_BENCH)
    find_package(benchmark CONFIG)
    if(NOT benchmark_FOUND)
        message(STATUS "Downloading Google Benchmark")
        include(FetchContent)
        set(BENCHMARK_ENABLE_TESTING OFF CACHE INTERNAL "")
        FetchContent_Declare(googlebenchmark
            GIT_REPOSITORY https://github.com/google/benchmark.git
            GIT_TAG v1.9.5)
        FetchContent_MakeAvailable(googlebenchmark)
    endif()

    add_executable(latency bench/latency.cpp)
    target_link_libraries(latency PRIVATE spdlite benchmark::benchmark)

    add_executable(formatter_bench bench/formatter_bench.cpp)
    target_link_libraries(formatter_bench PRIVATE spdlite benchmark::benchmark)

    # Comparative benchmark vs spdlog (optional)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../spdlog/CMakeLists.txt")
        add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../spdlog ${CMAKE_BINARY_DIR}/spdlog_build EXCLUDE_FROM_ALL)
        add_executable(vs_spdlog bench/vs_spdlog.cpp)
        # both header-only - spdlite's fmt (FMT_HEADER_ONLY) and spdlog's inline code share the same symbols cleanly
        target_link_libraries(vs_spdlog PRIVATE spdlite spdlog::spdlog_header_only benchmark::benchmark)
    endif()
endif()

# Tests (doctest, fetched via FetchContent)
if(SPDLITE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
