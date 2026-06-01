// SPDX-License-Identifier: MIT
// Copyright (c) 2026, Gabi Melman

#pragma once

#include <chrono>
#include <cstdint>
#include <ctime>
#include <string>

// Platform thread-ID source for the optional [tid] header field.
#if defined(__linux__)
    #include <unistd.h>
    #include <sys/syscall.h>
#elif defined(__APPLE__)
    #include <pthread.h>
#elif defined(_WIN32)
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentThreadId(void);
#else
    #include <functional>
    #include <thread>
#endif

#include "common.h"

namespace spdlite::detail {

// Cached per-thread ID, already capped to 6 decimal digits to match the fixed
// header field width. Hot-path cost: one thread_local load.
inline std::uint32_t this_thread_id_log() noexcept {
    thread_local const std::uint32_t cached = []() noexcept -> std::uint32_t {
        std::uint64_t raw = 0;
#if defined(__linux__)
        raw = static_cast<std::uint64_t>(::syscall(SYS_gettid));
#elif defined(__APPLE__)
        ::pthread_threadid_np(nullptr, &raw);
#elif defined(_WIN32)
        raw = static_cast<std::uint64_t>(::GetCurrentThreadId());
#else
        raw = static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
        return static_cast<std::uint32_t>(raw % 1'000'000ULL);
    }();
    return cached;
}

inline void put2(char* dst, int n) {
    dst[0] = static_cast<char>('0' + n / 10);
    dst[1] = static_cast<char>('0' + n % 10);
}

inline void put3(char* dst, int n) {
    dst[0] = static_cast<char>('0' + n / 100);
    dst[1] = static_cast<char>('0' + (n / 10) % 10);
    dst[2] = static_cast<char>('0' + n % 10);
}

inline void put4(char* dst, int n) {
    dst[0] = static_cast<char>('0' + n / 1000);
    dst[1] = static_cast<char>('0' + (n / 100) % 10);
    dst[2] = static_cast<char>('0' + (n / 10) % 10);
    dst[3] = static_cast<char>('0' + n % 10);
}

// Write a 6-digit zero-padded number (max 999,999) as two 3-digit halves.
inline void put6(char* dst, std::uint64_t n) {
    put3(dst, static_cast<int>(n / 1000));
    put3(dst + 3, static_cast<int>(n % 1000));
}

// Write a 9-digit zero-padded number (max 999,999,999) as three 3-digit segments.
inline void put9(char* dst, std::uint64_t n) {
    put3(dst, static_cast<int>(n / 1'000'000));
    put3(dst + 3, static_cast<int>((n / 1000) % 1000));
    put3(dst + 6, static_cast<int>(n % 1000));
}

}  // namespace spdlite::detail

namespace spdlite {

// Fractional-second resolution for the timestamp. none = no ".xxx" suffix at all.
enum class time_precision { none, ms, us, ns };

// Composable formatting knobs. Defaults reproduce the original fixed shape.
struct format_options {
    bool utc = false;                               // gmtime instead of localtime
    bool show_date = true;                          // include "YYYY-MM-DD " prefix
    bool show_thread_id = false;                    // include "[tid] " after the timestamp
    time_precision precision = time_precision::ms;  // .mmm (default), .uuuuuu, .nnnnnnnnn, or none
};

// Default shape: [YYYY-MM-DD HH:MM:SS.mmm] [name] [LVL] payload\n
// Layout flexes with format_options - the cached header is rebuilt on options change,
// so the hot path stays "patch a few bytes + memcpy" regardless of layout.
struct formatter {
    explicit formatter(std::string_view logger_name = {}, format_options opts = {})
        : opts_(opts) {
        rebuild_header(logger_name);
    }

    void set_logger_name(std::string_view name) { rebuild_header(name); }

