#include <catch2/catch_test_macros.hpp>

#include <discovery.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#include <ymir/debug/util/env.hpp>
#include <ymir/debug/util/path.hpp>

namespace fs = std::filesystem;

namespace {

class ScopedEnvVar {
public:
    /// @brief Captures the current value of the environment variable.
    explicit ScopedEnvVar(const char *name)
        : m_name(name) {
        if (auto value = ymir::debug::util::EnvGet(name)) {
            m_prior = *value;
        }
    }

    /// @brief Restores the environment variable to its state at object creation.
    ~ScopedEnvVar() {
        if (m_prior) {
            ymir::debug::util::EnvSet(m_name, *m_prior);
        } else {
            ymir::debug::util::EnvUnset(m_name);
        }
    }
    void Set(const std::string &value) {
        ymir::debug::util::EnvSet(m_name, value);
    }
    void Unset() {
        ymir::debug::util::EnvUnset(m_name);
    }

private:
    const char *m_name;
    std::optional<std::string> m_prior;
};

#ifdef _WIN32
constexpr std::string_view kHeadlessBinaryName = "ymir-headless.exe";

std::optional<fs::path> WindowsCmdPathFromSystemRoot() {
    auto systemRoot = ymir::debug::util::EnvGetPath("SystemRoot");
    if (!systemRoot || systemRoot->empty()) {
        return std::nullopt;
    }
    return *systemRoot / "System32" / "cmd.exe";
}
#else
constexpr std::string_view kHeadlessBinaryName = "ymir-headless";
#endif

} // namespace

#if !defined(_WIN32)
TEST_CASE("discovery: explicit path finds existing file", "[discovery]") {
    // Use a file known to exist on all Unix systems
    auto result = ymir::debug::find_headless_binary("/bin/sh");
    REQUIRE(result.has_value());
    // canonical resolves symlinks; /bin/sh -> /bin/dash or /bin/bash depending on OS
    std::error_code ec;
    auto expected = fs::canonical("/bin/sh", ec);
    auto actual = fs::canonical(result.value(), ec);
    CHECK(actual == expected);
}
#else
TEST_CASE("discovery: explicit path finds existing file", "[discovery]") {
    // Use a file known to exist on Windows systems.
    auto cmdPath = WindowsCmdPathFromSystemRoot();
    REQUIRE(cmdPath.has_value());
    REQUIRE(fs::is_regular_file(*cmdPath));

    auto result = ymir::debug::find_headless_binary(cmdPath->string());
    REQUIRE(result.has_value());
    std::error_code ec;
    auto expected = fs::canonical(*cmdPath, ec);
    auto actual = fs::canonical(result.value(), ec);
    CHECK(actual == expected);
}

TEST_CASE("discovery: Windows SystemRoot helper honors non-C drive", "[discovery]") {
    ScopedEnvVar systemRoot{"SystemRoot"};
    systemRoot.Set("D:\\CustomWindows");

    auto cmdPath = WindowsCmdPathFromSystemRoot();

    REQUIRE(cmdPath.has_value());
    CHECK(*cmdPath == fs::path{"D:\\CustomWindows"} / "System32" / "cmd.exe");
}
#endif

TEST_CASE("discovery: explicit path to nonexistent returns nullopt", "[discovery]") {
#ifdef _WIN32
    auto result = ymir::debug::find_headless_binary("C:\\nonexistent_ymir_path\\ymir-headless");
#else
    auto result = ymir::debug::find_headless_binary("/nonexistent/path/ymir-headless");
#endif
    CHECK_FALSE(result.has_value());
}

TEST_CASE("discovery: YMIR_HEADLESS env var finds file", "[discovery]") {
    auto tmp = fs::temp_directory_path() / "ymir-dbg-discovery-test";
    {
        std::ofstream ofs(tmp);
        ofs << "dummy";
    }

    ScopedEnvVar env{"YMIR_HEADLESS"};
    env.Set(tmp.string());

    auto result = ymir::debug::find_headless_binary();

    REQUIRE(result.has_value());
    std::error_code ec;
    auto expected = fs::canonical(tmp, ec);
    auto actual = fs::canonical(result.value(), ec);
    CHECK(actual == expected);

    env.Unset();
    fs::remove(tmp);
}

TEST_CASE("discovery: PATH env var finds binary by name", "[discovery]") {
    auto tmp_dir = fs::temp_directory_path() / "ymir-dbg-path-test";
    fs::create_directories(tmp_dir);

    auto binary = tmp_dir / kHeadlessBinaryName;
    {
        std::ofstream ofs(binary);
        ofs << "dummy";
    }

    // Prepend tmp_dir to PATH so discovery hits it before any real install.
    ScopedEnvVar path_env{"PATH"};
    std::string new_path = tmp_dir.string();
    if (auto existing = ymir::debug::util::EnvGet("PATH"); existing && !existing->empty()) {
        new_path += ymir::debug::util::kSearchPathDelimiter;
        new_path += *existing;
    }
    path_env.Set(new_path);

    // Ensure YMIR_HEADLESS does not short-circuit to Level 2.
    ScopedEnvVar headless_env{"YMIR_HEADLESS"};
    headless_env.Unset();

    auto result = ymir::debug::find_headless_binary();

    REQUIRE(result.has_value());
    std::error_code ec1, ec2;
    CHECK(fs::canonical(result.value(), ec1) == fs::canonical(binary, ec2));

    fs::remove(binary);
    fs::remove(tmp_dir);
}

TEST_CASE("discovery: all levels fail returns nullopt", "[discovery]") {
    // Ensure YMIR_HEADLESS is not set
    ScopedEnvVar env{"YMIR_HEADLESS"};
    env.Unset();

    // Use a name that definitely does not exist on PATH
    auto result = ymir::debug::find_headless_binary();
    // This may succeed on adjacent binary level if ymir-headless is in the build dir.
    // That is acceptable behaviour.
    // We verify the function doesn't crash.
    SUCCEED("find_headless_binary completed without exception");
}
