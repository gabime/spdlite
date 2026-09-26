// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "helpers.h"
#include "spdlite/logger.h"

using namespace spdlite;
using helpers::capture_sink;

TEST_CASE("logger: N threads x M messages all reach the sink, no torn lines") {
    constexpr int n_threads = 8;
    constexpr int per_thread = 500;

    capture_sink cap;
    logger_mt<capture_sink> log{"mt", cap};

    std::vector<std::thread> threads;
    threads.reserve(n_threads);
    for (int t = 0; t < n_threads; ++t) {
        threads.emplace_back([&log, t] {
            for (int i = 0; i < per_thread; ++i) {
                log.info("t={} i={}", t, i);
            }
        });
    }
    for (auto& th : threads) th.join();

    // (1) every message reached the sink
    REQUIRE(cap.state->payloads.size() == static_cast<std::size_t>(n_threads * per_thread));

    // (2) no torn lines: every captured payload is one of the strings a single
    // thread could have produced. Concurrent formats interleaving args would
    // produce strings not in this set.
    std::set<std::string> expected;
    for (int t = 0; t < n_threads; ++t)
        for (int i = 0; i < per_thread; ++i) expected.insert("t=" + std::to_string(t) + " i=" + std::to_string(i));

    CHECK(std::all_of(cap.state->payloads.begin(), cap.state->payloads.end(),
                      [&](const std::string& p) { return expected.count(p) != 0; }));
}

TEST_CASE("logger: concurrent log + flush is safe") {
    constexpr int n_writers = 4;
    constexpr int per_writer = 200;

    capture_sink cap;
    logger_mt<capture_sink> log{"mt", cap};

    std::atomic<bool> done{false};
    std::thread flusher([&] {
        while (!done.load()) {
            log.flush();
            std::this_thread::yield();
        }
    });

    std::vector<std::thread> writers;
    writers.reserve(n_writers);
    for (int t = 0; t < n_writers; ++t) {
        writers.emplace_back([&log, t] {
            for (int i = 0; i < per_writer; ++i) {
                log.info("t={} i={}", t, i);
            }
        });
    }
    for (auto& w : writers) w.join();
    done.store(true);
    flusher.join();

    CHECK(cap.state->payloads.size() == static_cast<std::size_t>(n_writers * per_writer));
    CHECK(cap.state->flush_count > 0);
}
