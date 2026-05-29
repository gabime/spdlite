// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "helpers.h"
#include "spdlite/logger.h"
#include "spdlite/sinks/console_sink.h"

using namespace spdlite;
using helpers::contains;
namespace fs = std::filesystem;

TEST_CASE("console_sink / console_err_sink satisfy the log_sink concept") {
    CHECK(log_sink<console_sink>);
    CHECK(log_sink<console_err_sink>);
}

TEST_CASE("console_sink constructs with every color_mode and writes without throwing") {
    for (auto mode : {color_mode::automatic, color_mode::always, color_mode::never}) {
        logger_st<console_sink> log{"c", console_sink{mode}};
        log.set_log_level(level::trace);
        CHECK_NOTHROW(log.trace("t"));
        CHECK_NOTHROW(log.info("i {}", 1));
        CHECK_NOTHROW(log.critical("boom"));
        CHECK_NOTHROW(log.flush());
    }
}

TEST_CASE("console_err_sink writes without throwing") {
    logger_st<console_err_sink> log{"e", console_err_sink{}};
    CHECK_NOTHROW(log.error("to stderr"));
    CHECK_NOTHROW(log.flush());
}

#ifndef _WIN32
// On POSIX the color path emits ANSI escape codes around the level tag. The
// public console_sink hardcodes stdout/stderr, so we drive the detail base at
// a temp FILE to capture and inspect the bytes. (The Windows path uses the
// native console API and can't be captured this way.)
static std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST_CASE("console_sink_base wraps the level tag in ANSI codes when color_mode::always") {
    helpers::tmpdir td{"console_color"};
    const auto path = td / "out.txt";
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    {
        logger_st<detail::console_sink_base> log{"c", detail::console_sink_base{f, color_mode::always}};
        log.info("hello");
        log.flush();
    }
    std::fclose(f);

    auto contents = read_all(path);
    CHECK(contains(contents, "\033[32m"));  // green (info) before the tag
    CHECK(contains(contents, "\033[m"));    // reset after the tag
    CHECK(contains(contents, "INF"));
    CHECK(contains(contents, "hello"));
}

TEST_CASE("console_sink_base emits no escape codes when color_mode::never") {
    helpers::tmpdir td{"console_plain"};
    const auto path = td / "out.txt";
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f != nullptr);
    {
        logger_st<detail::console_sink_base> log{"c", detail::console_sink_base{f, color_mode::never}};
        log.info("hello");
        log.flush();
    }
    std::fclose(f);

    auto contents = read_all(path);
    CHECK(!contains(contents, "\033["));  // no ANSI escapes
    CHECK(contains(contents, "[INF] hello"));
}
#endif
