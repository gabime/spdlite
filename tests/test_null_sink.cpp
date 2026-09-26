// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include "spdlite/logger.h"
#include "spdlite/sinks/null_sink.h"

using namespace spdlite;

TEST_CASE("null_sink satisfies the log_sink concept") { CHECK(log_sink<null_sink>); }

TEST_CASE("null_sink absorbs writes and flushes without throwing") {
    logger_st<null_sink> log{null_sink{}};
    log.set_log_level(level::trace);
    CHECK_NOTHROW(log.trace("t {}", 1));
    CHECK_NOTHROW(log.critical("c"));
    CHECK_NOTHROW(log.flush());
}

TEST_CASE("null_sink composes with other null_sinks in a multi-sink logger") {
    logger_st<null_sink, null_sink> log{null_sink{}, null_sink{}};
    CHECK_NOTHROW(log.info("anything"));
}
