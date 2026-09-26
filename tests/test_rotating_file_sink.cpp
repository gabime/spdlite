// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include "helpers.h"
#include "spdlite/logger.h"
#include "spdlite/sinks/rotating_file_sink.h"

using namespace spdlite;
namespace fs = std::filesystem;

static void touch(const fs::path& p, std::string_view content = "x") {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << content;
}

TEST_CASE("max_size=0 throws") {
    helpers::tmpdir td{"rot_zero"};
    CHECK_THROWS_AS(rotating_file_sink(td / "x.txt", 0, 1), std::invalid_argument);
}

TEST_CASE("empty base_filename throws std::invalid_argument") {
    CHECK_THROWS_AS(rotating_file_sink(fs::path{}, 1024, 1), std::invalid_argument);
}

TEST_CASE("max_files greater than max_files_limit throws") {
    helpers::tmpdir td{"rot_limit"};
    CHECK_THROWS_AS(rotating_file_sink(td / "x.txt", 1024, rotating_file_sink::max_files_limit + 1), std::invalid_argument);
}

TEST_CASE("max_files_limit constant matches documented value") { CHECK(rotating_file_sink::max_files_limit == 1000); }

TEST_CASE("rotating_file_sink creates the active file") {
    helpers::tmpdir td{"rot_create"};
    const auto path = td / "app.txt";
    {
        rotating_file_sink sink{path, 1024, 3};
    }
    CHECK(fs::exists(path));
}

TEST_CASE("rotation produces an archive once max_size is exceeded") {
    helpers::tmpdir td{"rot_basic"};
    const auto path = td / "app.txt";
    {
        // 4 ~55-byte log lines exceed the 128-byte cap on the 3rd write, producing exactly one
        // rotation. max_files=10 ensures the resulting app.1.txt isn't pruned out of the window.
        logger_st<rotating_file_sink> log{rotating_file_sink{path, 128, 10}};
        for (int i = 0; i < 4; ++i) log.info("padding line number {}", i);
    }

    CHECK(fs::exists(path));
    CHECK(fs::exists(td / "app.1.txt"));
}

TEST_CASE("max_files window keeps only the newest N archives") {
    helpers::tmpdir td{"rot_window"};
    const auto path = td / "app.txt";
    {
        // many rotations under a window of 2; only the two newest archives should survive
        logger_st<rotating_file_sink> log{rotating_file_sink{path, 128, 2}};
        for (int i = 0; i < 60; ++i) log.info("padding line number {}", i);
    }

    CHECK(fs::exists(path));

    // For "app.N.txt": path.stem() = "app.N", path.stem().extension() = ".N".
    // Check that only N newest files exist
    std::size_t archive_count = 0;
    std::size_t highest = 0;
    for (const auto& entry : fs::directory_iterator(td.path())) {
        if (entry.path().filename() == "app.txt") continue;
        ++archive_count;
        const auto dot_n = entry.path().stem().extension().string();  // ".N"
        if (dot_n.size() > 1) highest = std::max<std::size_t>(highest, std::stoul(dot_n.substr(1)));
    }
    CHECK(archive_count == 2);
    CHECK(fs::exists(td / ("app." + std::to_string(highest) + ".txt")));
    CHECK(fs::exists(td / ("app." + std::to_string(highest - 1) + ".txt")));
}

TEST_CASE("scan_and_prune drops archives below the keep window on construction") {
    helpers::tmpdir td{"rot_prune"};
    const auto path = td / "app.txt";

    // pre-populate stale archives
    touch(td / "app.1.txt");
    touch(td / "app.2.txt");
    touch(td / "app.3.txt");
    touch(td / "app.4.txt");
    touch(td / "app.5.txt");

    {
        // max_files=2 so keep window is [highest-1, highest] = [4, 5]
        rotating_file_sink sink{path, 1024, 2};
    }

    CHECK_FALSE(fs::exists(td / "app.1.txt"));
    CHECK_FALSE(fs::exists(td / "app.2.txt"));
    CHECK_FALSE(fs::exists(td / "app.3.txt"));
    CHECK(fs::exists(td / "app.4.txt"));
    CHECK(fs::exists(td / "app.5.txt"));
}

TEST_CASE("counter resumes across restarts") {
    helpers::tmpdir td{"rot_resume"};
    const auto path = td / "app.txt";

    // pre-populate as if a previous run rotated to .5
    touch(td / "app.5.txt");

    {
        // construct, write enough to trigger one rotation
        logger_st<rotating_file_sink> log{rotating_file_sink{path, 128, 10}};
        for (int i = 0; i < 10; ++i) log.info("padding line number {}", i);
    }

    // next archive after .5 should be .6 (counter resumed)
    CHECK(fs::exists(td / "app.6.txt"));
}

