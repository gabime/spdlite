// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "helpers.h"
#include "spdlite/logger.h"
#include "spdlite/sinks/file_sink.h"
#include "spdlite/sinks/null_sink.h"

using namespace spdlite;
using helpers::capture_sink;
using helpers::contains;

TEST_CASE("named ctor sets the logger name and embeds it in the header") {
    capture_sink cap;
    logger_st<capture_sink> log{"my_logger", cap};

    CHECK(log.get_name() == "my_logger");
    log.info("hello");
    REQUIRE(cap.state->formatted.size() == 1);
    CHECK(contains(cap.state->formatted[0], "[my_logger]"));
}

TEST_CASE("sinks-only ctor produces an empty name and no name bracket in the header") {
    capture_sink cap;
    logger_st<capture_sink> log{cap};

    CHECK(log.get_name().empty());
    log.info("hello");
    REQUIRE(cap.state->formatted.size() == 1);
    // header shape with no name: "[ts] [INF] hello\n" - level tag butts up against payload
    CHECK(contains(cap.state->formatted[0], "[INF] hello"));
}

namespace {

// default-constructible sinks; state is static because the logger constructs its own instances
struct default_sink {
    static inline std::vector<std::string> lines;
    void write(const log_msg& msg) { lines.emplace_back(msg.formatted); }
    void flush() {}
};

struct other_default_sink {
    static inline std::vector<std::string> lines;
    void write(const log_msg& msg) { lines.emplace_back(msg.formatted); }
    void flush() {}
};

struct needs_arg_sink {
    explicit needs_arg_sink(int) {}
    void write(const log_msg&) {}
    void flush() {}
};

}  // namespace

// every ctor form resolves to exactly one ctor, also when the sinks are default-constructible
static_assert(std::is_constructible_v<logger_st<default_sink>>);
static_assert(std::is_constructible_v<logger_st<default_sink>, std::string>);
static_assert(std::is_constructible_v<logger_st<default_sink>, default_sink>);
static_assert(std::is_constructible_v<logger_st<default_sink>, std::string, default_sink>);
static_assert(std::is_constructible_v<logger_st<>>);
static_assert(std::is_constructible_v<logger_st<>, std::string>);

// sinks without a default ctor must be passed explicitly, also when mixed with default-constructible ones
static_assert(!std::is_constructible_v<logger_st<needs_arg_sink>>);
static_assert(!std::is_constructible_v<logger_st<needs_arg_sink>, std::string>);
static_assert(std::is_constructible_v<logger_st<needs_arg_sink>, std::string, needs_arg_sink>);
static_assert(!std::is_constructible_v<logger_st<default_sink, needs_arg_sink>, std::string>);
static_assert(std::is_constructible_v<logger_st<default_sink, needs_arg_sink>, std::string, default_sink, needs_arg_sink>);

// explicit: a name never converts implicitly to a logger
static_assert(!std::is_convertible_v<std::string, logger_st<default_sink>>);
static_assert(!std::is_convertible_v<std::string, logger_st<>>);

// bundled sinks
static_assert(std::is_constructible_v<logger_mt<null_sink>, std::string>);
static_assert(!std::is_constructible_v<logger_mt<file_sink>, std::string>);

TEST_CASE("name-only ctor default-constructs the sinks") {
    default_sink::lines.clear();
    logger_st<default_sink> log{"app"};

    CHECK(log.get_name() == "app");
    CHECK(log.get_log_level() == level::info);
    log.info("hello {}", 1);
    REQUIRE(default_sink::lines.size() == 1);
    CHECK(contains(default_sink::lines[0], "[app] [INF] hello 1"));
}

TEST_CASE("default ctor default-constructs the sinks with an empty name") {
    default_sink::lines.clear();
    logger_st<default_sink> log;

    CHECK(log.get_name().empty());
    log.warn("w");
    REQUIRE(default_sink::lines.size() == 1);
    CHECK(contains(default_sink::lines[0], "[WRN] w"));
}

TEST_CASE("name-only ctor default-constructs every sink of a multi-sink logger") {
    default_sink::lines.clear();
    other_default_sink::lines.clear();
    logger_st<default_sink, other_default_sink> log{"multi"};

    log.error("boom");
    REQUIRE(default_sink::lines.size() == 1);
    REQUIRE(other_default_sink::lines.size() == 1);
    CHECK(contains(default_sink::lines[0], "[multi] [ERR] boom"));
    CHECK(default_sink::lines[0] == other_default_sink::lines[0]);
}

TEST_CASE("name-only ctor works with make_shared behind shared_ptr<logger>") {
    default_sink::lines.clear();
    std::shared_ptr<logger> log = std::make_shared<logger_mt<default_sink>>("shared");

    log->info("via {}", "shared_ptr");
    REQUIRE(default_sink::lines.size() == 1);
    CHECK(contains(default_sink::lines[0], "[shared] [INF] via shared_ptr"));
}

