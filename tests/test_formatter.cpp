// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include <chrono>
#include <string>
#include <string_view>

#include "helpers.h"
#include "spdlite/formatter.h"

using namespace spdlite;
using namespace spdlite::detail;
using namespace std::chrono;
using helpers::contains;

TEST_CASE("put2 writes a zero-padded 2-digit number") {
    char buf[2];
    put2(buf, 0);
    CHECK(std::string_view(buf, 2) == "00");
    put2(buf, 7);
    CHECK(std::string_view(buf, 2) == "07");
    put2(buf, 42);
    CHECK(std::string_view(buf, 2) == "42");
    put2(buf, 99);
    CHECK(std::string_view(buf, 2) == "99");
}

TEST_CASE("put3 writes a zero-padded 3-digit number") {
    char buf[3];
    put3(buf, 0);
    CHECK(std::string_view(buf, 3) == "000");
    put3(buf, 7);
    CHECK(std::string_view(buf, 3) == "007");
    put3(buf, 999);
    CHECK(std::string_view(buf, 3) == "999");
}

TEST_CASE("put4 writes a zero-padded 4-digit number") {
    char buf[4];
    put4(buf, 0);
    CHECK(std::string_view(buf, 4) == "0000");
    put4(buf, 2026);
    CHECK(std::string_view(buf, 4) == "2026");
    put4(buf, 9999);
    CHECK(std::string_view(buf, 4) == "9999");
}

TEST_CASE("put6 writes a zero-padded 6-digit number") {
    char buf[6];
    put6(buf, 0);
    CHECK(std::string_view(buf, 6) == "000000");
    put6(buf, 7);
    CHECK(std::string_view(buf, 6) == "000007");
    put6(buf, 999);
    CHECK(std::string_view(buf, 6) == "000999");
    put6(buf, 1000);
    CHECK(std::string_view(buf, 6) == "001000");
    put6(buf, 123456);
    CHECK(std::string_view(buf, 6) == "123456");
    put6(buf, 999999);
    CHECK(std::string_view(buf, 6) == "999999");
}

TEST_CASE("put9 writes a zero-padded 9-digit number") {
    char buf[9];
    put9(buf, 0);
    CHECK(std::string_view(buf, 9) == "000000000");
    put9(buf, 7);
    CHECK(std::string_view(buf, 9) == "000000007");
    put9(buf, 999);
    CHECK(std::string_view(buf, 9) == "000000999");
    put9(buf, 1000);
    CHECK(std::string_view(buf, 9) == "000001000");
    put9(buf, 999999);
    CHECK(std::string_view(buf, 9) == "000999999");
    put9(buf, 123456789);
    CHECK(std::string_view(buf, 9) == "123456789");
    put9(buf, 999999999);
    CHECK(std::string_view(buf, 9) == "999999999");
}

// Helper: extract the entire produced header as a string.
static std::string format_one(formatter& fmt, log_clock::time_point tp, level lvl) {
    memory_buf_t buf;
    fmt.format_header(tp, lvl, buf);
    return std::string(buf.data(), buf.size());
}

TEST_CASE("format_header produces a fixed-shape header with a tag") {
    formatter fmt{"mytag"};
    auto out = format_one(fmt, log_clock::now(), level::info);

    // Layout (byte offsets): the timestamp separators are fixed, then a space at 25.
    //   [ Y Y Y Y - M M - D D   H H : M M : S S . m m m ]
    //   0 1       5     8       11    14    17    20    24 25
    CHECK(out.front() == '[');
    CHECK(out[5] == '-');
    CHECK(out[8] == '-');
    CHECK(out[11] == ' ');
    CHECK(out[14] == ':');
    CHECK(out[17] == ':');
    CHECK(out[20] == '.');
    CHECK(out[24] == ']');
    CHECK(out[25] == ' ');
    CHECK(contains(out, "[mytag]"));
    CHECK(contains(out, "[INF] "));
    CHECK(out.back() == ' ');  // header always ends with " " (payload appended after)
}

TEST_CASE("format_header omits the tag bracket when the tag is empty") {
    formatter fmt{};
    auto out = format_one(fmt, log_clock::now(), level::warn);

    // [YYYY-MM-DD HH:MM:SS.mmm] [WRN]<space>
    CHECK(out.size() == 32);  // 26 timestamp + "[WRN] "
    CHECK(out.substr(26) == "[WRN] ");
}

