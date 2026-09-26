// SPDX-License-Identifier: MIT
//
// Shared test helpers: substring check, in-memory capture sink, per-test temp dir.

#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "spdlite/common.h"

namespace helpers {

inline bool contains(std::string_view a, std::string_view b) { return a.find(b) != std::string_view::npos; }

// In-memory sink used by tests. Records every write and counts flushes.
// State lives behind a shared_ptr so copies of the sink (the logger stores
// sinks by value in a tuple) observe the same state.
struct capture_sink {
    struct state_t {
        std::mutex mu;
        std::vector<std::string> formatted;
        std::vector<std::string> payloads;
        std::vector<std::string> tags;
        std::vector<spdlite::level> levels;
        std::vector<std::size_t> level_offsets;
        std::size_t flush_count{0};
        std::atomic<bool> fail_writes{false};
    };

    capture_sink()
        : state(std::make_shared<state_t>()) {}

    // When true, every subsequent write() call throws unconditionally.
    void fail_writes(bool v) const { state->fail_writes.store(v, std::memory_order_relaxed); }

    void write(const spdlite::log_msg& msg) const {
        if (state->fail_writes.load(std::memory_order_relaxed)) {
            throw std::runtime_error("capture_sink: induced write failure");
        }
        std::lock_guard<std::mutex> lock(state->mu);
        state->formatted.emplace_back(msg.formatted);
        state->payloads.emplace_back(msg.payload);
        state->tags.emplace_back(msg.tag);
        state->levels.push_back(msg.log_level);
        state->level_offsets.push_back(msg.level_offset);
    }

    void flush() const {
        std::lock_guard<std::mutex> lock(state->mu);
        ++state->flush_count;
    }

    std::shared_ptr<state_t> state;
};

// Per-test temp directory under <system_tmp>/spdlite_tests/<unique>/.
// Sample folder: /tmp/spdlite_tests/rot_basic_18472938472013_0
// Cleaned up on destruction (best-effort).
class tmpdir {
public:
    explicit tmpdir(std::string_view label) {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() / "spdlite_tests" /
                (std::string{label} + "_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
        std::filesystem::create_directories(path_, ec);
    }

    ~tmpdir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    tmpdir(const tmpdir&) = delete;
    tmpdir& operator=(const tmpdir&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::filesystem::path operator/(std::string_view rel) const { return path_ / rel; }

private:
    std::filesystem::path path_;
};

}  // namespace helpers
