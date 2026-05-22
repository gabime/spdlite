// SPDX-License-Identifier: MIT
// Copyright (c) 2026, Gabi Melman

#pragma once

#ifdef _MSC_VER
    // fmt checks that source literals are UTF-8 on Windows; enforce UTF-8 source encoding for this header.
    #pragma execution_character_set("utf-8")
#endif

#include <cstdio>
#include <iterator>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>

#include "common.h"
#include "formatter.h"

namespace spdlite {

// Sink concept: write(log_msg) + flush().
template <typename T>
concept log_sink = requires(T & s, const log_msg& m) {
    s.write(m);
    s.flush();
};

// Logger class template. Use the `logger` / `logger_st` aliases below rather than
// instantiating directly. Formats log messages and forwards them to the sinks.
template <typename Mutex, typename... Sinks>
class logger_impl {
public:
    explicit logger_impl(std::string name, Sinks... sinks)
        : name_(std::move(name)),
          formatter_(name_),
          sinks_(std::move(sinks)...) {}

    explicit logger_impl(Sinks... sinks) requires(sizeof...(Sinks) > 0 && (log_sink<Sinks> && ...))
        : formatter_(name_),
          sinks_(std::move(sinks)...) {}

    logger_impl() = default;

    logger_impl(logger_impl&& other) noexcept
        : name_(std::move(other.name_)),
          level_(other.level_.load(std::memory_order_relaxed)),
          flush_level_(other.flush_level_.load(std::memory_order_relaxed)),
          formatter_(std::move(other.formatter_)),
          buf_(std::move(other.buf_)),
          sinks_(std::move(other.sinks_)) {
        other.level_.store(level::off, std::memory_order_relaxed);
    }

    logger_impl& operator=(logger_impl&&) = delete;
    logger_impl(const logger_impl&) = delete;
    logger_impl& operator=(const logger_impl&) = delete;

    template <typename... Args>
    void log(level lvl, format_string_t<Args...> fmt, Args&&... args) const noexcept {
        if (should_log(lvl)) dispatch_fmt_(lvl, fmt, std::forward<Args>(args)...);
    }

    void log(level lvl, std::string_view msg) const noexcept {
        if (should_log(lvl)) log_sv_(lvl, msg);
    }