TEST_CASE("format_header patches the level label per call") {
    formatter fmt{};
    auto tp = log_clock::now();

    auto a = format_one(fmt, tp, level::trace);
    auto b = format_one(fmt, tp, level::critical);
    auto c = format_one(fmt, tp, level::off);

    CHECK(a.substr(27, 3) == "TRC");
    CHECK(b.substr(27, 3) == "CRT");
    CHECK(c.substr(27, 3) == "OFF");
}

TEST_CASE("level_offset points at the level character (used by color sinks)") {
    // anon header: "[YYYY-MM-DD HH:MM:SS.mmm] [" -> offset 27
    CHECK(formatter{}.level_offset() == 27);

    // tag "abc" inserts "[abc] " before the level bracket -> offset 27 + 6 = 33
    CHECK(formatter{"abc"}.level_offset() == 33);
}

TEST_CASE("set_tag updates the header") {
    formatter fmt{"old"};
    auto out1 = format_one(fmt, log_clock::now(), level::info);
    CHECK(contains(out1, "[old]"));

    fmt.set_tag("new");
    auto out2 = format_one(fmt, log_clock::now(), level::info);
    CHECK(!contains(out2, "[old]"));
    CHECK(contains(out2, "[new]"));
}

TEST_CASE("format_header patches only the millis when within the same second") {
    // Three timestamps in the same second should share the date+time portion and
    // differ only in the millis triplet at offsets 21..23. Exercises the cached-header
    // path that skips localtime() on sub-second updates.
    formatter fmt{};
    auto base = log_clock::time_point{seconds{1700000000}};
    auto a = format_one(fmt, base, level::info);
    auto b = format_one(fmt, base + milliseconds{123}, level::info);
    auto c = format_one(fmt, base + milliseconds{999}, level::info);

    CHECK(a.substr(1, 19) == b.substr(1, 19));
    CHECK(a.substr(1, 19) == c.substr(1, 19));

    CHECK(a.substr(21, 3) == "000");
    CHECK(b.substr(21, 3) == "123");
    CHECK(c.substr(21, 3) == "999");

    auto d = format_one(fmt, base + milliseconds{1000}, level::info);
    auto e = format_one(fmt, base + milliseconds{1027}, level::info);
    CHECK(d.substr(1, 19) == e.substr(1, 19));
    CHECK(d.substr(21, 3) == "000");
    CHECK(e.substr(21, 3) == "027");
}

TEST_CASE("format_options{utc=true} uses gmtime for the timestamp") {
    // 1700000000 = 2023-11-14 22:13:20 UTC. Pin to UTC and verify exact bytes;
    // works regardless of the test runner's local timezone.
    formatter fmt{"", format_options{.utc = true}};
    auto tp = log_clock::time_point{seconds{1700000000}};
    auto out = format_one(fmt, tp, level::info);
    CHECK(out.substr(1, 19) == "2023-11-14 22:13:20");
}

TEST_CASE("format_options{show_date=false} drops the YYYY-MM-DD prefix") {
    formatter fmt{"app", format_options{.show_date = false}};
    auto out = format_one(fmt, log_clock::time_point{seconds{1700000000} + milliseconds{456}}, level::warn);

    // shape: [HH:MM:SS.mmm] [app] [WRN] - first char is '[', position 9 is '.', position 13 is ']'
    CHECK(out[0] == '[');
    CHECK(out[9] == '.');
    CHECK(out[13] == ']');
    CHECK(out.substr(10, 3) == "456");
    CHECK(contains(out, "[app]"));
    CHECK(contains(out, "[WRN] "));
    CHECK(!contains(out, "2023-"));  // no date anywhere
}

TEST_CASE("format_options{precision=none} drops the fractional suffix") {
    formatter fmt{"app", format_options{.precision = time_precision::none}};
    auto a = format_one(fmt, log_clock::time_point{seconds{1700000000}}, level::info);
    auto b = format_one(fmt, log_clock::time_point{seconds{1700000000} + milliseconds{789}}, level::info);

    // shape: [YYYY-MM-DD HH:MM:SS] [app] [INF] - closing ']' at offset 20, no '.' before it
    CHECK(a[20] == ']');
    CHECK(!contains(a, ".000"));
    CHECK(!contains(a, ".789"));
    CHECK(a == b);  // same second, no fractional - identical output
}

TEST_CASE("format_options{precision=us} writes 6 fractional digits") {
    formatter fmt{"", format_options{.precision = time_precision::us}};
    // 1700000000 s + 123456 µs
    auto tp = log_clock::time_point{seconds{1700000000} + microseconds{123456}};
    auto out = format_one(fmt, tp, level::info);
    // header shape: [YYYY-MM-DD HH:MM:SS.uuuuuu] - '.' at 20, 6 digits at 21..26, ']' at 27
    CHECK(out[20] == '.');
    CHECK(out.substr(21, 6) == "123456");
    CHECK(out[27] == ']');
}

