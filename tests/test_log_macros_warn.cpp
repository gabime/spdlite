// SPDX-License-Identifier: MIT

// LEVEL_WARN: TRACE/DEBUG/INFO elide; WARN/ERROR/CRITICAL emit. Boundary witness.
#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_WARN  // lazily expanded inside logger.h

#include <doctest/doctest.h>

#include "helpers.h"
#include "spdlite/logger.h"

using namespace spdlite;
using helpers::capture_sink;

TEST_CASE("at LEVEL_WARN, trace/debug/info elide; warn/error/critical emit") {
    int call_count = 0;
    auto bumper = [&] {
        ++call_count;
        return 42;
    };
    capture_sink cap;
    logger_st<capture_sink> log{cap};
    log.set_log_level(level::trace);  // runtime gate wide open - only the compile-time gate filters

    SPDLITE_TRACE(log, "x={}", bumper());     // elided
    SPDLITE_DEBUG(log, "x={}", bumper());     // elided
    SPDLITE_INFO(log, "x={}", bumper());      // elided
    SPDLITE_WARN(log, "x={}", bumper());      // emits
    SPDLITE_ERROR(log, "x={}", bumper());     // emits
    SPDLITE_CRITICAL(log, "x={}", bumper());  // emits

    CHECK(call_count == 3);                  // bumper invoked 3 times, not 6
    CHECK(cap.state->payloads.size() == 3);  // 3 messages reached the sink
    CHECK(cap.state->levels[0] == level::warn);
    CHECK(cap.state->levels[1] == level::err);
    CHECK(cap.state->levels[2] == level::critical);
}
