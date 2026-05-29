//
// Copyright(c) 2018 Gabi Melman.
// Distributed under the MIT License (http://opensource.org/licenses/MIT)
//

//
// latency.cpp : spdlite latency benchmarks
//

#include <cstdio>
#include <filesystem>
#include <iostream>

#include "benchmark/benchmark.h"
#include "spdlite/logger.h"
#include "spdlite/sinks/console_sink.h"
#include "spdlite/sinks/file_sink.h"
#include "spdlite/sinks/null_sink.h"
#include "spdlite/sinks/shared_sink.h"

using namespace spdlite;

// Output dir for file-touching benches. On Linux, /tmp is typically tmpfs so
// these writes don't hit the SSD on heavy runs.
static std::filesystem::path bench_dir() { return std::filesystem::temp_directory_path() / "spdlite_bench"; }

// Bench with a long C string (no formatting)
static void bench_null_sink_c_string(benchmark::State& state) {
    logger_st<null_sink> log("bench", null_sink{});
    const char* msg =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vestibulum pharetra metus cursus "
        "lacus placerat congue. Nulla egestas, mauris a tincidunt tempus, enim lectus volutpat mi, "
        "eu consequat sem "
        "libero nec massa. In dapibus ipsum a diam rhoncus gravida. Etiam non dapibus eros. Donec "
        "fringilla dui sed "
        "augue pretium, nec scelerisque est maximus. Nullam convallis, sem nec blandit maximus, "
        "nisi turpis ornare "
        "nisl, sit amet volutpat neque massa eu odio. Maecenas malesuada quam ex, posuere congue "
        "nibh turpis duis.";

    for (auto _ : state) {
        log.info(msg);
    }
}

// Bench with fmt formatting
static void bench_null_sink_formatted(benchmark::State& state) {
    logger_st<null_sink> log("bench", null_sink{});
    int i = 0;
    for (auto _ : state) {
        log.info("Hello logger: msg number {}...............", ++i);
    }
}

// Bench with logger disabled at runtime
static void bench_disabled_runtime(benchmark::State& state) {
    logger_st<null_sink> log("bench", null_sink{});
    log.set_log_level(level::off);
    int i = 0;
    for (auto _ : state) {
        log.info("Hello logger: msg number {}...............", ++i);
    }
}

// Bench null_sink_mt with multiple threads
static void bench_null_sink_mt(benchmark::State& state) {
    static logger<null_sink> log("bench", null_sink{});
    int i = 0;
    for (auto _ : state) {
        log.info("Hello logger: msg number {}...............", ++i);
    }
}

// Bench color stdout sink (single-threaded)
static void bench_color_sink_st(benchmark::State& state) {
    logger_st<console_sink> log("bench", console_sink{});
    int i = 0;
    for (auto _ : state) {
        log.info("Hello logger: msg number {}...............", ++i);
    }
}

// Bench color stdout sink (multi-threaded)
static void bench_color_sink_mt(benchmark::State& state) {
    static logger<console_sink> log("bench", console_sink{});
    int i = 0;
    for (auto _ : state) {
        log.info("Hello logger: msg number {}...............", ++i);
    }
}

// Bench basic file sink (single-threaded)
static void bench_basic_file_st(benchmark::State& state) {
    logger_st<file_sink> log("bench", file_sink{bench_dir() / "basic_st.log", open_mode::truncate});
    int i = 0;
    for (auto _ : state) {
        log.info("Hello logger: msg number {}...............", ++i);
    }
}

// Bench basic file sink (multi-threaded)
static void bench_basic_file_mt(benchmark::State& state) {
    static logger<file_sink> log("bench", file_sink{bench_dir() / "basic_mt.log", open_mode::truncate});
    int i = 0;
    for (auto _ : state) {
        log.info("Hello logger: msg number {}...............", ++i);
    }
}