TEST_CASE("format_options{precision=ns} writes 9 fractional digits") {
    formatter fmt{"", format_options{.precision = time_precision::ns}};
    // Build from microseconds so the duration converts implicitly into every
    // platform's system_clock::duration (nano on Linux glibc, 100ns on MSVC,
    // micro on macOS libc++). The trailing 3 ns digits land as 000.
    auto tp = log_clock::time_point{seconds{1700000000} + microseconds{123456}};
    auto out = format_one(fmt, tp, level::info);
    // header shape: [YYYY-MM-DD HH:MM:SS.nnnnnnnnn] - '.' at 20, 9 digits at 21..29, ']' at 30
    CHECK(out[20] == '.');
    CHECK(out.substr(21, 9) == "123456000");
    CHECK(out[30] == ']');
}

TEST_CASE("format_options{show_date=false, precision=none} produces time-only header") {
    formatter fmt{"", format_options{.show_date = false, .precision = time_precision::none}};
    auto out = format_one(fmt, log_clock::time_point{seconds{1700000000}}, level::info);
    // shape: "[HH:MM:SS] [INF] " - 17 chars
    CHECK(out.size() == 17);
    CHECK(out[0] == '[');
    CHECK(out[9] == ']');
    CHECK(out.substr(11) == "[INF] ");
}

TEST_CASE("level_offset() reflects the format_options layout") {
    // no date, no fractional, no tag: "[HH:MM:SS] [" -> level char at offset 12
    CHECK(formatter({}, format_options{.show_date = false, .precision = time_precision::none}).level_offset() == 12);
    // no date, with millis, no tag: "[HH:MM:SS.mmm] [" -> level char at offset 16
    CHECK(formatter({}, format_options{.show_date = false}).level_offset() == 16);
    // with date, no fractional, no tag: "[YYYY-MM-DD HH:MM:SS] [" -> level char at offset 23
    CHECK(formatter({}, format_options{.precision = time_precision::none}).level_offset() == 23);
    // with date, ns precision, no tag: "[YYYY-MM-DD HH:MM:SS.nnnnnnnnn] [" -> level char at offset 33
    CHECK(formatter({}, format_options{.precision = time_precision::ns}).level_offset() == 33);
}

TEST_CASE("format_options{show_thread_id=true} adds a 6-digit tid field after the timestamp") {
    formatter fmt{"app", format_options{.show_thread_id = true}};
    auto out = format_one(fmt, log_clock::time_point{seconds{1700000000} + milliseconds{123}}, level::info);

    // shape: "[YYYY-MM-DD HH:MM:SS.mmm] [tttttt] [app] [INF] "
    //         0                     24 26     33 35   39 41
    CHECK(out[24] == ']');
    CHECK(out[26] == '[');
    CHECK(out[33] == ']');
    CHECK(out.substr(35, 5) == "[app]");
    CHECK(contains(out, "[INF] "));

    // Field is six decimal digits.
    const auto tid_field = out.substr(27, 6);
    CHECK(tid_field.size() == 6);
    for (char c : tid_field) CHECK(c >= '0');
    for (char c : tid_field) CHECK(c <= '9');
}

TEST_CASE("show_thread_id is stable per thread") {
    // The tid is cached in a thread_local, so multiple calls from the same thread
    // must produce the same six digits.
    formatter fmt{"", format_options{.show_thread_id = true}};
    auto base = log_clock::time_point{seconds{1700000000}};
    auto a = format_one(fmt, base, level::info);
    auto b = format_one(fmt, base + milliseconds{500}, level::warn);
    // tid field lives at offset 27..32 (anon header)
    CHECK(a.substr(27, 6) == b.substr(27, 6));
}

TEST_CASE("show_thread_id shifts level_offset by 9 bytes") {
    // Adds "[tttttt] " = 9 bytes between timestamp and tag/level brackets.
    const auto base = formatter{}.level_offset();
    const auto with_tid = formatter({}, format_options{.show_thread_id = true}).level_offset();
    CHECK(with_tid == base + 9);

    // Same shift with a tag.
    const auto tagged = formatter{"app"}.level_offset();
    const auto tagged_tid = formatter{"app", format_options{.show_thread_id = true}}.level_offset();
    CHECK(tagged_tid == tagged + 9);
}
