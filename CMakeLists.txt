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
option(SPDLITE_BUILD_ALL        "Build everything (example, tests, bench)" OFF)
option(SPDLITE_BUILD_EXAMPLE    "Build spdlite example"                  ON)
option(SPDLITE_BUILD_TESTS      "Build spdlite unit tests (doctest)"     OFF)
option(SPDLITE_BUILD_BENCH      "Build spdlite benchmarks"               OFF)
option(SPDLITE_USE_STD_FORMAT   "Use std::format instead of bundled fmt" OFF)
option(SPDLITE_INSTALL          "Generate install target"               ${PROJECT_IS_TOP_LEVEL})

if(SPDLITE_BUILD_ALL)
    set(SPDLITE_BUILD_EXAMPLE ON)
    set(SPDLITE_BUILD_TESTS   ON)
    set(SPDLITE_BUILD_BENCH   ON)
endif()

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
add_library(spdlite::spdlite ALIAS spdlite)

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

# ---- Install / export ----
# Header-only: install the headers and an exported spdlite::spdlite target so
# consumers can `find_package(spdlite)`. The bundled fmt/ headers ship along
# with the include directory.
if(SPDLITE_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(SPDLITE_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/spdlite")

    install(TARGETS spdlite EXPORT spdliteTargets)
    install(DIRECTORY include/spdlite DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    install(EXPORT spdliteTargets
        FILE spdliteTargets.cmake
        NAMESPACE spdlite::
        DESTINATION ${SPDLITE_CMAKE_DIR})

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/spdliteConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/spdliteConfig.cmake
        INSTALL_DESTINATION ${SPDLITE_CMAKE_DIR})

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/spdliteConfigVersion.cmake
        VERSION ${SPDLITE_VERSION}
        COMPATIBILITY SameMajorVersion)

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/spdliteConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/spdliteConfigVersion.cmake
        DESTINATION ${SPDLITE_CMAKE_DIR})
endif()
