// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include "helpers.h"
#include "spdlite/logger.h"
#include "spdlite/sinks/null_sink.h"

using spdlite::level;

TEST_CASE("level enum numeric values are stable") {
    CHECK(static_cast<int>(level::trace) == 0);
    CHECK(static_cast<int>(level::debug) == 1);
    CHECK(static_cast<int>(level::info) == 2);
    CHECK(static_cast<int>(level::warn) == 3);
    CHECK(static_cast<int>(level::err) == 4);
    CHECK(static_cast<int>(level::critical) == 5);
    CHECK(static_cast<int>(level::off) == 6);
    CHECK(spdlite::levels_count == 7);
}

TEST_CASE("level_names map to expected 3-char tags") {
    CHECK(spdlite::to_string_view(level::trace) == "TRC");
    CHECK(spdlite::to_string_view(level::debug) == "DBG");
    CHECK(spdlite::to_string_view(level::info) == "INF");
    CHECK(spdlite::to_string_view(level::warn) == "WRN");
    CHECK(spdlite::to_string_view(level::err) == "ERR");
    CHECK(spdlite::to_string_view(level::critical) == "CRT");
    CHECK(spdlite::to_string_view(level::off) == "OFF");
}

TEST_CASE("should_log filters by current log_level") {
    spdlite::logger_st<spdlite::null_sink> log{spdlite::null_sink{}};

    log.set_log_level(level::warn);
    CHECK_FALSE(log.should_log(level::trace));
    CHECK_FALSE(log.should_log(level::debug));
    CHECK_FALSE(log.should_log(level::info));
    CHECK(log.should_log(level::warn));
    CHECK(log.should_log(level::err));
    CHECK(log.should_log(level::critical));

    log.set_log_level(level::trace);
    CHECK(log.should_log(level::trace));
    CHECK(log.should_log(level::critical));

    log.set_log_level(level::off);
    CHECK_FALSE(log.should_log(level::trace));
    CHECK_FALSE(log.should_log(level::critical));
}

TEST_CASE("level filtering suppresses messages at the sink") {
    helpers::capture_sink cap;
    spdlite::logger_st<helpers::capture_sink> log{cap};
    log.set_log_level(level::warn);

    log.trace("nope");
    log.debug("nope");
    log.info("nope");
    log.warn("yes");
    log.error("yes");
    log.critical("yes");

    CHECK(cap.state->payloads.size() == 3);
    CHECK(cap.state->payloads[0] == "yes");
    CHECK(cap.state->levels[0] == level::warn);
    CHECK(cap.state->levels[1] == level::err);
    CHECK(cap.state->levels[2] == level::critical);
}

TEST_CASE("default log level is info") {
    spdlite::logger_st<spdlite::null_sink> log{spdlite::null_sink{}};
    CHECK(log.get_log_level() == level::info);
    CHECK_FALSE(log.should_log(level::trace));
    CHECK_FALSE(log.should_log(level::debug));
    CHECK(log.should_log(level::info));
}
