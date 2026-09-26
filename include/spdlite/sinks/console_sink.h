// SPDX-License-Identifier: MIT
// Copyright (c) 2026, Gabi Melman

// console_sink: colored stdout/stderr output.
// Win32 SetConsoleTextAttribute on Windows, ANSI escape codes elsewhere.

#pragma once

#include <array>

#include "../common.h"

namespace spdlite {

// Whether the color sink emits color codes or attributes.
//   automatic - colors when the destination is a terminal, plain otherwise (default)
//   always    - always emit colors, even when output is redirected to a file
//   never     - never emit colors
enum class color_mode { automatic, always, never };

}  // namespace spdlite

#ifdef _WIN32

// ==================== Windows: native console API ====================

    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>

namespace spdlite::detail {

// uses SetConsoleTextAttribute + WriteConsoleA for colored output.
// falls back to WriteFile when output is redirected to a file or color_mode is off.
struct console_sink_base {
    explicit console_sink_base(HANDLE handle, color_mode mode = color_mode::automatic);

    void set_color_mode(color_mode mode) noexcept {
        mode_ = mode;
        update_should_color_();
    }

    void write(const log_msg& msg);

    void flush() {}  // windows console is unbuffered

    void set_color(level lvl, WORD color) { colors_[static_cast<std::size_t>(lvl)] = color; }

private:
    HANDLE handle_;
    color_mode mode_;
    WORD orig_attribs_{FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE};
    bool is_console_{false};
    bool should_color_{false};
    std::array<WORD, levels_count> colors_{};

    void update_should_color_() noexcept {
        switch (mode_) {
            case color_mode::always:
                should_color_ = true;
                break;
            case color_mode::never:
                should_color_ = false;
                break;
            case color_mode::automatic:
                should_color_ = is_console_;
                break;
        }
    }

    void write_console_(const char* data, std::size_t size) {
        if (size > 0) {
            DWORD written = 0;
            ::WriteConsoleA(handle_, data, static_cast<DWORD>(size), &written, nullptr);
        }
    }
};

inline console_sink_base::console_sink_base(HANDLE handle, color_mode mode)
    : handle_(handle),
      mode_(mode) {
    // detect if we're writing to a real console
    DWORD console_mode = 0;
    is_console_ = ::GetConsoleMode(handle_, &console_mode) != 0;

    // save original attributes for reset
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (is_console_ && ::GetConsoleScreenBufferInfo(handle_, &info)) {
        orig_attribs_ = info.wAttributes;
    }

    colors_[static_cast<std::size_t>(level::trace)] = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
    colors_[static_cast<std::size_t>(level::debug)] = FOREGROUND_GREEN | FOREGROUND_BLUE;
    colors_[static_cast<std::size_t>(level::info)] = FOREGROUND_GREEN;
    colors_[static_cast<std::size_t>(level::warn)] = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    colors_[static_cast<std::size_t>(level::err)] = FOREGROUND_RED | FOREGROUND_INTENSITY;
    colors_[static_cast<std::size_t>(level::critical)] =
        BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    colors_[static_cast<std::size_t>(level::off)] = 0;

    update_should_color_();
}

inline void console_sink_base::write(const log_msg& msg) {
    if (handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE) return;

    const char* data = msg.formatted.data();
    auto size = msg.formatted.size();

    // plain path - either color_mode::never, or automatic+redirected
    if (!should_color_) {
        if (is_console_)
            write_console_(data, size);
        else {
            DWORD written = 0;
            ::WriteFile(handle_, data, static_cast<DWORD>(size), &written, nullptr);
        }
        return;
    }

    // SetConsoleTextAttribute only works against a real console; if the user
    // forced color_mode::always and output is redirected, drop back to plain.
    if (!is_console_) {
        DWORD written = 0;
        ::WriteFile(handle_, data, static_cast<DWORD>(size), &written, nullptr);
        return;
    }

    const auto level_start = msg.level_offset;
    const auto level_end = level_start + level_width;

    // before level label
    write_console_(data, level_start);
    // colored level label
    ::SetConsoleTextAttribute(handle_, colors_[static_cast<std::size_t>(msg.log_level)]);
    write_console_(data + level_start, level_width);
    // reset and remainder
    ::SetConsoleTextAttribute(handle_, orig_attribs_);
    write_console_(data + level_end, size - level_end);
}

}  // namespace spdlite::detail

