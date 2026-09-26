// SPDX-License-Identifier: MIT
// Copyright (c) 2026, Gabi Melman

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include "../common.h"

namespace spdlite {

// File sink with a max_size cap. Active file is always at base_filename;
// archives use a monotonic counter that resumes across restarts.
// On rotation:
// rename active -> base.<N>.ext, open fresh active, drop the archive that
// fell out of the window. One rename + at most one delete per rotation.
//
// max_files = 0 disables archives (truncate-only).
//
// Example: base="logs/app.txt", max_files=3, after 10 rotations:
//   logs/app.txt   app.10.txt        app.9.txt   app.8.txt
//   (active)       (newest archive)              (oldest kept)
struct rotating_file_sink {
    static constexpr std::size_t max_files_limit = 1000;

    rotating_file_sink(std::filesystem::path base_filename, std::size_t max_size, std::size_t max_files = 1);

    void write(const log_msg& msg);

    void flush() const {
        if (file_) std::fflush(file_.get());
    }

private:
    std::filesystem::path base_filename_;
    std::size_t max_size_;
    std::size_t max_files_;
    std::size_t current_size_{0};
    std::size_t next_index_{1};  // index assigned to the next archive
    std::unique_ptr<std::FILE, detail::file_closer> file_;

    [[nodiscard]] std::filesystem::path archive_path_(std::size_t n) const;
    void open_(bool truncate);
    void rotate_();
    static bool try_rename_(const std::filesystem::path& src, const std::filesystem::path& dst);
    [[nodiscard]] std::size_t scan_and_prune_archives_() const;
    static std::optional<std::size_t> parse_archive_index_(const std::filesystem::path& filepath,
                                                           const std::filesystem::path& base_stem,
                                                           const std::filesystem::path& base_ext);
};

inline rotating_file_sink::rotating_file_sink(std::filesystem::path base_filename, std::size_t max_size, std::size_t max_files)
    : base_filename_(std::move(base_filename)),
      max_size_(max_size),
      max_files_(max_files) {
    if (base_filename_.empty()) {
        throw std::invalid_argument("rotating_file_sink: base_filename must not be empty");
    }
    if (max_size == 0) {
        throw std::invalid_argument("rotating_file_sink: max_size must be > 0");
    }
    if (max_files > max_files_limit) {
        throw std::invalid_argument("rotating_file_sink: max_files exceeds max_files_limit (1000)");
    }
    const auto parent = base_filename_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    // resume the counter from the highest existing archive, and clean up
    // any orphans below the [highest - max_files + 1, highest] window
    next_index_ = scan_and_prune_archives_() + 1;

    // pick up existing active size so cross-restart logs are preserved
    std::error_code ec;
    const auto sz = std::filesystem::file_size(base_filename_, ec);
    if (!ec) {
        current_size_ = static_cast<std::size_t>(sz);
    }

    open_(false);
}

inline void rotating_file_sink::write(const log_msg& msg) {
    if (!file_) return;  // degraded state after a failed rotation - drop silently
    const auto bytes = msg.formatted.size();
    auto new_size = current_size_ + bytes;
    if (new_size > max_size_ && current_size_ > 0) {
        std::fflush(file_.get());
        rotate_();
        if (!file_) return;  // rotation fallback failed too
        new_size = bytes;
    }
    if (detail::fwrite_bytes(msg.formatted.data(), bytes, file_.get())) {
        current_size_ = new_size;
    }
}

// archive_path_(5) for "logs/app.txt" -> "logs/app.5.txt".
inline std::filesystem::path rotating_file_sink::archive_path_(std::size_t n) const {
    auto stem = base_filename_.stem().string();
    auto ext = base_filename_.extension().string();
    auto dir = base_filename_.parent_path();
    auto name = stem + "." + std::to_string(n) + ext;
    return dir.empty() ? std::filesystem::path(name) : dir / name;
}

inline void rotating_file_sink::open_(bool truncate) {
#ifdef _WIN32
    std::FILE* fp = _wfopen(base_filename_.c_str(), truncate ? L"wb" : L"ab");
#else
    std::FILE* fp = std::fopen(base_filename_.c_str(), truncate ? "wb" : "ab");
#endif
    if (!fp) {
        throw std::runtime_error("spdlite: failed to open file: " + base_filename_.string());
    }
    file_.reset(fp);
}

// close active, rename active -> archive(next_index_), drop the archive
// that just rolled out of the window, open fresh active.
// on rename failure fall back to truncating in place to bound file size.
inline void rotating_file_sink::rotate_() {
    file_.reset();  // close
    const auto archive = archive_path_(next_index_);
    if (!try_rename_(base_filename_, archive)) {
        // give up on rotation; just truncate active in place to bound size
        open_(true);
        current_size_ = 0;
        return;
    }
    ++next_index_;
    // delete the archive that just left the window (best-effort)
    if (next_index_ > max_files_ + 1) {
        std::error_code ec;
        std::filesystem::remove(archive_path_(next_index_ - max_files_ - 1), ec);
    }
    open_(false);  // base no longer exists, "ab" creates fresh
    current_size_ = 0;
}

// rename src->dst with retries after a short sleep. workaround for the
// windows issue where antivirus / indexer can briefly hold the file and
// cause rename to fail with permission denied at high rotation rates.
inline bool rotating_file_sink::try_rename_(const std::filesystem::path& src, const std::filesystem::path& dst) {
    constexpr int max_attempts = 5;
    constexpr auto retry_delay = std::chrono::milliseconds(20);
    std::error_code ec;
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        std::filesystem::remove(dst, ec);  // best-effort
        std::filesystem::rename(src, dst, ec);
        if (!ec) return true;
        std::this_thread::sleep_for(retry_delay);
    }
    return false;
}