TEST_CASE("base filename with no parent directory still works") {
    // The scan_and_prune code special-cases an empty parent path; exercise that branch
    // by passing a bare filename (cwd-relative). Cleanup is manual since we're not in a tmpdir.
    const auto unique =
        std::string{"spdlite_rot_noparent_"} + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const fs::path base = unique + ".log";
    {
        rotating_file_sink sink{base, 1024, 1};
    }
    CHECK(fs::exists(base));
    std::error_code ec;
    fs::remove(base, ec);
}

TEST_CASE("max_files=0 bounds the active file and keeps no archives") {
    helpers::tmpdir td{"rot_zero_archives"};
    const auto path = td / "app.txt";
    {
        // many rotations under a window of 0 - every rename is immediately undone by the delete
        logger_st<rotating_file_sink> log{rotating_file_sink{path, 128, 0}};
        for (int i = 0; i < 30; ++i) log.info("padding line number {}", i);
    }
    CHECK(fs::exists(path));
    CHECK(fs::file_size(path) <= 128);
    for (const auto& entry : fs::directory_iterator(td.path())) {
        CHECK(entry.path().filename() == "app.txt");
    }
}

TEST_CASE("base filename with no extension is handled") {
    helpers::tmpdir td{"rot_no_ext"};
    const auto path = td / "app";  // no extension

    // pre-populate archives in the no-extension scheme: "app.<N>"
    touch(td / "app.1");
    touch(td / "app.2");
    touch(td / "app.3");

    {
        // window=[2,3]
        rotating_file_sink sink{path, 1024, 2};
    }

    CHECK_FALSE(fs::exists(td / "app.1"));
    CHECK(fs::exists(td / "app.2"));
    CHECK(fs::exists(td / "app.3"));
}

TEST_CASE("base stem containing dots is parsed correctly") {
    helpers::tmpdir td{"rot_dotted_stem"};
    const auto path = td / "app.foo.txt";  // stem is "app.foo"

    touch(td / "app.foo.1.txt");
    touch(td / "app.foo.2.txt");
    touch(td / "app.foo.3.txt");

    {
        rotating_file_sink sink{path, 1024, 2};
    }

    CHECK_FALSE(fs::exists(td / "app.foo.1.txt"));
    CHECK(fs::exists(td / "app.foo.2.txt"));
    CHECK(fs::exists(td / "app.foo.3.txt"));
}

TEST_CASE("non-archive files in the directory are not misidentified") {
    helpers::tmpdir td{"rot_lookalikes"};
    const auto path = td / "app.txt";

    // these should all survive a scan_and_prune with a tight window
    touch(td / "app.txt.bak");  // wrong extension
    touch(td / "appx.5.txt");   // wrong stem prefix
    touch(td / "app.foo.txt");  // ".foo" segment is not digits
    touch(td / "other.5.txt");  // unrelated stem

    // real archives - give the scanner something to actually prune
    touch(td / "app.1.txt");
    touch(td / "app.2.txt");
    touch(td / "app.3.txt");

    {
        // window=[3,3]
        rotating_file_sink sink{path, 1024, 1};
    }

    CHECK_FALSE(fs::exists(td / "app.1.txt"));
    CHECK_FALSE(fs::exists(td / "app.2.txt"));
    CHECK(fs::exists(td / "app.3.txt"));

    CHECK(fs::exists(td / "app.txt.bak"));
    CHECK(fs::exists(td / "appx.5.txt"));
    CHECK(fs::exists(td / "app.foo.txt"));
    CHECK(fs::exists(td / "other.5.txt"));
}

TEST_CASE("existing active file size is preserved across construction") {
    helpers::tmpdir td{"rot_preserve_size"};
    const auto path = td / "app.txt";

    // pre-populate active file with 60 bytes
    const std::string preexisting(60, 'A');
    touch(path, preexisting);

    rotating_file_sink sink{path, 100, 2};

    // a single 50-byte write. without size preservation, 0+50<=100 -> no rotation.
    // with preservation, 60+50=110 > 100 -> rotation moves the 60 'A's into app.1.txt.
    const std::string line(50, 'B');
    log_msg msg{log_clock::time_point{}, "r", level::info, line, line, 0};
    sink.write(msg);
    sink.flush();

    REQUIRE(fs::exists(td / "app.1.txt"));
    CHECK(fs::file_size(td / "app.1.txt") == 60);
    CHECK(fs::file_size(path) == 50);
}

