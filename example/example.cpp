// SPDX-License-Identifier: MIT
// Copyright (c) 2026, Gabi Melman

#include "spdlite/logger.h"
#include "spdlite/sinks/console_sink.h"
#include "spdlite/sinks/file_sink.h"
#include "spdlite/sinks/rotating_file_sink.h"
#include "spdlite/sinks/shared_sink.h"

void banner();
void log_levels();
void format_options_example();
void file_sink_example();
void rotating_file_sink_example();
void multi_sink_example();
void shared_file_sink_example();
void compile_time_gating_example();
void set_logger(spdlite::logger<spdlite::console_sink>* logger);

int main() {
    banner();
    log_levels();
    format_options_example();
    file_sink_example();
    rotating_file_sink_example();
    multi_sink_example();
    shared_file_sink_example();
    compile_time_gating_example();
    return 0;
}

// Print an ASCII banner with the spdlite version through a colored console logger.
// `logger` is the thread-safe default (uses std::mutex). Use `logger_st` instead if
// the logger is provably single-threaded - it skips locking entirely.
void banner() {
    using namespace spdlite;
    logger<console_sink> console(console_sink{});
    console.info(R"(
                ____ ___ __
   _________  ____/ / (.) /____
  / ___/ __ \/ __  / / / __/ _ \
 (__  ) /_/ / /_/ / / / /_/  __/
/____/ .___/\__,_/_/_/\__/\___/   v{}.{}.{}
    /_/
)",
                 SPDLITE_VER_MAJOR, SPDLITE_VER_MINOR, SPDLITE_VER_PATCH);
}

// Walk all log levels through a colored console logger.
// By default, the threshold is info; enable trace explicitly to see everything.
void log_levels() {
    using namespace spdlite;
    logger<console_sink> console(console_sink{});
    console.set_log_level(level::trace);

    console.trace("This is a {} message", "trace");
    console.debug("This is a {} message", "debug");
    console.info("This is a {} message", "info");
    console.warn("This is a {} message", "warning");
    console.error("This is a {} message", "error");
    console.critical("This is a {} message", "critical");
}

// Reconfigure the header layout at runtime via format_options.
// Default shape: [YYYY-MM-DD HH:MM:SS.mmm] [name] [LVL] payload
void format_options_example() {
    using namespace spdlite;
    logger<console_sink> log(console_sink{});

    log.set_format_options({.utc = true});
    log.set_format_options({.show_date = false});
    log.set_format_options({.precision = time_precision::ns});
    log.set_format_options({.precision = time_precision::none});
    log.set_format_options({.show_thread_id = true});
    log.set_format_options({.show_date = false, .precision = time_precision::none});
}

// Log to a file via file_sink. The sink creates parent directories
// automatically and uses _wfopen on Windows for Unicode paths.
void file_sink_example() {
    using namespace spdlite;
    logger<file_sink> file_logger("my_logger", file_sink{"logs/example.txt", open_mode::truncate});
    file_logger.info("This message is written to logs/example.txt");
}

// Cap a log file at max_size and keep up to max_files rotated backups.
// Default max_files=1 gives single rotation: rot.txt + rot.1.txt.
void rotating_file_sink_example() {
    using namespace spdlite;
    constexpr std::size_t max_size = 256;
    constexpr std::size_t max_files = 3;
    logger<rotating_file_sink> rot(rotating_file_sink{"logs/rot.txt", max_size, max_files});
    for (int i = 0; i < 2000; ++i) {
        rot.info("rotating message #{:03}", i);
    }
}

// Compose multiple sinks into one logger - a single log call writes to all of them.
void multi_sink_example() {
    using namespace spdlite;
    logger<console_sink, file_sink> multi(console_sink{}, file_sink{"logs/multi.txt", open_mode::truncate});
    multi.info("This goes to both console and file");
}

// Multiple named loggers writing to one shared file via shared_sink.
// Each line is tagged with the logger's name, so subsystems can be
// distinguished by grep on a single output file.
void shared_file_sink_example() {
    using namespace spdlite;
    auto file = std::make_shared<file_sink>("logs/shared.txt", open_mode::truncate);
    shared_sink wrapped(file);
    logger<shared_sink<file_sink>> network("network", wrapped);
    logger<shared_sink<file_sink>> auth("auth", wrapped);

    network.info("connection established");
    auth.warn("invalid token");
}

// Compile-time level gating. Build with -DSPDLITE_ACTIVE_LEVEL=SPDLITE_LEVEL_INFO
// to strip the SPDLITE_DEBUG call entirely (no format string, no arg eval).
// `logger_st` here demonstrates the single-threaded opt-out: identical API to
// `logger`, but no mutex - use it only when you can prove the logger is never
// shared across threads.
void compile_time_gating_example() {
    using namespace spdlite;
    logger_st<console_sink> console(console_sink{});
    SPDLITE_DEBUG(console, "debug message — visible at LEVEL_TRACE/DEBUG, elided at LEVEL_INFO+");
    SPDLITE_INFO(console, "info message — always compiled in unless built at LEVEL_WARN or higher");
}
