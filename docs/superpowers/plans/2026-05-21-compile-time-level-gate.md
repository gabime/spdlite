# Compile-time level gate implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add preprocessor macros (`SPDLITE_TRACE`/`DEBUG`/`INFO`/`WARN`/`ERROR`/`CRITICAL`) to spdlite that compile down to either the real method call or `(void)0` based on `SPDLITE_ACTIVE_LEVEL`, with three test TUs proving full coverage, mid-spectrum boundary correctness, and full elision.

**Architecture:** Macros are appended to the bottom of `include/spdlite/logger.h` after the closing `}  // namespace spdlite`. The level constants (`SPDLITE_LEVEL_TRACE` ... `SPDLITE_LEVEL_OFF`) are numbered 0..6 to match `enum class level`. Default is `SPDLITE_LEVEL_TRACE` (no elision). Each test TU sets `SPDLITE_ACTIVE_LEVEL` before `#include`-ing `logger.h` and verifies elision behavior using a counter-bumping lambda as a format arg - the lambda's call count proves whether the macro's args were evaluated.

**Tech Stack:** C++20 header-only library. doctest 2.5.2 test framework (already vendored via FetchContent in `tests/CMakeLists.txt`). CMake build with `-DSPDLITE_BUILD_TESTS=ON`.

**Reference:** `docs/superpowers/specs/2026-05-21-compile-time-level-gate-design.md` is the source-of-truth spec for this plan.

---

## Task 1: Add macros + WARN boundary test

This is the load-bearing task. The WARN test is the *correctness witness* for the elision behavior - if macros are off-by-one (`<=` vs `<`), the all-elide or all-emit tests in later tasks won't catch it, but the WARN boundary will.

**Files:**
- Create: `tests/test_log_macros_warn.cpp`
- Modify: `include/spdlite/logger.h` (append after closing namespace)
- Modify: `tests/CMakeLists.txt` (add one source line)

- [ ] **Step 1: Write the failing WARN-boundary test**

Create `tests/test_log_macros_warn.cpp`:

```cpp
// SPDX-License-Identifier: MIT

// At LEVEL_WARN, TRACE/DEBUG/INFO must elide (zero arg evaluation),
// WARN/ERROR/CRITICAL must emit. This is the boundary correctness witness.

#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_WARN

#include <doctest/doctest.h>

#include "helpers.h"
#include "spdlite/logger.h"

using namespace spdlite;
using helpers::capture_sink;

TEST_CASE("at LEVEL_WARN, trace/debug/info elide; warn/error/critical emit") {
    int call_count = 0;
    auto bumper = [&] {
        ++call_count;
        return 42;
    };
    capture_sink cap;
    logger_st<capture_sink> log{cap};
    log.set_log_level(level::trace);  // runtime gate wide open - only the compile-time gate filters

    SPDLITE_TRACE(log, "x={}", bumper());     // elided
    SPDLITE_DEBUG(log, "x={}", bumper());     // elided
    SPDLITE_INFO(log, "x={}", bumper());      // elided
    SPDLITE_WARN(log, "x={}", bumper());      // emits
    SPDLITE_ERROR(log, "x={}", bumper());     // emits
    SPDLITE_CRITICAL(log, "x={}", bumper());  // emits

    CHECK(call_count == 3);                  // bumper invoked 3 times, not 6
    CHECK(cap.state->payloads.size() == 3);  // 3 messages reached the sink
    CHECK(cap.state->levels[0] == level::warn);
    CHECK(cap.state->levels[1] == level::err);
    CHECK(cap.state->levels[2] == level::critical);
}
```

- [ ] **Step 2: Wire the new source into `tests/CMakeLists.txt`**

Modify `tests/CMakeLists.txt`. Find the existing `add_executable(spdlite_tests ...)` block (lines 18-26) and add `test_log_macros_warn.cpp` as the last entry:

```cmake
add_executable(spdlite_tests
    test_level.cpp
    test_formatter.cpp
    test_logger.cpp
    test_file_sink.cpp
    test_rotating_file_sink.cpp
    test_null_sink.cpp
    test_shared_sink.cpp
    test_thread_safety.cpp
    test_log_macros_warn.cpp)
```

- [ ] **Step 3: Run the build, expect a compile failure**

If `build-tests/` already exists from earlier work, reuse it; otherwise configure first:

```bash
cmake -B build-tests -DSPDLITE_BUILD_TESTS=ON .
cmake --build build-tests 2>&1 | tail -20
```

