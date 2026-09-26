// SPDX-License-Identifier: MIT

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "helpers.h"
#include "spdlite/logger.h"
#include "spdlite/sinks/file_sink.h"

using namespace spdlite;
using helpers::contains;
namespace fs = std::filesystem;

static std::string read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

TEST_CASE("file_sink creates the file on construction") {
    helpers::tmpdir td{"file_create"};
    const auto path = td / "out.txt";
    {
        file_sink sink{path};
    }
    CHECK(fs::exists(path));
}

TEST_CASE("file_sink auto-creates parent directories") {
    helpers::tmpdir td{"file_parents"};
    const auto path = td.path() / "a" / "b" / "c" / "out.txt";
    {
        file_sink sink{path};
    }
    CHECK(fs::exists(path));
    CHECK(fs::is_directory(td.path() / "a" / "b" / "c"));
}

TEST_CASE("file_sink truncate mode discards existing contents") {
    helpers::tmpdir td{"file_trunc"};
    const auto path = td / "out.txt";
    {
        std::ofstream pre(path, std::ios::binary);
        pre << "preexisting\n";
    }
    REQUIRE(read_all(path) == "preexisting\n");

    {
        logger_st<file_sink> log{file_sink{path, open_mode::truncate}};
        log.info("fresh");
    }

    auto contents = read_all(path);
    CHECK(!contains(contents, "preexisting"));
    CHECK(contains(contents, "fresh"));
}

TEST_CASE("file_sink append mode preserves existing contents") {
    helpers::tmpdir td{"file_append"};
    const auto path = td / "out.txt";
    {
        std::ofstream pre(path, std::ios::binary);
        pre << "preexisting\n";
    }

    {
        logger_st<file_sink> log{file_sink{path}};  // default = append
        log.info("added");
    }

    auto contents = read_all(path);
    CHECK(contains(contents, "preexisting"));
    CHECK(contains(contents, "added"));
}

TEST_CASE("file_sink writes the formatted line (header + payload + newline)") {
    helpers::tmpdir td{"file_write"};
    const auto path = td / "out.txt";
    {
        logger_st<file_sink> log{file_sink{path, open_mode::truncate}};
        log.set_tag("app");
        log.info("hello");
        log.warn("there");
    }
    auto contents = read_all(path);
    CHECK(contains(contents, "[app]"));
    CHECK(contains(contents, "[INF] hello"));
    CHECK(contains(contents, "[WRN] there"));
    CHECK(contents.back() == '\n');
}

TEST_CASE("file_sink throws when the path cannot be opened") {
    // Force a failure by making the parent path traverse through an existing regular file:
    // create_directories then fails with filesystem_error (a std::runtime_error subclass).
    helpers::tmpdir td{"file_bad"};
    const auto blocker = td / "blocker";
    {
        std::ofstream o(blocker, std::ios::binary);
        o << "x";
    }
    const auto bad_path = blocker / "child" / "out.txt";
    CHECK_THROWS_AS(file_sink{bad_path}, std::runtime_error);
}