    // append the cached header to dest. Patches the fractional digits (if any) and
    // level per call; rebuilds the date/time digits only when the second changes.
    void format_header(log_clock::time_point time, level lvl, memory_buf_t& dest) {
        using namespace std::chrono;

        const auto time_since_epoch = time.time_since_epoch();
        const auto secs = duration_cast<seconds>(time_since_epoch);

        if (secs != last_secs_) {
            last_secs_ = secs;
            rebuild_timestamp(static_cast<std::time_t>(secs.count()));
        }

        if (opts_.precision != time_precision::none) {
            const auto frac_ns = (duration_cast<nanoseconds>(time_since_epoch) - duration_cast<nanoseconds>(secs)).count();
            switch (opts_.precision) {
                case time_precision::ms:
                    detail::put3(header_.data() + frac_offset_, static_cast<int>(frac_ns / 1'000'000));
                    break;
                case time_precision::us:
                    detail::put6(header_.data() + frac_offset_, static_cast<std::uint64_t>(frac_ns / 1'000));
                    break;
                case time_precision::ns:
                    detail::put9(header_.data() + frac_offset_, static_cast<std::uint64_t>(frac_ns));
                    break;
                case time_precision::none:
                    break;
            }
        }
        if (opts_.show_thread_id) {
            detail::put6(header_.data() + tid_offset_, detail::this_thread_id_log());
        }
        std::memcpy(header_.data() + level_offset_, to_string_view(lvl).data(), level_width);
        dest.append(header_.data(), header_.data() + header_.size());
    }

    // Offset where the level starts in formatted output (for color sinks)
    [[nodiscard]] std::size_t level_offset() const noexcept { return level_offset_; }

private:
    static constexpr int frac_width(time_precision p) noexcept {
        switch (p) {
            case time_precision::ms:
                return 3;
            case time_precision::us:
                return 6;
            case time_precision::ns:
                return 9;
            default:
                return 0;
        }
    }

    format_options opts_;
    std::string header_;
    std::size_t frac_offset_{};
    std::size_t tid_offset_{};
    std::size_t level_offset_{};
    std::chrono::seconds last_secs_{};

    void rebuild_header(std::string_view logger_name) {
        header_.clear();
        header_ = opts_.show_date ? "[0000-00-00 00:00:00" : "[00:00:00";
        if (opts_.precision != time_precision::none) {
            frac_offset_ = header_.size() + 1;  // skip the '.'
            header_.push_back('.');
            header_.append(frac_width(opts_.precision), '0');
        }
        header_.append("] ");
        if (opts_.show_thread_id) {
            header_.push_back('[');
            tid_offset_ = header_.size();
            header_.append("000000] ");  // 6-digit zero-padded thread id, patched per call
        }
        if (!logger_name.empty()) {
            header_.push_back('[');
            header_.append(logger_name);
            header_.append("] ");
        }
        header_.push_back('[');
        level_offset_ = header_.size();
        header_.append("INF] ");  // placeholder level (level_width chars + "] ")
        last_secs_ = {};          // force timestamp rebuild on next format
    }

    void rebuild_timestamp(std::time_t time_t_val) {
        std::tm tm{};
#ifdef _WIN32
        if (opts_.utc)
            gmtime_s(&tm, &time_t_val);
        else
            localtime_s(&tm, &time_t_val);
#else
        if (opts_.utc)
            gmtime_r(&time_t_val, &tm);
        else
            localtime_r(&time_t_val, &tm);
#endif
        std::size_t off = 1;  // skip leading '['
        if (opts_.show_date) {
            detail::put4(header_.data() + off, tm.tm_year + 1900);
            detail::put2(header_.data() + off + 5, tm.tm_mon + 1);
            detail::put2(header_.data() + off + 8, tm.tm_mday);
            off += 11;  // skip past "YYYY-MM-DD "
        }
        detail::put2(header_.data() + off, tm.tm_hour);
        detail::put2(header_.data() + off + 3, tm.tm_min);
        detail::put2(header_.data() + off + 6, tm.tm_sec);
        // punctuation ('-', ':', '.', ']', ' ', '[') is invariant; set once by rebuild_header
    }
};

}  // namespace spdlite