Expected: `error: 'SPDLITE_TRACE' was not declared in this scope` (or analogous "undefined macro" / "identifier not found" error). This proves the macros don't exist yet.

- [ ] **Step 4: Add the macros to `logger.h`**

Modify `include/spdlite/logger.h`. After the closing `}  // namespace spdlite` (currently the last line of the file), append:

```cpp

// ===== Compile-time level gate =====
// Numeric values match enum class level (trace=0 ... off=6) in common.h.
// Set SPDLITE_ACTIVE_LEVEL before including this header to strip lower levels
// at compile time. Default is SPDLITE_LEVEL_TRACE - no elision.

#define SPDLITE_LEVEL_TRACE    0
#define SPDLITE_LEVEL_DEBUG    1
#define SPDLITE_LEVEL_INFO     2
#define SPDLITE_LEVEL_WARN     3
#define SPDLITE_LEVEL_ERROR    4
#define SPDLITE_LEVEL_CRITICAL 5
#define SPDLITE_LEVEL_OFF      6

#ifndef SPDLITE_ACTIVE_LEVEL
    #define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_TRACE
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_TRACE
    #define SPDLITE_TRACE(log, ...) (log).trace(__VA_ARGS__)
#else
    #define SPDLITE_TRACE(log, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_DEBUG
    #define SPDLITE_DEBUG(log, ...) (log).debug(__VA_ARGS__)
#else
    #define SPDLITE_DEBUG(log, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_INFO
    #define SPDLITE_INFO(log, ...) (log).info(__VA_ARGS__)
#else
    #define SPDLITE_INFO(log, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_WARN
    #define SPDLITE_WARN(log, ...) (log).warn(__VA_ARGS__)
#else
    #define SPDLITE_WARN(log, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_ERROR
    #define SPDLITE_ERROR(log, ...) (log).error(__VA_ARGS__)
#else
    #define SPDLITE_ERROR(log, ...) (void)0
#endif

#if SPDLITE_ACTIVE_LEVEL <= SPDLITE_LEVEL_CRITICAL
    #define SPDLITE_CRITICAL(log, ...) (log).critical(__VA_ARGS__)
#else
    #define SPDLITE_CRITICAL(log, ...) (void)0
#endif
```

- [ ] **Step 5: Build + run the test, expect PASS**

```bash
cmake --build build-tests 2>&1 | tail -5
ctest --test-dir build-tests --output-on-failure
```

Expected: build succeeds, `spdlite_tests` runs, `100% tests passed`.

- [ ] **Step 6: Run `clang-format -i` on touched files**

```bash
clang-format -i include/spdlite/logger.h tests/test_log_macros_warn.cpp
```

This is enforced by the project's pre-commit hook (`.githooks/pre-commit`).

- [ ] **Step 7: Commit**

```bash
git add include/spdlite/logger.h tests/test_log_macros_warn.cpp tests/CMakeLists.txt
git commit -m "compile-time level gate: SPDLITE_TRACE..CRITICAL macros + WARN boundary test"
```

---

## Task 2: Add the TRACE full-emit test

Defense in depth: proves the macros don't accidentally elide at the most-permissive setting.

**Files:**
- Create: `tests/test_log_macros_trace.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the TRACE test file**

Create `tests/test_log_macros_trace.cpp`:

```cpp
// SPDX-License-Identifier: MIT

// At LEVEL_TRACE, all six macros must emit and evaluate their args.

#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_TRACE

#include <doctest/doctest.h>

#include "helpers.h"
#include "spdlite/logger.h"

using namespace spdlite;
using helpers::capture_sink;

