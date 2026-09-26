# Compile-time level gate for spdlite

**Status:** Design
**Date:** 2026-05-21
**Scope:** Add preprocessor macros that let the user strip log calls below a chosen severity at compile time. Pure addition; no existing API changes.

## Context

Spdlite has a runtime level gate (`set_log_level()` / `should_log()`) but no compile-time gate. Every `log.debug(...)` call site that lives in shipped code pays for:

- the runtime atomic load in `should_log()`,
- the format-string literal sitting in `.rodata`,
- the per-call-site `fmt::vformat_to` / `std::vformat_to` template instantiation,
- evaluation of the message arguments (which may have side effects or non-trivial cost).

For library/SDK authors who want trace/debug logging in development but pay nothing in release, this is unacceptable today. They have to wrap calls in `#if`s by hand. The fix is the same pattern spdlog ships: preprocessor macros that compile down to either the real call or `(void)0` based on a numeric `SPDLITE_ACTIVE_LEVEL`.

This is the highest-leverage missing feature for production use of spdlite, per the design discussion that preceded this spec.

## Goals

- Let the user pick a compile-time severity floor via `-DSPDLITE_ACTIVE_LEVEL=...`.
- Calls to a macro below that floor must vanish: no runtime check, no format string in the binary, no fmt instantiation, no argument evaluation.
- Calls at or above the floor go through the normal logger path (runtime `should_log()` still applies).
- Zero behavior change for users who don't define the macro or don't use the new headers.
- Each translation unit can set its own `SPDLITE_ACTIVE_LEVEL` independently.

## Non-goals

- No default-logger variant (`SPDLITE_TRACE(...)` without a logger arg). Spdlite has no default logger by design; macros always take the logger as the first argument.
- No `SPDLITE_TRACE_IF(cond, log, ...)` conditional variant. Trivial to add later if asked.
- No `std::source_location` capture. Separate feature, separate spec.
- No change to the runtime API. Existing code keeps working.

## Design

### Macros live at the bottom of `include/spdlite/logger.h`

No new header. The macros are appended to the existing `logger.h` after the `namespace spdlite` block closes, so they're available to anyone who already includes the logger. The `SPDLITE_*` prefix means non-users won't see collisions; the runtime cost for non-users is zero.

```cpp
// appended to include/spdlite/logger.h, AFTER the closing `}  // namespace spdlite`

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

