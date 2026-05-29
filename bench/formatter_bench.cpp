//
// formatter_bench.cpp - benchmark the formatter in isolation
//

#include "benchmark/benchmark.h"
#include "spdlite/formatter.h"

using namespace spdlite;

// appends: header + payload + \r?\n - mirrors what logger does per call
static inline void format_line(formatter& fmt, memory_buf_t& dest, log_clock::time_point t, level lvl, std::string_view payload) {
    fmt.format_header(t, lvl, dest);
    dest.append(payload.data(), payload.data() + payload.size());
#ifdef _WIN32
    dest.push_back('\r');
#endif
    dest.push_back('\n');
}

// Format with short payload
static void bench_format_short(benchmark::State& state) {
    formatter fmt("mylogger");
    memory_buf_t buf;
    auto now = log_clock::now();
    for (auto _ : state) {
        buf.clear();
        format_line(fmt, buf, now, level::info, "Hello world");
        benchmark::DoNotOptimize(buf.data());
    }
}

// Format with typical payload
static void bench_format_typical(benchmark::State& state) {
    formatter fmt("mylogger");
    memory_buf_t buf;
    auto now = log_clock::now();
    for (auto _ : state) {
        buf.clear();
        format_line(fmt, buf, now, level::info, "Hello logger: msg number 12345...............");
        benchmark::DoNotOptimize(buf.data());
    }
}

// Format with long payload (500 bytes)
static void bench_format_long(benchmark::State& state) {
    formatter fmt("mylogger");
    memory_buf_t buf;
    auto now = log_clock::now();
    std::string_view payload =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum pharetra metus cursus "
        "lacus placerat congue. Nulla egestas, mauris a tincidunt tempus, enim lectus volutpat mi, "
        "eu consequat sem libero nec massa. In dapibus ipsum a diam rhoncus gravida. Etiam non dapibus eros. Donec "
        "fringilla dui sed augue pretium, nec scelerisque est maximus. Nullam convallis, sem nec blandit maximus, "
        "nisi turpis ornare nisl, sit amet volutpat neque massa eu odio. Maecenas malesuada quam ex.";
    for (auto _ : state) {
        buf.clear();
        format_line(fmt, buf, now, level::info, payload);
        benchmark::DoNotOptimize(buf.data());
    }
}

// Just the timestamp (cached path - same second)
static void bench_timestamp_cached(benchmark::State& state) {
    formatter fmt("x");
    memory_buf_t buf;
    auto now = log_clock::now();
    // Warm the cache
    format_line(fmt, buf, now, level::info, "x");
    for (auto _ : state) {
        buf.clear();
        format_line(fmt, buf, now, level::info, "x");
        benchmark::DoNotOptimize(buf.data());
    }
}

// Timestamp with varying milliseconds (simulates real traffic)
static void bench_timestamp_varying_ms(benchmark::State& state) {
    formatter fmt("x");
    memory_buf_t buf;
    int ms = 0;
    auto base = std::chrono::system_clock::now();
    for (auto _ : state) {
        auto t = base + std::chrono::milliseconds(ms++ % 1000);
        buf.clear();
        format_line(fmt, buf, t, level::info, "x");
        benchmark::DoNotOptimize(buf.data());
    }
}

// Per-call hot path under each format_options combination - regression guard.
// Stays in the cached-second path so only the per-call patcher + memcpy varies.
template <bool Utc, bool ShowDate, time_precision Prec>
static void bench_options_cached(benchmark::State& state) {
    formatter fmt("mylogger", format_options{.utc = Utc, .show_date = ShowDate, .precision = Prec});
    memory_buf_t buf;
    auto now = log_clock::now();
    format_line(fmt, buf, now, level::info, "warmup");  // prime the timestamp cache
    for (auto _ : state) {
        buf.clear();
        format_line(fmt, buf, now, level::info, "Hello logger: msg number 12345...............");
        benchmark::DoNotOptimize(buf.data());
    }
}

// rebuild_timestamp path - varying millisecond crosses second boundaries occasionally
// and exercises localtime_r/gmtime_r in the rebuild step.
template <bool Utc, bool ShowDate, time_precision Prec>
static void bench_options_varying(benchmark::State& state) {
    formatter fmt("mylogger", format_options{.utc = Utc, .show_date = ShowDate, .precision = Prec});
    memory_buf_t buf;
    int ms = 0;
    auto base = std::chrono::system_clock::now();
    for (auto _ : state) {
        auto t = base + std::chrono::milliseconds(ms++ % 1000);
        buf.clear();
        format_line(fmt, buf, t, level::info, "Hello logger: msg number 12345...............");
        benchmark::DoNotOptimize(buf.data());
    }
}

// thread_id path - measures the per-call cost of the show_thread_id field
// (one thread_local load + one put6 + a longer cached-header memcpy).
template <bool ShowTid>
static void bench_options_thread_id(benchmark::State& state) {
    formatter fmt("mylogger", format_options{.show_thread_id = ShowTid});
    memory_buf_t buf;
    auto now = log_clock::now();
    format_line(fmt, buf, now, level::info, "warmup");  // prime caches (timestamp + thread_local tid)
    for (auto _ : state) {
        buf.clear();
        format_line(fmt, buf, now, level::info, "Hello logger: msg number 12345...............");
        benchmark::DoNotOptimize(buf.data());
    }
}

BENCHMARK(bench_format_short);
BENCHMARK(bench_format_typical);
BENCHMARK(bench_format_long);
BENCHMARK(bench_timestamp_cached);
BENCHMARK(bench_timestamp_varying_ms);

// cached-path matrix: every relevant format_options combination
BENCHMARK(bench_options_cached<false, true, time_precision::ms>)->Name("opts_cached/default");
BENCHMARK(bench_options_cached<true, true, time_precision::ms>)->Name("opts_cached/utc");
BENCHMARK(bench_options_cached<false, false, time_precision::ms>)->Name("opts_cached/no_date");
BENCHMARK(bench_options_cached<false, true, time_precision::none>)->Name("opts_cached/no_frac");
BENCHMARK(bench_options_cached<false, true, time_precision::us>)->Name("opts_cached/us");
BENCHMARK(bench_options_cached<false, true, time_precision::ns>)->Name("opts_cached/ns");
BENCHMARK(bench_options_cached<false, false, time_precision::none>)->Name("opts_cached/minimal");

// varying-time matrix: catches regressions in rebuild_timestamp (UTC vs local + layout shift)
BENCHMARK(bench_options_varying<false, true, time_precision::ms>)->Name("opts_varying/default");
BENCHMARK(bench_options_varying<true, true, time_precision::ms>)->Name("opts_varying/utc");
BENCHMARK(bench_options_varying<false, false, time_precision::ms>)->Name("opts_varying/no_date");
BENCHMARK(bench_options_varying<false, true, time_precision::ns>)->Name("opts_varying/ns");

// thread_id pair: side-by-side baseline (off) vs the new path (on).
BENCHMARK(bench_options_thread_id<false>)->Name("opts_cached/tid_off");
BENCHMARK(bench_options_thread_id<true>)->Name("opts_cached/tid_on");

BENCHMARK_MAIN();
