# spdlite

[![Linux](https://github.com/gabime/spdlite/actions/workflows/linux.yml/badge.svg)](https://github.com/gabime/spdlite/actions/workflows/linux.yml)
[![macOS](https://github.com/gabime/spdlite/actions/workflows/macos.yml/badge.svg)](https://github.com/gabime/spdlite/actions/workflows/macos.yml)
[![Windows](https://github.com/gabime/spdlite/actions/workflows/windows.yml/badge.svg)](https://github.com/gabime/spdlite/actions/workflows/windows.yml)

A small, header-only C++20 logger - the lite version of [spdlog](https://github.com/gabime/spdlog), simpler, smaller, fewer features.

Use spdlite if you want a tiny, fast, capable logger.

## Install

The simplest path: copy the `include/spdlite/` folder into your build tree — no build step, header-only.

To consume it via CMake instead, either install it and use `find_package`:

```bash
cmake -B build .
cmake --install build --prefix /your/prefix
```

```cmake
find_package(spdlite REQUIRED)
target_link_libraries(your_app PRIVATE spdlite::spdlite)
```

or pull it in directly with `FetchContent` (or `add_subdirectory`):

```cmake
include(FetchContent)
FetchContent_Declare(spdlite
    GIT_REPOSITORY https://github.com/gabime/spdlite.git
    GIT_TAG main)
FetchContent_MakeAvailable(spdlite)
target_link_libraries(your_app PRIVATE spdlite::spdlite)
```

## Quick start
```c++
#include "spdlite/logger.h"
#include "spdlite/sinks/console_sink.h"

int main() {
    using namespace spdlite;
    logger<console_sink> log("app", console_sink{});

    log.info("Hello {}", "world");
    log.info("Value: {}", 42);
    log.warn("Something happened");
    log.error("Failed with code {}", -1);
}
```

Output:
```
[2026-04-11 10:30:45.123] [app] [INF] Hello world
[2026-04-11 10:30:45.123] [app] [INF] Value: 42
[2026-04-11 10:30:45.123] [app] [WRN] Something happened
[2026-04-11 10:30:45.123] [app] [ERR] Failed with code -1
```

See [`include/spdlite/logger.h`](include/spdlite/logger.h) for the full API and [`include/spdlite/sinks/`](include/spdlite/sinks/) for the available sinks.

## Thread safety

`logger` is thread-safe by default — it uses a `std::mutex` to serialize format and dispatch
per call, so multiple threads can write through the same instance safely.

If you don't require thread safety, you can use `logger_st` which skips the lock entirely:

```c++
spdlite::logger<console_sink>    loogger("app", console_sink{});  // std::mutex
spdlite::logger_st<console_sink> logger("app", console_sink{});  // no locking
```

Both share the same API; only the mutex type differs.

## Formatter options

The default header is `[YYYY-MM-DD HH:MM:SS.mmm] [name] [LVL] payload`. Reconfigure
via `format_options`:

```c++
log.format_options({.utc = true});
log.format_options({.precision = time_precision::ns});
log.format_options({.show_date = false, .precision = time_precision::none});
```

See the table below for all available fields:

| Field            | Default              | Effect                                                       |
| ---------------- | -------------------- | ------------------------------------------------------------ |
| `utc`            | `false`              | Use `gmtime` instead of `localtime`.                         |
| `show_date`      | `true`               | Include the `YYYY-MM-DD ` prefix.                            |
| `show_thread_id` | `false`              | Include a 6-digit thread id after the timestamp.             |
| `precision`      | `time_precision::ms` | Fractional digits: `none`, `ms` (3), `us` (6), or `ns` (9).  |

## Compile-time level gating

Strip log calls below a chosen severity from the binary entirely — no format string,
no argument evaluation, no symbol — via the `SPDLITE_*` macros:

```c++
#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_INFO  // before the include
#include "spdlite/logger.h"

void hot_path(spdlite::logger<spdlite::console_sink>& log) {
    SPDLITE_DEBUG(log, "value={}", expensive_to_compute());  // gone — args not evaluated
    SPDLITE_INFO(log,  "did the thing");                     // stays
}
```

Macros: `SPDLITE_TRACE`, `SPDLITE_DEBUG`, `SPDLITE_INFO`, `SPDLITE_WARN`, `SPDLITE_ERROR`,
`SPDLITE_CRITICAL`. Levels: `SPDLITE_LEVEL_TRACE` (0) ... `SPDLITE_LEVEL_OFF` (6). Per-TU
setting; default is `SPDLITE_LEVEL_TRACE` (no elision). The compile-time gate is independent
of the runtime `set_log_level()` — a call emits only when both gates pass.

## Build options

CMake isn't required to *use* spdlite — copy the headers and you're done. The flags below
only apply if you build the bundled example, tests, or benchmarks with the provided
`CMakeLists.txt`.

| Option                   | Default | Effect                                                                 |
| ------------------------ | ------- | ---------------------------------------------------------------------- |
| `SPDLITE_BUILD_EXAMPLE`  | `ON`    | Build the example executable.                                          |
| `SPDLITE_BUILD_TESTS`    | `OFF`   | Build the doctest-based unit tests.                                    |
| `SPDLITE_BUILD_BENCH`    | `OFF`   | Build the benchmarks (fetches Google Benchmark automatically).         |
| `SPDLITE_USE_STD_FORMAT` | `OFF`   | CMake option: use `<format>` instead of bundled fmt — drop `fmt/` from the install. Pass via `-DSPDLITE_USE_STD_FORMAT=ON`. |

## Benchmarks

Build and run:
```console
$ cmake -B build -DSPDLITE_BUILD_BENCH=ON .
$ cmake --build build
$ ./build/latency           # quick set
$ ./build/latency full      # adds multi-threaded + file I/O
```

## License
MIT
