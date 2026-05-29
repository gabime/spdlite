# spdlite unit tests (doctest)
#
# Build with: cmake -B build -DSPDLITE_BUILD_TESTS=ON .
# Run with:   ctest --test-dir build --output-on-failure
#
# doctest is fetched at configure time via FetchContent (URL + SHA256).

include(FetchContent)

FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/releases/download/v2.5.2/doctest-v2.5.2.tar.gz
    URL_HASH SHA256=03635c39d59844f1550b9ffa56e3cbc79bddfed557e8465cd3692628aadba70a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(DOCTEST_NO_INSTALL ON CACHE INTERNAL "")
set(DOCTEST_WITH_TESTS OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(doctest)

add_executable(spdlite_tests
    test_level.cpp
    test_formatter.cpp
    test_logger.cpp
    test_file_sink.cpp
    test_rotating_file_sink.cpp
    test_null_sink.cpp
    test_stdout_sink.cpp
    test_console_sink.cpp
    test_shared_sink.cpp
    test_thread_safety.cpp
    test_log_macros_warn.cpp
    test_log_macros_trace.cpp
    test_log_macros_off.cpp)

target_link_libraries(spdlite_tests PRIVATE spdlite doctest::doctest_with_main)
target_include_directories(spdlite_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# doctest's CHECK macros expand to "decomposer << a == b" patterns that trip
# -Wparentheses (operator<< precedence vs ==). The operator chain is intentional.
target_compile_options(spdlite_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-parentheses>)

add_test(NAME spdlite_tests COMMAND spdlite_tests)