TEST_CASE("at LEVEL_TRACE, all six macros emit and evaluate their args") {
    int call_count = 0;
    auto bumper = [&] {
        ++call_count;
        return 42;
    };
    capture_sink cap;
    logger_st<capture_sink> log{cap};
    log.set_log_level(level::trace);

    SPDLITE_TRACE(log, "x={}", bumper());
    SPDLITE_DEBUG(log, "x={}", bumper());
    SPDLITE_INFO(log, "x={}", bumper());
    SPDLITE_WARN(log, "x={}", bumper());
    SPDLITE_ERROR(log, "x={}", bumper());
    SPDLITE_CRITICAL(log, "x={}", bumper());

    CHECK(call_count == 6);
    CHECK(cap.state->payloads.size() == 6);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`**

Append `test_log_macros_trace.cpp` to the `add_executable(spdlite_tests ...)` source list:

```cmake
add_executable(spdlite_tests
    test_level.cpp
    test_formatter.cpp
    test_logger.cpp
    test_file_sink.cpp
    test_rotating_file_sink.cpp
    test_null_sink.cpp
    test_shared_sink.cpp
    test_thread_safety.cpp
    test_log_macros_warn.cpp
    test_log_macros_trace.cpp)
```

- [ ] **Step 3: Build + run, expect PASS**

```bash
cmake --build build-tests 2>&1 | tail -5
ctest --test-dir build-tests --output-on-failure
```

Expected: build succeeds, `100% tests passed`.

- [ ] **Step 4: Format + commit**

```bash
clang-format -i tests/test_log_macros_trace.cpp
git add tests/test_log_macros_trace.cpp tests/CMakeLists.txt
git commit -m "test: LEVEL_TRACE coverage - all six log macros emit"
```

---

## Task 3: Add the OFF full-elision test

Proves the macros elide *every* level - counter remains at 0, sink receives nothing.

**Files:**
- Create: `tests/test_log_macros_off.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the OFF test file**

Create `tests/test_log_macros_off.cpp`:

```cpp
// SPDX-License-Identifier: MIT

// At LEVEL_OFF, all six macros must elide entirely - zero arg evaluation,
// zero messages reaching the sink.

#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_OFF

#include <doctest/doctest.h>

#include "helpers.h"
#include "spdlite/logger.h"

using namespace spdlite;
using helpers::capture_sink;

TEST_CASE("at LEVEL_OFF, all six macros elide entirely") {
    int call_count = 0;
    auto bumper = [&] {
        ++call_count;
        return 42;
    };
    capture_sink cap;
    logger_st<capture_sink> log{cap};
    log.set_log_level(level::trace);

    SPDLITE_TRACE(log, "x={}", bumper());
    SPDLITE_DEBUG(log, "x={}", bumper());
    SPDLITE_INFO(log, "x={}", bumper());
    SPDLITE_WARN(log, "x={}", bumper());
    SPDLITE_ERROR(log, "x={}", bumper());
    SPDLITE_CRITICAL(log, "x={}", bumper());

    CHECK(call_count == 0);              // bumper never invoked
    CHECK(cap.state->payloads.empty());  // sink saw nothing
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`**

Append `test_log_macros_off.cpp` to the source list. Final block should be:

```cmake
add_executable(spdlite_tests
    test_level.cpp
    test_formatter.cpp
    test_logger.cpp
    test_file_sink.cpp
    test_rotating_file_sink.cpp
    test_null_sink.cpp
    test_shared_sink.cpp
    test_thread_safety.cpp
    test_log_macros_warn.cpp
    test_log_macros_trace.cpp
    test_log_macros_off.cpp)
```

- [ ] **Step 3: Build + run, expect PASS**

```bash
cmake --build build-tests 2>&1 | tail -5
ctest --test-dir build-tests --output-on-failure
```

Expected: build succeeds, all three macro-test TUs pass.

- [ ] **Step 4: Format + commit**

```bash
clang-format -i tests/test_log_macros_off.cpp
git add tests/test_log_macros_off.cpp tests/CMakeLists.txt
git commit -m "test: LEVEL_OFF coverage - all six log macros elide"
```

---

## Task 4: Add example demonstrating the feature

Adds one function to `example/example.cpp` that uses the macros. Serves as both a smoke test (compiles + runs) and a discoverability anchor for readers.

**Files:**
- Modify: `example/example.cpp`

> Note: `example/example.cpp` is part of the canonical example, NOT the user's scratch. (The user's scratch is `example/a.cpp` and ad-hoc modifications to `example/CMakeLists.txt`.) Edit only the example.cpp content shown below.

- [ ] **Step 1: Add the forward declaration and `main()` call**

Modify `example/example.cpp`. Near the top of the file, with the other forward declarations (currently around line 10-16, ending with `void shared_file_sink_example();`), add:

```cpp
void compile_time_gating_example();
```

In `main()` (currently around line 19-28), add a call to it as the last example before `return 0;`:

```cpp
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
```

- [ ] **Step 2: Add the function body**

Append the implementation at the bottom of `example/example.cpp`, after `shared_file_sink_example()`:

