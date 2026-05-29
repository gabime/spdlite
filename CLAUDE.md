# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What is spdlite

A minimal, header-only C++20 logging library - a lite version of [spdlog](https://github.com/gabime/spdlog). Sinks are compile-time template parameters (zero virtual dispatch). Default `logger` is thread-safe (`std::mutex`); `logger_st` is the single-threaded opt-out (null_mutex, zero locking). Bundles fmt 12.1.0 (header-only); can alternatively use `std::format` via `-DSPDLITE_USE_STD_FORMAT`.

## Build commands

```bash
# configure + build example (default)
cmake -B build .
cmake --build build

# build with std::format instead of bundled fmt
cmake -B build -DSPDLITE_USE_STD_FORMAT=ON .
cmake --build build

# build + run tests (doctest - fetched automatically)
cmake -B build -DSPDLITE_BUILD_TESTS=ON .
cmake --build build
ctest --test-dir build --output-on-failure

# build benchmarks (requires Google Benchmark - fetched automatically)
cmake -B build -DSPDLITE_BUILD_BENCH=ON .
cmake --build build

# build everything (example + tests + bench)
cmake -B build -DSPDLITE_BUILD_ALL=ON .
cmake --build build

# run
./build/example          # or build/Debug/example.exe on MSVC
./build/latency          # quick benchmarks
./build/latency full     # includes multi-threaded and file I/O benchmarks
./build/formatter_bench

# comparative benchmark vs spdlog (only if ../spdlog/ exists as a sibling checkout)
./build/vs_spdlog
```

Tests live under `tests/` (doctest, fetched via FetchContent) and run with `ctest`. CI exercises gcc/clang/msvc against both the bundled fmt and `SPDLITE_USE_STD_FORMAT=ON`. Formatting is governed by `.clang-format` (Google base, 4-space indent, 130-col limit) - run `clang-format -i <files>` before committing.

A pre-commit hook in `.githooks/pre-commit` formats staged C/C++ files automatically. Enable it once per clone with `git config core.hooksPath .githooks` (preserves any global `pre-push` via a delegate).

## Architecture

All code lives under `include/spdlite/` - there is no `.cpp` compilation.

- **`common.h`** - shared types (`level` enum, `log_msg`, `stack_buf<N>` used as `memory_buf_t`, `level_names`, `fwrite_bytes`, `null_mutex`, `atomic_level_t`). Also pulls in fmt (or `<format>`). Sinks include this directly, avoiding the formatter and logger template.
- **`formatter.h`** - `simple_formatter` plus `put2`/`put3`/`put4` helpers. Produces `[YYYY-MM-DD HH:MM:SS.mmm] [name] [LVL] payload\n`. Caches the entire header string and patches only millis (3 bytes) and level (1 byte) per call; timestamp rebuilds only on second-boundary change.
- **`logger.h`** - public include. Pulls in `common.h` + `formatter.h` and defines the `logger<Mutex, Sinks...>` template (with `logger_mt` / `logger_st` aliases). Two internal paths: `log_sv_` (string_view, no formatting) and `log_fmt_args_` (fmt/std::format into payload buffer, fed by per-Args trampoline `dispatch_fmt_`). Both consult `should_flush(lvl)` after dispatch and fold-call `flush()` on every sink when the message level meets the flush threshold (default `level::off` = no auto-flush). Emits `\r\n` on Windows, `\n` elsewhere.
- **`sinks/`** - each sink is a simple struct with `write(const log_msg&)` and `flush()`, including only `common.h` from spdlite. The `log_msg` carries metadata plus two views into the logger's shared buffer: `formatted` (the whole line, header + payload + newline) and `payload` (the raw user message, no header, no newline). Sinks typically write `msg.formatted`; structured sinks (JSON, syslog, etc.) can read `msg.payload` directly. No base class - sinks are duck-typed template parameters.
  - `stdout_sink` / `stderr_sink` - plain `fwrite` to stdout/stderr.
  - `console_sink` / `console_err_sink` - inserts color around the level tag. Native Win32 `SetConsoleTextAttribute` on Windows, ANSI escape codes on Linux/macOS.
  - `file_sink` - `fopen`/`fwrite` with RAII via `unique_ptr<FILE>`. Creates parent directories automatically.
  - `rotating_file_sink` - same as `file_sink` plus a `max_size` cap with N-file rotation (`app.txt` -> `app.1.txt` -> ... -> dropped). `max_files` defaults to 1 (single rotation), capped at `rotating_file_sink::max_files_limit` (1000). Tracks `current_size_` in the sink so the cap check is one int compare on the hot path.
  - `null_sink` - discards output (used in benchmarks).
  - `shared_sink<Sink>` - wraps any sink with `shared_ptr<Sink>` + a shared `std::mutex`, so multiple loggers can write through one underlying instance. Copy the wrapper to give it to additional loggers - both the sink and the lock are shared. The wrapper's lock serializes cross-logger access; each `logger`'s own mutex still serializes within one logger.
- **`fmt/`** - vendored fmt 12.1.0 headers (`base.h`, `format.h`, `format-inl.h`). Do not edit these.

To drop into another project, copy `include/spdlite/logger.h` plus the sinks you need (and `fmt/` unless using `SPDLITE_USE_STD_FORMAT`). No CMake required.

## Code style

- Comments inside function bodies start lowercase; comments before declarations can start uppercase.
- Level names are fixed 3-char tags: TRC, DBG, INF, WRN, ERR, CRT, OFF.
- Single namespace: `spdlite`. Sinks are types like `console_sink`, `file_sink`, `null_sink` directly in `spdlite::`.