A user who wants a non-default level must set the macro *before* the first `#include` of `logger.h` in that TU - see the [Per-TU configurability](#per-tu-configurability) section below.

### Design choices baked in (decided during brainstorming)

| Choice | Decision | Reason |
|---|---|---|
| Mechanism | Preprocessor macros only | Guarantees full elision: no format string in binary, no fmt instantiation, no symbol. Matches spdlog. |
| Default `SPDLITE_ACTIVE_LEVEL` | `SPDLITE_LEVEL_TRACE` | Least surprise: users who don't set the macro see today's behavior. Slimming is explicit opt-in. |
| Macro naming | `SPDLITE_TRACE(log, ...)` (logger as first arg) | Spdlite has no default-logger concept, so only one form is needed. Verbose `SPDLITE_LOGGER_TRACE` is redundant. |
| Error-macro spelling | `SPDLITE_ERROR` (not `SPDLITE_ERR`) | Matches the spelled-out method name `log.error(...)`. Same trade-off spdlog made. |
| Where macros live | Appended to existing `logger.h` | Discoverable next to the API they gate. `SPDLITE_*` prefix prevents collisions; non-users pay nothing. Matches spdlog's `spdlog.h` layout. |

### Semantics

Two gates, AND-ed:

1. **Compile-time gate** (the new macro): if `SPDLITE_ACTIVE_LEVEL` is set above the level, the macro expands to `(void)0` and the call site disappears.
2. **Runtime gate** (existing): surviving calls still go through `log.should_log()` and can be further filtered by `log.set_log_level(...)`.

This means the compile-time gate is a strict subset of what the runtime gate can do, but it eliminates the cost where the runtime gate would only short-circuit it.

### Macro hygiene

- `(log).method(...)` - the logger expression is parenthesized so complex expressions like `SPDLITE_TRACE(get_logger("x"), "fmt", arg)` work safely.
- `(void)0` for the no-op - standard idiom; safe inside `if (cond) SPDLITE_TRACE(...);` without dangling-else issues.
- `__VA_ARGS__` only - no `__VA_OPT__` needed because the macros always require at least one argument (the format string).

### Per-TU configurability

Falls out of macros being preprocessor-level: any TU can do

```cpp
#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_INFO
#include "spdlite/logger.h"
```

and that TU's call sites are gated independently of other TUs. There is no ODR concern because the macros only affect what the call sites expand to; no `inline` function body in spdlite depends on `SPDLITE_ACTIVE_LEVEL`. Each TU's macros are local to that TU.

## Testing

Three test translation units, all linked into the existing `spdlite_tests` binary (no new CMake target). Each TU sets `SPDLITE_ACTIVE_LEVEL` before including `logger.h`, then verifies elision behavior.

### `tests/test_log_macros_trace.cpp` - full coverage

```cpp
#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_TRACE
#include "spdlite/logger.h"
// ...
TEST_CASE("at LEVEL_TRACE, all six macros emit and evaluate their args") {
    int call_count = 0;
    auto bumper = [&]{ ++call_count; return 42; };
    capture_sink cap;
    logger_st<capture_sink> log{cap};
    log.set_log_level(level::trace);

    SPDLITE_TRACE(log,    "x={}", bumper());
    SPDLITE_DEBUG(log,    "x={}", bumper());
    SPDLITE_INFO(log,     "x={}", bumper());
    SPDLITE_WARN(log,     "x={}", bumper());
    SPDLITE_ERROR(log,    "x={}", bumper());
    SPDLITE_CRITICAL(log, "x={}", bumper());

    CHECK(call_count == 6);
    CHECK(cap.state->payloads.size() == 6);
}
```

### `tests/test_log_macros_warn.cpp` - boundary case

```cpp
#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_WARN
#include "spdlite/logger.h"
// ...
TEST_CASE("at LEVEL_WARN, trace/debug/info elide; warn/error/critical emit") {
    int call_count = 0;
    auto bumper = [&]{ ++call_count; return 42; };
    // ... same setup

    SPDLITE_TRACE(log,    "x={}", bumper());  // elided
    SPDLITE_DEBUG(log,    "x={}", bumper());  // elided
    SPDLITE_INFO(log,     "x={}", bumper());  // elided
    SPDLITE_WARN(log,     "x={}", bumper());  // emits
    SPDLITE_ERROR(log,    "x={}", bumper());  // emits
    SPDLITE_CRITICAL(log, "x={}", bumper());  // emits

    CHECK(call_count == 3);  // proves boundary correctness
    CHECK(cap.state->payloads.size() == 3);
}
```

### `tests/test_log_macros_off.cpp` - full elision

```cpp
#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_OFF
#include "spdlite/logger.h"
// ...
TEST_CASE("at LEVEL_OFF, all six macros elide entirely") {
    int call_count = 0;
    auto bumper = [&]{ ++call_count; return 42; };
    // ... same setup

    SPDLITE_TRACE(log,    "x={}", bumper());
    SPDLITE_DEBUG(log,    "x={}", bumper());
    SPDLITE_INFO(log,     "x={}", bumper());
    SPDLITE_WARN(log,     "x={}", bumper());
    SPDLITE_ERROR(log,    "x={}", bumper());
    SPDLITE_CRITICAL(log, "x={}", bumper());

    CHECK(call_count == 0);                 // proof of elision
    CHECK(cap.state->payloads.empty());
}
```

### Why three TUs?

A `TRACE`-only build doesn't catch elision bugs (nothing is elided). An `OFF`-only build doesn't catch the *boundary* (everything elides uniformly). A bug like `<=` written as `<` in one of the six `#if` checks would only surface in a mid-spectrum build like `WARN`, where some macros must emit and some must elide. The `WARN` TU is the actual correctness witness.

### CMake change

Append the three new sources to the existing `spdlite_tests` target in `tests/CMakeLists.txt`. No new targets, no new `add_test()` calls.

## Documentation

### README

Add a section "Compile-time level gating" with the minimal example:

```cpp
// In one TU:
#define SPDLITE_ACTIVE_LEVEL SPDLITE_LEVEL_INFO
#include "spdlite/logger.h"

void hot_path(spdlite::logger_st<...>& log) {
    SPDLITE_DEBUG(log, "value={}", expensive_to_compute());  // gone in release
    SPDLITE_INFO(log,  "did the thing");                     // stays
}
```

Document the three rules:
1. `SPDLITE_ACTIVE_LEVEL` is a per-TU setting. Set it before `#include "spdlite/logger.h"`.
2. Default is `SPDLITE_LEVEL_TRACE` (no elision).
3. Elision means the macro expands to `(void)0` - the format string and all argument expressions disappear.

### Example

Add `compile_time_gating_example()` to `example/example.cpp`. Demonstrates one macro that emits and one that's elided, with a comment showing the `-DSPDLITE_ACTIVE_LEVEL=...` flag.

## Out of scope

- `SPDLITE_TRACE(...)` without a logger arg (no default-logger).
- `SPDLITE_TRACE_IF(cond, log, ...)`.
- `std::source_location` capture.
- Any binary-size measurement infrastructure (caller_zoo measures template-instantiation cost; a separate one-time measurement can demonstrate elision savings, but is not part of this spec).

## Files touched

| File | Change |
|---|---|
| `include/spdlite/logger.h` | Append ~50 lines of `#define`s after the closing `}  // namespace spdlite`. No changes to the logger class itself. |
| `tests/test_log_macros_trace.cpp` | New. ~30 lines. |
| `tests/test_log_macros_warn.cpp` | New. ~30 lines. |
| `tests/test_log_macros_off.cpp` | New. ~30 lines. |
| `tests/CMakeLists.txt` | Add the three new test sources to `spdlite_tests`. ~3 lines. |
| `README.md` | New "Compile-time level gating" section. ~25 lines. |
| `example/example.cpp` | New `compile_time_gating_example()` function. ~15 lines. |

The `logger.h` change is purely additive (no edits to the class). No API breakage.