TEST_CASE("symlinks in the log directory are ignored by the archive scanner") {
    helpers::tmpdir td{"rot_symlink"};
    helpers::tmpdir target_dir{"rot_symlink_target"};
    const auto path = td / "app.txt";

    // a regular file living outside the log directory, named like an archive
    const auto target = target_dir / "secret.txt";
    touch(target, "do not delete me");

    // plant a symlink inside the log dir that looks like an archive
    const auto link = td / "app.5.txt";
    std::error_code link_ec;
    fs::create_symlink(target, link, link_ec);
    if (link_ec) {
        MESSAGE("create_symlink not supported on this platform; skipping");
        return;
    }

    {
        // window=[5,5] if the symlink were treated as an archive; the next archive
        // would land at .6.txt. with the symlink correctly ignored, counter starts
        // fresh and the next archive lands at .1.txt.
        logger_st<rotating_file_sink> log{rotating_file_sink{path, 128, 1}};
        for (int i = 0; i < 4; ++i) log.info("padding line number {}", i);
    }

    CHECK(fs::exists(target));
    CHECK(fs::is_symlink(link));
    CHECK(fs::exists(td / "app.1.txt"));
    CHECK_FALSE(fs::exists(td / "app.6.txt"));
}

TEST_CASE("subdirectory named like an archive is ignored") {
    helpers::tmpdir td{"rot_subdir"};
    const auto path = td / "app.txt";

    // a subdirectory whose name matches the archive pattern
    fs::create_directories(td / "app.5.txt");
    touch(td / "app.5.txt" / "inside.txt", "child");

    {
        // if scanner mistakenly treated the dir as archive idx=5, next archive
        // would be .6.txt. correct behavior: dir ignored, next archive is .1.txt.
        logger_st<rotating_file_sink> log{rotating_file_sink{path, 128, 1}};
        for (int i = 0; i < 4; ++i) log.info("padding line number {}", i);
    }

    CHECK(fs::is_directory(td / "app.5.txt"));
    CHECK(fs::exists(td / "app.5.txt" / "inside.txt"));
    CHECK(fs::exists(td / "app.1.txt"));
    CHECK_FALSE(fs::exists(td / "app.6.txt"));
}

TEST_CASE("archive index exceeding SIZE_MAX is ignored without overflow") {
    helpers::tmpdir td{"rot_overflow"};
    const auto path = td / "app.txt";

    // 30 nines: way beyond SIZE_MAX (uint64 max is 20 digits). parse must reject
    // this without producing UB or a wrap-around index that aliases a real archive.
    const std::string huge(30, '9');
    touch(td / ("app." + huge + ".txt"));
    // and exactly SIZE_MAX (per parse_archive_index_, max_idx itself is rejected)
    touch(td / "app.18446744073709551615.txt");

    {
        // both lookalikes must be ignored; counter starts at 1.
        logger_st<rotating_file_sink> log{rotating_file_sink{path, 128, 1}};
        for (int i = 0; i < 4; ++i) log.info("padding line number {}", i);
    }

    CHECK(fs::exists(td / ("app." + huge + ".txt")));
    CHECK(fs::exists(td / "app.18446744073709551615.txt"));
    CHECK(fs::exists(td / "app.1.txt"));
}

TEST_CASE("archive names with non-digit tails are ignored") {
    helpers::tmpdir td{"rot_nondigit"};
    const auto path = td / "app.txt";

    // all of these must be rejected by parse_archive_index_
    touch(td / "app.+5.txt");  // leading '+'
    touch(td / "app.-5.txt");  // leading '-'
    touch(td / "app. 5.txt");  // leading space
    touch(td / "app.5a.txt");  // trailing non-digit
    touch(td / "app..txt");    // empty digit run
    touch(td / "app.5..txt");  // two dots

    // one real archive to give the scanner something to actually find
    touch(td / "app.7.txt");

    {
        rotating_file_sink sink{path, 1024, 1};
    }

    CHECK(fs::exists(td / "app.+5.txt"));
    CHECK(fs::exists(td / "app.-5.txt"));
    CHECK(fs::exists(td / "app. 5.txt"));
    CHECK(fs::exists(td / "app.5a.txt"));
    CHECK(fs::exists(td / "app..txt"));
    CHECK(fs::exists(td / "app.5..txt"));
    // the real archive is the only one the scanner recognizes; with window=[7,7]
    // it stays untouched.
    CHECK(fs::exists(td / "app.7.txt"));
}

TEST_CASE("write landing exactly on max_size does not rotate") {
    helpers::tmpdir td{"rot_boundary"};
    const auto path = td / "app.txt";

    rotating_file_sink sink{path, 100, 2};

    // two 50-byte writes: total = 100, exactly at cap. strict > check -> no rotation.
    const std::string line(50, 'X');
    log_msg msg{log_clock::time_point{}, "r", level::info, line, line, 0};
    sink.write(msg);
    sink.write(msg);
    sink.flush();

    CHECK_FALSE(fs::exists(td / "app.1.txt"));
    CHECK(fs::file_size(path) == 100);

    // one more byte tips us over.
    const std::string single(1, 'Y');
    log_msg one{log_clock::time_point{}, "r", level::info, single, single, 0};
    sink.write(one);
    sink.flush();

    REQUIRE(fs::exists(td / "app.1.txt"));
    CHECK(fs::file_size(td / "app.1.txt") == 100);
    CHECK(fs::file_size(path) == 1);
}