namespace spdlite {

struct console_sink : detail::console_sink_base {
    explicit console_sink(color_mode mode = color_mode::automatic)
        : console_sink_base(::GetStdHandle(STD_OUTPUT_HANDLE), mode) {}
};

struct console_err_sink : detail::console_sink_base {
    explicit console_err_sink(color_mode mode = color_mode::automatic)
        : console_sink_base(::GetStdHandle(STD_ERROR_HANDLE), mode) {}
};

}  // namespace spdlite

#else

// ==================== Linux/macOS: ANSI escape codes ====================

    #include <unistd.h>  // for isatty / fileno

    #include <cstdio>
    #include <string_view>

namespace spdlite::detail {

namespace ansi_color {
constexpr std::string_view reset = "\033[m";
constexpr std::string_view white = "\033[37m";
constexpr std::string_view cyan = "\033[36m";
constexpr std::string_view green = "\033[32m";
constexpr std::string_view yellow_bold = "\033[33m\033[1m";
constexpr std::string_view red_bold = "\033[31m\033[1m";
constexpr std::string_view bold_on_red = "\033[1m\033[41m";
}  // namespace ansi_color

// wraps ANSI escape codes around the level label (level_width chars) in the formatted output.
// rebuilds the line into cbuf_ with: [prefix][color][LVL][reset][rest].
struct console_sink_base {
    explicit console_sink_base(std::FILE* file, color_mode mode = color_mode::automatic);

    void set_color_mode(color_mode mode) noexcept {
        mode_ = mode;
        update_should_color_();
    }

    void write(const log_msg& msg);

    void flush() const { std::fflush(file_); }

    void set_color(level lvl, std::string_view color) { colors_[static_cast<std::size_t>(lvl)] = color; }

private:
    std::FILE* file_;
    color_mode mode_;
    bool is_tty_{false};
    bool should_color_{false};
    memory_buf_t cbuf_;
    std::array<std::string_view, levels_count> colors_{};

    void update_should_color_() noexcept {
        switch (mode_) {
            case color_mode::always:
                should_color_ = true;
                break;
            case color_mode::never:
                should_color_ = false;
                break;
            case color_mode::automatic:
                should_color_ = is_tty_;
                break;
        }
    }
};

inline console_sink_base::console_sink_base(std::FILE* file, color_mode mode)
    : file_(file),
      mode_(mode) {
    colors_[static_cast<std::size_t>(level::trace)] = ansi_color::white;
    colors_[static_cast<std::size_t>(level::debug)] = ansi_color::cyan;
    colors_[static_cast<std::size_t>(level::info)] = ansi_color::green;
    colors_[static_cast<std::size_t>(level::warn)] = ansi_color::yellow_bold;
    colors_[static_cast<std::size_t>(level::err)] = ansi_color::red_bold;
    colors_[static_cast<std::size_t>(level::critical)] = ansi_color::bold_on_red;
    colors_[static_cast<std::size_t>(level::off)] = ansi_color::reset;

    is_tty_ = ::isatty(::fileno(file_)) != 0;
    update_should_color_();
}

inline void console_sink_base::write(const log_msg& msg) {
    const char* data = msg.formatted.data();
    auto size = msg.formatted.size();

    if (!should_color_) {
        detail::fwrite_bytes(data, size, file_);
        return;
    }

    auto color = colors_[static_cast<std::size_t>(msg.log_level)];
    const auto level_start = msg.level_offset;
    const auto level_end = level_start + level_width;

    cbuf_.clear();
    cbuf_.append(data, data + level_start);
    cbuf_.append(color.data(), color.data() + color.size());
    cbuf_.append(data + level_start, data + level_end);
    cbuf_.append(ansi_color::reset.data(), ansi_color::reset.data() + ansi_color::reset.size());
    cbuf_.append(data + level_end, data + size);
    detail::fwrite_bytes(cbuf_.data(), cbuf_.size(), file_);
}

}  // namespace spdlite::detail

namespace spdlite {

struct console_sink : detail::console_sink_base {
    explicit console_sink(color_mode mode = color_mode::automatic)
        : console_sink_base(stdout, mode) {}
};

struct console_err_sink : detail::console_sink_base {
    explicit console_err_sink(color_mode mode = color_mode::automatic)
        : console_sink_base(stderr, mode) {}
};

}  // namespace spdlite

#endif