// Bench shared_sink<file_sink> across multiple loggers (multi-threaded).
// Two independent loggers write through the same underlying file_sink, so
// every write takes the shared lock in addition to each logger's own mutex.
// Compare against bench_basic_file_mt to read the cross-logger lock cost.
static void bench_shared_file_mt(benchmark::State& state) {
    static auto raw = std::make_shared<file_sink>(bench_dir() / "shared_mt.log", open_mode::truncate);
    static shared_sink<file_sink> wrapped(raw);
    static logger<shared_sink<file_sink>> log_a("bench_a", wrapped);
    static logger<shared_sink<file_sink>> log_b("bench_b", wrapped);
    int i = 0;
    for (auto _ : state) {
        // alternate loggers to exercise cross-logger contention on the shared lock
        auto& log = (state.thread_index() & 1) ? log_a : log_b;
        log.info("Hello logger: msg number {}...............", ++i);
    }
}

// Bench just the formatter (no I/O)
static void bench_formatter_only(benchmark::State& state) {
    formatter fmt("bench");
    memory_buf_t buf;
    auto now = log_clock::now();
    std::string_view payload = "Hello logger: msg number 12345...............";
    for (auto _ : state) {
        buf.clear();
        fmt.format_header(now, level::info, buf);
        buf.append(payload.data(), payload.data() + payload.size());
#ifdef _WIN32
        buf.push_back('\r');
#endif
        buf.push_back('\n');
        benchmark::DoNotOptimize(buf.data());
    }
}

int main(int argc, char* argv[]) {
    // Fixed thread count keeps numbers comparable across machines (was: num_cpus).
    // Each _mt bench also runs single-threaded to isolate mutex-acquisition cost
    // from contention cost.
    constexpr int n_threads = 4;

    auto full_bench = argc > 1 && std::string(argv[1]) == "full";

    benchmark::RegisterBenchmark("disabled-at-runtime", bench_disabled_runtime);
    benchmark::RegisterBenchmark("null_sink_st (500_bytes c_str)", bench_null_sink_c_string);
    benchmark::RegisterBenchmark("null_sink_st", bench_null_sink_formatted);
    benchmark::RegisterBenchmark("formatter_only", bench_formatter_only);
    benchmark::RegisterBenchmark("color_sink_st", bench_color_sink_st)->UseRealTime();

    if (full_bench) {
        benchmark::RegisterBenchmark("null_sink_mt", bench_null_sink_mt)->Threads(1)->UseRealTime();
        benchmark::RegisterBenchmark("null_sink_mt", bench_null_sink_mt)->Threads(n_threads)->UseRealTime();
        benchmark::RegisterBenchmark("color_sink_mt", bench_color_sink_mt)->Threads(1)->UseRealTime();
        benchmark::RegisterBenchmark("color_sink_mt", bench_color_sink_mt)->Threads(n_threads)->UseRealTime();
        benchmark::RegisterBenchmark("basic_file_st", bench_basic_file_st)->UseRealTime();
        benchmark::RegisterBenchmark("basic_file_mt", bench_basic_file_mt)->Threads(1)->UseRealTime();
        benchmark::RegisterBenchmark("basic_file_mt", bench_basic_file_mt)->Threads(n_threads)->UseRealTime();
        benchmark::RegisterBenchmark("shared_file_mt", bench_shared_file_mt)->Threads(1)->UseRealTime();
        benchmark::RegisterBenchmark("shared_file_mt", bench_shared_file_mt)->Threads(n_threads)->UseRealTime();
    }

    benchmark::Initialize(&argc, argv);

    // redirect C stdout to null so log output from stdout/color sinks is silenced,
    // but keep benchmark results visible via cout -> stderr
    std::ios::sync_with_stdio(false);
    std::cout.rdbuf(std::cerr.rdbuf());
#ifdef _WIN32
    if (std::freopen("NUL", "w", stdout) == nullptr) {
        std::cerr << "freopen failed\n";
        return 1;
    }
#else
    if (std::freopen("/dev/null", "w", stdout) == nullptr) {
        std::cerr << "freopen failed\n";
        return 1;
    }
#endif

    benchmark::RunSpecifiedBenchmarks();
}