TEST_CASE("an explicitly passed sink is used even when the sink is default-constructible") {
    static_assert(std::is_default_constructible_v<capture_sink>);
    capture_sink cap;
    logger_st<capture_sink> named{"app", cap};
    logger_st<capture_sink> unnamed{cap};

    named.info("a");
    unnamed.info("b");
    REQUIRE(cap.state->payloads.size() == 2);
    CHECK(cap.state->payloads[0] == "a");
    CHECK(cap.state->payloads[1] == "b");
}

TEST_CASE("sinkless logger defaults to level off") {
    logger_st<> unnamed;
    logger_mt<> named{"noop"};
    CHECK(unnamed.get_log_level() == level::off);
    CHECK(named.get_log_level() == level::off);
    CHECK(named.get_name() == "noop");
    CHECK_FALSE(named.should_log(level::critical));
    named.info("dropped {}", 1);
    named.flush();
}

TEST_CASE("log_level get/set round trips") {
    logger_st<null_sink> log{"x", null_sink{}};
    CHECK(log.get_log_level() == level::info);  // default
    constexpr level all[] = {level::trace, level::debug, level::info, level::warn, level::err, level::critical, level::off};
    for (auto lvl : all) {
        log.set_log_level(lvl);
        CHECK(log.get_log_level() == lvl);
    }
}

TEST_CASE("flush_level get/set round trips; default is off (no auto-flush)") {
    logger_st<null_sink> log{"x", null_sink{}};
    CHECK(log.get_flush_level() == level::off);
    // off => never auto-flush in practice (callers never log at level::off)
    CHECK(log.should_flush(level::trace) == false);
    CHECK(log.should_flush(level::critical) == false);

    constexpr level all[] = {level::trace, level::debug, level::info, level::warn, level::err, level::critical, level::off};
    for (auto fl : all) {
        log.set_flush_level(fl);
        CHECK(log.get_flush_level() == fl);
        for (auto msg : all) {
            const bool expected = static_cast<int>(msg) >= static_cast<int>(fl);
            CHECK(log.should_flush(msg) == expected);
        }
    }
}

TEST_CASE("auto-flush triggers when message level >= flush_level") {
    capture_sink cap;
    logger_st<capture_sink> log{"x", cap};
    log.set_flush_level(level::err);

    log.info("hi");  // below threshold, no flush
    CHECK(cap.state->flush_count == 0);

    log.error("oops");  // at threshold, should flush
    CHECK(cap.state->flush_count == 1);

    log.critical("boom");  // above threshold, should flush
    CHECK(cap.state->flush_count == 2);
}

TEST_CASE("manual flush() reaches every sink in the tuple") {
    capture_sink a;
    capture_sink b;
    logger_st<capture_sink, capture_sink> log{"x", a, b};
    log.flush();
    log.flush();
    CHECK(a.state->flush_count == 2);
    CHECK(b.state->flush_count == 2);
}

TEST_CASE("multi-sink fan-out: every sink sees every message") {
    capture_sink a;
    capture_sink b;
    logger_st<capture_sink, capture_sink> log{"x", a, b};
    log.info("one");
    log.info("two");
    log.info("three");
    CHECK(a.state->payloads.size() == 3);
    CHECK(b.state->payloads.size() == 3);
    CHECK(a.state->payloads == b.state->payloads);
}

TEST_CASE("string_view overload emits the raw payload with no formatting") {
    capture_sink cap;
    logger_st<capture_sink> log{"x", cap};

    // Explicit std::string_view selects the raw overload. The fmt overload would
    // reject "{}" at compile time as an unfilled placeholder.
    std::string_view raw = "literal {} braces";
    log.info(raw);
    REQUIRE(cap.state->payloads.size() == 1);
    CHECK(cap.state->payloads[0] == "literal {} braces");
}

TEST_CASE("fmt-string overload formats arguments") {
    capture_sink cap;
    logger_st<capture_sink> log{"x", cap};
    log.info("{} + {} = {}", 1, 2, 3);
    REQUIRE(cap.state->payloads.size() == 1);
    CHECK(cap.state->payloads[0] == "1 + 2 = 3");
}

TEST_CASE("per-level convenience methods set the correct level") {
    capture_sink cap;
    logger_st<capture_sink> log{"x", cap};
    log.set_log_level(level::trace);

    log.trace("t");
    log.debug("d");
    log.info("i");
    log.warn("w");
    log.error("e");
    log.critical("c");

    REQUIRE(cap.state->levels.size() == 6);
    CHECK(cap.state->levels[0] == level::trace);
    CHECK(cap.state->levels[1] == level::debug);
    CHECK(cap.state->levels[2] == level::info);
    CHECK(cap.state->levels[3] == level::warn);
    CHECK(cap.state->levels[4] == level::err);
    CHECK(cap.state->levels[5] == level::critical);
}