    // Per-level convenience overloads - forward to log() with a compile-time level.
    template <typename... Args>
    void trace(format_string_t<Args...> fmt, Args&&... args) const noexcept {
        log(level::trace, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void debug(format_string_t<Args...> fmt, Args&&... args) const noexcept {
        log(level::debug, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void info(format_string_t<Args...> fmt, Args&&... args) const noexcept {
        log(level::info, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void warn(format_string_t<Args...> fmt, Args&&... args) const noexcept {
        log(level::warn, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void error(format_string_t<Args...> fmt, Args&&... args) const noexcept {
        log(level::err, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void critical(format_string_t<Args...> fmt, Args&&... args) const noexcept {
        log(level::critical, fmt, std::forward<Args>(args)...);
    }

    // string_view overloads - no formatting, just header + payload
    void trace(std::string_view msg) const noexcept { log(level::trace, msg); }
    void debug(std::string_view msg) const noexcept { log(level::debug, msg); }
    void info(std::string_view msg) const noexcept { log(level::info, msg); }
    void warn(std::string_view msg) const noexcept { log(level::warn, msg); }
    void error(std::string_view msg) const noexcept { log(level::err, msg); }
    void critical(std::string_view msg) const noexcept { log(level::critical, msg); }

    [[nodiscard]] bool should_log(level msg_level) const noexcept { return msg_level >= level_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool should_flush(level msg_level) const noexcept {
        return msg_level >= flush_level_.load(std::memory_order_relaxed);
    }

    void set_log_level(level lvl) noexcept { level_.store(lvl, std::memory_order_relaxed); }
    [[nodiscard]] level get_log_level() const noexcept { return level_.load(std::memory_order_relaxed); }
    void set_flush_level(level lvl) noexcept { flush_level_.store(lvl, std::memory_order_relaxed); }
    [[nodiscard]] level get_flush_level() const noexcept { return flush_level_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::string_view get_name() const noexcept { return name_; }
    void set_name(std::string_view new_name) {
        std::lock_guard<Mutex> lock(mutex_);
        name_.assign(new_name);
        formatter_.set_logger_name(name_);
    }

    // Reconfigure the cached header (UTC, show_date, show_millis). Cheap - one ctor call.
    void set_format_options(format_options opts) {
        std::lock_guard<Mutex> lock(mutex_);
        formatter_ = formatter{name_, opts};
    }

    void flush() const noexcept {
        std::lock_guard<Mutex> lock(mutex_);
        std::apply([](auto&... s) { (s.flush(), ...); }, sinks_);
    }

private:
    std::string name_;
    detail::atomic_level_t level_{level::info};
    detail::atomic_level_t flush_level_{level::off};  // off => never auto-flush
    mutable Mutex mutex_;
    mutable formatter formatter_;
    mutable memory_buf_t buf_;
    mutable std::tuple<Sinks...> sinks_;

    // string_view path - no formatting needed, just header + raw payload
    void log_sv_(level lvl, std::string_view sv) const noexcept {
        try {
            const auto now = log_clock::now();  // timestamp before lock for accuracy
            std::lock_guard<Mutex> lock(mutex_);
            buf_.clear();
            formatter_.format_header(now, lvl, buf_);
            const auto payload_start = buf_.size();
            buf_.append(sv.data(), sv.data() + sv.size());
            const auto payload_end = buf_.size();
#ifdef _WIN32
            buf_.push_back('\r');
#endif
            buf_.push_back('\n');
            std::string_view formatted{buf_.data(), buf_.size()};
            std::string_view payload{buf_.data() + payload_start, payload_end - payload_start};
            log_msg msg(now, name_, lvl, formatted, payload, formatter_.level_offset());
            std::apply([&](auto&... s) { (s.write(msg), ...); }, sinks_);
            if (should_flush(lvl)) std::apply([](auto&... s) { (s.flush(), ...); }, sinks_);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "spdlite: log error: %s\n", ex.what());
        } catch (...) {
            std::fprintf(stderr, "spdlite: unknown log error\n");
        }
    }

    // per-Args trampoline - type-erases args, forwards to log_fmt_args_.
    template <typename... Args>
    void dispatch_fmt_(level lvl, format_string_t<Args...> fmt_str, Args&&... args) const noexcept {
#ifdef SPDLITE_USE_STD_FORMAT
        log_fmt_args_(lvl, fmt_str.get(), std::make_format_args(args...));
#else
        log_fmt_args_(lvl, fmt_str, fmt::make_format_args(args...));
#endif
    }

    // format and send the message to sinks.
    // All formatting + dispatch happens under a single lock, so buf_ is shared state.
    void log_fmt_args_(level lvl, format_string_view_t fmt_str, format_args_t args) const noexcept {
        try {
            const auto now = log_clock::now();  // timestamp before lock for accuracy
            std::lock_guard<Mutex> lock(mutex_);
            buf_.clear();
            formatter_.format_header(now, lvl, buf_);
            const auto payload_start = buf_.size();
#ifdef SPDLITE_USE_STD_FORMAT
            std::vformat_to(std::back_inserter(buf_), fmt_str, args);
#else
            fmt::vformat_to(fmt::appender(buf_), fmt_str, args);
#endif
            const auto payload_end = buf_.size();
#ifdef _WIN32
            buf_.push_back('\r');
#endif
            buf_.push_back('\n');
            std::string_view formatted{buf_.data(), buf_.size()};
            std::string_view payload{buf_.data() + payload_start, payload_end - payload_start};
            log_msg msg(now, name_, lvl, formatted, payload, formatter_.level_offset());
            std::apply([&](auto&... s) { (s.write(msg), ...); }, sinks_);
            if (should_flush(lvl)) std::apply([](auto&... s) { (s.flush(), ...); }, sinks_);
        } catch (const std::exception& ex) {
            std::fprintf(stderr, "spdlite: log error: %s\n", ex.what());
        } catch (...) {
            std::fprintf(stderr, "spdlite: unknown log error\n");
        }
    }
};

// logger: thread-safe (std::mutex). Serializes format + dispatch per log call.
// Default choice - prefer this unless you can prove the logger is never shared
// across threads.
template <typename... Sinks>
using logger = logger_impl<std::mutex, Sinks...>;

// logger_st: single-threaded (null_mutex). Zero locking overhead.
template <typename... Sinks>
using logger_st = logger_impl<detail::null_mutex, Sinks...>;

}  // namespace spdlite

// Compile-time level gate. Define SPDLITE_ACTIVE_LEVEL before including to strip
// lower levels. Values match enum class level in common.h.

#define SPDLITE_LEVEL_TRACE 0
#define SPDLITE_LEVEL_DEBUG 1
#define SPDLITE_LEVEL_INFO 2
#define SPDLITE_LEVEL_WARN 3
#define SPDLITE_LEVEL_ERROR 4
#define SPDLITE_LEVEL_CRITICAL 5
#define SPDLITE_LEVEL_OFF 6  // sentinel: silences every macro

#ifndef SPDLITE_ACTIVE_LEVEL
    #define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_TRACE
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_TRACE
    #define SPDLITE_TRACE(logger, ...) (logger).trace(__VA_ARGS__)
#else
    #define SPDLITE_TRACE(logger, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_DEBUG
    #define SPDLITE_DEBUG(logger, ...) (logger).debug(__VA_ARGS__)
#else
    #define SPDLITE_DEBUG(logger, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_INFO
    #define SPDLITE_INFO(logger, ...) (logger).info(__VA_ARGS__)
#else
    #define SPDLITE_INFO(logger, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_WARN
    #define SPDLITE_WARN(logger, ...) (logger).warn(__VA_ARGS__)
#else
    #define SPDLITE_WARN(logger, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_ERROR
    #define SPDLITE_ERROR(logger, ...) (logger).error(__VA_ARGS__)
#else
    #define SPDLITE_ERROR(logger, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_CRITICAL
    #define SPDLITE_CRITICAL(logger, ...) (logger).critical(__VA_ARGS__)
#else
    #define SPDLITE_CRITICAL(logger, ...) (void)0
#endif
