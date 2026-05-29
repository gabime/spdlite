// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "helpers.h"
#include "spdlite/logger.h"
#include "spdlite/sinks/stdout_sink.h"

using namespace spdlite;
using helpers::contains;
namespace fs = std::filesystem;

static std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST_CASE("stdout_sink / stderr_sink satisfy the log_sink concept") {
    CHECK(log_sink<stdout_sink>);
    CHECK(log_sink<stderr_sink>);
}

TEST_CASE("stdout_sink writes the formatted line to the given FILE") {
    helpers::tmpdir td{"stdout_write"};
    const auto path = td / "out.txt";
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    {
        logger_st<stdout_sink> log{"app", stdout_sink{f}};
        log.info("hello {}", 42);
        log.warn("second line");
        log.flush();
    }
    std::fclose(f);

    auto contents = read_all(path);
    CHECK(contains(contents, "[app]"));
    CHECK(contains(contents, "[INF] hello 42"));
    CHECK(contains(contents, "[WRN] second line"));
    // exactly two log lines (one '\n' each, even with \r\n on Windows)
    CHECK(std::count(contents.begin(), contents.end(), '\n') == 2);
}

TEST_CASE("stdout_sink does not own/close the FILE it was given") {
    helpers::tmpdir td{"stdout_noclose"};
    const auto path = td / "out.txt";
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    {
        logger_st<stdout_sink> log{"app", stdout_sink{f}};
        log.info("x");
    }  // logger (and its sink copy) destroyed - must NOT have closed f
    // f is still usable
    CHECK(std::fputs("after\n", f) >= 0);
    std::fclose(f);
    CHECK(contains(read_all(path), "after"));
}

TEST_CASE("stderr_sink writes without throwing") {
    logger_st<stderr_sink> log{"e", stderr_sink{}};
    CHECK_NOTHROW(log.error("to stderr"));
    CHECK_NOTHROW(log.flush());
}