```cpp
// Compile-time level gating: build with -DSPDLITE_ACTIVE_LEVEL=SPDLITE_LEVEL_INFO
// and the SPDLITE_DEBUG call below disappears entirely (no format string in the
// binary, no argument evaluation). The SPDLITE_INFO call survives and is gated
// only by the runtime log_level.
void compile_time_gating_example() {
    using namespace spdlite;
    logger_st console(console_sink{});
    SPDLITE_DEBUG(console, "debug message - visible at LEVEL_TRACE/DEBUG, elided at LEVEL_INFO+");
    SPDLITE_INFO(console, "info message - always compiled in unless built at LEVEL_WARN or higher");
}
```

- [ ] **Step 3: Build and run the example**

```bash
cmake --build build 2>&1 | tail -5
./build/example/example
```

Expected: build succeeds, the two new lines appear at the end of the example output (alongside the existing banners and demos).

- [ ] **Step 4: Format + commit**

```bash
clang-format -i example/example.cpp
git add example/example.cpp
git commit -m "example: demonstrate SPDLITE_DEBUG/INFO compile-time level gating"
```

---

## Task 5: Document in README

Adds a short "Compile-time level gating" section to the README.

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Read the existing README to find a good insertion point**

```bash
grep -n "^##\|^###" README.md
```

Look for a section after the basic usage examples but before the "Build" / "Benchmarks" / "License" trailing sections. A natural anchor is after any "Sinks" section or after `format_options` documentation if one exists. If none of those apply, place the new section just before the first build/benchmarks/license heading.

- [ ] **Step 2: Insert the new section**

Add this section at the insertion point identified in Step 1:

```markdown
## Compile-time level gating

To strip log calls below a chosen severity from the binary entirely (no format string,
no argument evaluation, no symbol), use the `SPDLITE_*` macros:

```cpp
#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_INFO
#include "spdlite/logger.h"

void hot_path(spdlite::logger_st<spdlite::console_sink>& log) {
    SPDLITE_DEBUG(log, "value={}", expensive_to_compute());  // gone - args not evaluated
    SPDLITE_INFO(log,  "did the thing");                     // stays
}
```

Levels are `SPDLITE_LEVEL_TRACE` (0), `SPDLITE_LEVEL_DEBUG` (1), `SPDLITE_LEVEL_INFO` (2),
`SPDLITE_LEVEL_WARN` (3), `SPDLITE_LEVEL_ERROR` (4), `SPDLITE_LEVEL_CRITICAL` (5),
`SPDLITE_LEVEL_OFF` (6). Macros are `SPDLITE_TRACE`, `SPDLITE_DEBUG`, `SPDLITE_INFO`,
`SPDLITE_WARN`, `SPDLITE_ERROR`, `SPDLITE_CRITICAL`.

Three rules:

1. `SPDLITE_ACTIVE_LEVEL` is a **per-translation-unit** setting. Set it *before* the first
   `#include "spdlite/logger.h"` in that TU (or pass `-DSPDLITE_ACTIVE_LEVEL=...` to the
   compiler for project-wide setting).
2. The default is `SPDLITE_LEVEL_TRACE` - every call survives. You opt in to elision.
3. The compile-time gate is independent of the runtime `set_log_level()` gate. A call is
   emitted only if both gates pass: the macro must survive *and* `should_log()` must return
   true at runtime.
```

- [ ] **Step 3: Verify the README renders correctly**

```bash
head -200 README.md   # or open in a Markdown viewer
```

Sanity-check: the new section appears at the insertion point with a `##` heading, the code blocks render with C++ syntax highlighting in GitHub, and the trailing rules list is numbered.

- [ ] **Step 4: Commit**

```bash
git add README.md
git commit -m "docs: README section on compile-time level gating"
```

---

## Self-review checklist (after all tasks)

- [ ] **Spec coverage**: every section of `docs/superpowers/specs/2026-05-21-compile-time-level-gate-design.md` is addressed:
  - macros (Task 1)
  - level constants (Task 1)
  - default `SPDLITE_ACTIVE_LEVEL` (Task 1)
  - per-TU configurability (works automatically; verified by the three test TUs in Tasks 1-3 living in the same binary with different settings)
  - testing (Tasks 1, 2, 3)
  - docs (Task 5)
  - example (Task 4)

- [ ] **All builds green**:
  ```bash
  cmake --build build && cmake --build build-std && cmake --build build-bench && cmake --build build-zoo && cmake --build build-tests
  ctest --test-dir build-tests --output-on-failure
  ```
  All five build configs (default, `SPDLITE_USE_STD_FORMAT`, bench, caller_zoo, tests) must succeed, and `ctest` must report `100% tests passed`.

- [ ] **No collateral changes**: `git status` should show no stray modifications outside the files listed in each task's "Files" block.