// walk the parent directory; return the highest archive index found, and
// delete any files below the [highest - max_files + 1, highest]
inline std::size_t rotating_file_sink::scan_and_prune_archives_() const {
    auto parent = base_filename_.parent_path();
    if (parent.empty()) parent = ".";
    std::error_code ec;
    if (!std::filesystem::exists(parent, ec)) return 0;
    const auto stem = base_filename_.stem();
    const auto ext = base_filename_.extension();

    // first pass: find the highest index
    std::size_t highest = 0;
    for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
        if (ec) break;
        std::error_code stat_ec;
        if (entry.is_symlink(stat_ec)) continue;  // never follow planted symlinks
        if (!entry.is_regular_file(stat_ec)) continue;
        const auto n = parse_archive_index_(entry.path().filename(), stem, ext);
        if (n && *n > highest) highest = *n;
    }
    if (highest == 0) return 0;  // no archives found, nothing to prune

    // second pass: delete anything below the keep window (best-effort)
    if (highest > max_files_) {
        const std::size_t lowest_keep = highest - max_files_ + 1;
        for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
            if (ec) break;
            std::error_code stat_ec;
            if (entry.is_symlink(stat_ec)) continue;
            if (!entry.is_regular_file(stat_ec)) continue;
            const auto n = parse_archive_index_(entry.path().filename(), stem, ext);
            if (n && *n < lowest_keep) std::filesystem::remove(entry.path(), ec);
        }
    }
    return highest;
}

// matches `<base_stem>.<digits><base_ext>`; returns the index if so.
inline std::optional<std::size_t> rotating_file_sink::parse_archive_index_(const std::filesystem::path& filepath,
                                                                           const std::filesystem::path& base_stem,
                                                                           const std::filesystem::path& base_ext) {
    // if base has an extension, file's must match exactly. if base has no extension,
    // the file's ".<digits>" tail is its only "extension" and is parsed below.
    if (!base_ext.empty() && filepath.extension() != base_ext) return std::nullopt;

    // "app.5.txt"  -> inner is "app.5";
    // "app.5" with -> inner is "app.5" too. inner's stem must equal base_stem;
    // inner's extension is the ".<digits>" tail.
    const auto& inner = base_ext.empty() ? filepath : filepath.stem();
    if (inner.stem() != base_stem) return std::nullopt;
    const auto& dot_digits = inner.extension();        // prvalue path, lifetime-extended
    const auto& dot_digits_str = dot_digits.native();  // const string_type& - no allocation
    if (dot_digits_str.size() < 2 || dot_digits_str[0] != '.') return std::nullopt;

    std::size_t value = 0;
    constexpr auto max_idx = std::numeric_limits<std::size_t>::max();
    for (std::size_t i = 1; i < dot_digits_str.size(); ++i) {
        const auto c = dot_digits_str[i];
        if (c < '0' || c > '9') return std::nullopt;
        const auto digit = static_cast<std::size_t>(c - '0');
        if (value > (max_idx - digit) / 10) return std::nullopt;  // overflow guard
        value = value * 10 + digit;
    }
    // we never produce <stem>.0<ext>, and SIZE_MAX would overflow next_index_
    if (value == 0 || value == max_idx) return std::nullopt;
    return value;
}

}  // namespace spdlite
