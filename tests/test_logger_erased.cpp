// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include <memory>
#include <type_traits>

#include "helpers.h"
#include "spdlite/logger.h"

using namespace spdlite;
using helpers::capture_sink;
using helpers::contains;

static_assert(std::is_abstract_v<logger>);
static_assert(std::has_virtual_destructor_v<logger>);
static_assert(std::is_base_of_v<logger, logger_mt<capture_sink>>);
static_assert(std::is_base_of_v<logger, logger_st<capture_sink>>);
static_assert(std::is_base_of_v<logger, logger_st<>>);
static_assert(std::is_final_v<logger_mt<capture_sink>>);
static_assert(!std::is_copy_constructible_v<logger_mt<capture_sink>>);
static_assert(std::is_nothrow_move_constructible_v<logger_mt<capture_sink>>);

namespace {

// consumer that only knows the erased type
void consume(logger& log) {
    log.info("hello {}", 42);
    log.warn("raw");
}

// flags its own destruction; move leaves the source's flag null so only the live instance reports
struct dtor_sink {
    std::shared_ptr<bool> destroyed;
    explicit dtor_sink(std::shared_ptr<bool> flag)
        : destroyed(std::move(flag)) {}
    dtor_sink(dtor_sink&&) = default;
    ~dtor_sink() {
        if (destroyed) *destroyed = true;
    }
    void write(const log_msg&) {}
    void flush() {}
};

}  // namespace

TEST_CASE("mt, st and sinkless loggers all pass as logger&") {
    capture_sink a, b;
    logger_mt<capture_sink> mt{"mt", a};
    logger_st<capture_sink> st{"st", b};
    logger_st<> noop{"noop"};
    consume(mt);
    consume(st);
    consume(noop);

    REQUIRE(a.state->payloads.size() == 2);
    CHECK(a.state->payloads[0] == "hello 42");
    CHECK(a.state->payloads[1] == "raw");
    CHECK(contains(a.state->formatted[0], "[mt] [INF] hello 42"));
    REQUIRE(b.state->payloads.size() == 2);
    CHECK(contains(b.state->formatted[1], "[st] [WRN] raw"));
}

TEST_CASE("level filtering through logger& uses the concrete logger's level") {
    capture_sink cap;
    logger_st<capture_sink> st{"x", cap};
    logger& log = st;

    log.set_log_level(level::warn);
    CHECK(st.get_log_level() == level::warn);
    CHECK_FALSE(log.should_log(level::info));
    log.info("dropped {}", 1);
    log.info("dropped");
    log.error("kept");
    REQUIRE(cap.state->payloads.size() == 1);
    CHECK(cap.state->payloads[0] == "kept");
}

TEST_CASE("flush, set_name and set_format_options through logger& reach the concrete logger") {
    capture_sink cap;
    logger_st<capture_sink> st{"old", cap};
    logger& log = st;

    log.flush();
    CHECK(cap.state->flush_count == 1);
    log.set_flush_level(level::err);
    log.error("e");
    CHECK(cap.state->flush_count == 2);

    log.set_name("new");
    CHECK(st.get_name() == "new");
    log.set_format_options({.show_date = false, .precision = time_precision::none});
    log.info("x");
    REQUIRE(cap.state->formatted.size() == 2);
    CHECK(cap.state->formatted[1][9] == ']');  // [HH:MM:SS]
    CHECK(contains(cap.state->formatted[1], "] [new] [INF] x"));
}

TEST_CASE("shared_ptr<logger> shares one concrete logger") {
    capture_sink cap;
    std::shared_ptr<logger> a = std::make_shared<logger_mt<capture_sink>>("shared", cap);
    std::shared_ptr<logger> b = a;
    a->info("from a");
    b->info("from {}", "b");
    REQUIRE(cap.state->payloads.size() == 2);
    CHECK(cap.state->payloads[1] == "from b");
}

TEST_CASE("shared_ptr<logger> can be re-pointed from a no-op logger to a real one") {
    capture_sink cap;
    std::shared_ptr<logger> log = std::make_shared<logger_st<>>("app");
    log->critical("dropped");
    log = std::make_shared<logger_st<capture_sink>>("app", cap);
    log->info("kept");
    REQUIRE(cap.state->payloads.size() == 1);
    CHECK(cap.state->payloads[0] == "kept");
}

TEST_CASE("unique_ptr<logger> destroys the concrete logger and its sinks") {
    auto destroyed = std::make_shared<bool>(false);
    std::unique_ptr<logger> log = std::make_unique<logger_st<dtor_sink>>("x", dtor_sink{destroyed});
    CHECK_FALSE(*destroyed);
    log.reset();
    CHECK(*destroyed);
}

TEST_CASE("move ctor transfers name, levels and sinks; moved-from logger is silenced") {
    capture_sink cap;
    logger_st<capture_sink> src{"src", cap};
    src.set_log_level(level::debug);
    src.set_flush_level(level::warn);

    logger_st<capture_sink> dst{std::move(src)};
    CHECK(dst.get_name() == "src");
    CHECK(dst.get_log_level() == level::debug);
    CHECK(dst.get_flush_level() == level::warn);
    CHECK(src.get_log_level() == level::off);

    dst.debug("moved");
    REQUIRE(cap.state->payloads.size() == 1);
    CHECK(contains(cap.state->formatted[0], "[src] [DBG] moved"));
}
