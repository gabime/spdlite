// SPDX-License-Identifier: MIT
// Copyright (c) 2026, Gabi Melman
//
// Compiled-fmt backend - opt-in via SPDLITE_COMPILED_FMT.
//
// Emits fmt's out-of-line implementation (dragonbox float formatting, vformat_to,
// bigint, ...) exactly ONCE here, so every TU that logs sees declarations only and
// compiles ~faster instead of re-instantiating the whole engine. The runtime path
// is otherwise identical to header-only fmt.
//
// To use without CMake: compile this file into your build and define
// SPDLITE_COMPILED_FMT for every TU that includes spdlite. The CMake option
// SPDLITE_COMPILED_FMT wires both up (see CMakeLists.txt).
//
// The fmt config below MUST match common.h's (FMT_UNICODE=0, FMT_USE_LOCALE=0).
// FMT_HEADER_ONLY is deliberately NOT defined: format-inl.h then leaves FMT_FUNC
// empty, giving the definitions external linkage (header-only would mark them
// `inline` and the linker would discard them).

#ifndef FMT_UNICODE
    #define FMT_UNICODE 0
#endif
#ifndef FMT_USE_LOCALE
    #define FMT_USE_LOCALE 0
#endif

#include "spdlite/fmt/format-inl.h"

// With FMT_USE_LOCALE=0 the float-formatting code still references these locale
// helpers, but format-inl.h declares them `extern template` (see format.h) and
// their explicit instantiations normally live in fmt's src/format.cc, which
// spdlite does not vendor. Emit them here or the link fails with undefined refs.
FMT_BEGIN_NAMESPACE
namespace detail {
template FMT_API auto thousands_sep_impl<char>(locale_ref) -> thousands_sep_result<char>;
template FMT_API auto decimal_point_impl(locale_ref) -> char;
}  // namespace detail
FMT_END_NAMESPACE