TEST_CASE("formatted line ends in a newline") {
    capture_sink cap;
    logger_st<capture_sink> log{"x", cap};
    log.info("hello");
    REQUIRE(cap.state->formatted.size() == 1);
    CHECK(cap.state->formatted[0].back() == '\n');
}

TEST_CASE("logger never throws to caller when a sink throws on write") {
    capture_sink cap;
    cap.fail_writes(true);
    logger_st<capture_sink> log{"x", cap};
    log.set_log_level(level::trace);  // exercise every level path

    // Every entrypoint must swallow the sink's exception. Logger writes a line to
    // stderr per caught exception - expected noise during this test.
    CHECK_NOTHROW(log.trace("t"));
    CHECK_NOTHROW(log.debug("d"));
    CHECK_NOTHROW(log.info("hello"));
    CHECK_NOTHROW(log.warn("w"));
    CHECK_NOTHROW(log.error("e"));
    CHECK_NOTHROW(log.critical("c"));
    CHECK_NOTHROW(log.info("{} + {} = {}", 1, 2, 3));  // fmt path
    CHECK_NOTHROW(log.info(std::string_view{"raw"}));  // string_view path
    CHECK_NOTHROW(log.log(level::info, "via log()"));  // generic log()
    CHECK(cap.state->payloads.empty());                // nothing reached the sink

    // logger recovers once the sink stops throwing
    cap.fail_writes(false);
    CHECK_NOTHROW(log.info("after"));
    REQUIRE(cap.state->payloads.size() == 1);
    CHECK(cap.state->payloads[0] == "after");
}

TEST_CASE("format_options() reconfigures the header in place") {
    capture_sink cap;
    logger_st<capture_sink> log{"app", cap};

    log.info("first");  // default shape
    log.set_format_options({.utc = true, .show_date = false, .precision = time_precision::none});
    log.info("second");  // new shape

    REQUIRE(cap.state->formatted.size() == 2);
    // first line: full default header includes the date and the .mmm
    CHECK(contains(cap.state->formatted[0], "[app]"));
    CHECK(contains(cap.state->formatted[0], "[INF] first"));
    // second line: time-only header, no date, no millis
    const auto& second = cap.state->formatted[1];
    CHECK(second[0] == '[');
    CHECK(second[9] == ']');  // [HH:MM:SS]
    CHECK(contains(second, "] [app] [INF] second"));
    CHECK(!contains(second, "."));  // no .mmm anywhere in header
}

TEST_CASE("log_msg.level_offset always points at the level tag, for any format_options") {
    // Regression guard: console_sink (and any color sink) relies on msg.level_offset
    // landing exactly on the 3-byte level tag. If the logger forgets to propagate it
    // or the formatter miscomputes it, color codes wrap the wrong bytes.
    auto check = [](format_options opts, level lvl, std::string_view tag, std::string_view name) {
        capture_sink cap;
        logger_st<capture_sink> log{std::string{name}, cap};
        log.set_format_options(opts);
        log.set_log_level(level::trace);
        log.log(lvl, "x");
        REQUIRE(cap.state->formatted.size() == 1);
        const auto& line = cap.state->formatted[0];
        const auto offset = cap.state->level_offsets[0];
        CHECK(line.substr(offset, level_width) == tag);
    };
    // matrix: every flag combo x named/unnamed x representative levels
    check({}, level::info, "INF", "app");
    check({}, level::info, "INF", "");
    check({.utc = true}, level::warn, "WRN", "app");
    check({.show_date = false}, level::trace, "TRC", "app");
    check({.show_date = false}, level::trace, "TRC", "");
    check({.precision = time_precision::none}, level::err, "ERR", "app");
    check({.precision = time_precision::us}, level::critical, "CRT", "app");
    check({.precision = time_precision::ns}, level::debug, "DBG", "app");
    check({.show_date = false, .precision = time_precision::none}, level::info, "INF", "");
    check({.utc = true, .show_date = false, .precision = time_precision::ns}, level::warn, "WRN", "n");
}

TEST_CASE("formatted line contains the rendered payload after the header") {
    capture_sink cap;
    logger_st<capture_sink> log{"name", cap};
    log.info("value={}", 42);
    REQUIRE(cap.state->formatted.size() == 1);
    const auto& line = cap.state->formatted[0];
    CHECK(contains(line, "[name]"));
    CHECK(contains(line, "[INF]"));
    CHECK(contains(line, "value=42"));
}
