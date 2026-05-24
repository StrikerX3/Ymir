#include <catch2/catch_test_macros.hpp>

#include <config_parser.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <ymir/debug/util/env.hpp>

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

    void Set(const std::filesystem::path &path) const {
        ymir::debug::util::EnvSet(m_name, path.string());
    }

    void Unset() const {
        ymir::debug::util::EnvUnset(m_name);
    }

private:
    const char *m_name;
    std::optional<std::string> m_prior;
};

class TempConfigFile {
public:
    explicit TempConfigFile(std::string_view contents)
        : m_path(std::filesystem::temp_directory_path() /
                 ("ymir-headless-config-test-" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".toml")) {
        std::ofstream out{m_path};
        out << contents;
    }

    ~TempConfigFile() {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path &Path() const {
        return m_path;
    }

private:
    std::filesystem::path m_path;
};

ymir::debug::HeadlessConfig LoadWithArgs(std::vector<std::string> args) {
    std::vector<char *> argv;
    argv.reserve(args.size());
    for (auto &arg : args) {
        argv.push_back(arg.data());
    }
    return ymir::debug::LoadConfig(static_cast<int>(argv.size()), argv.data());
}

} // namespace

TEST_CASE("LoadConfig lets CLI IPL override config file", "[config]") {
    ScopedEnvVar env{"YMIR_CONFIG"};
    env.Unset();
    TempConfigFile configFile{R"(ipl_path = "config-bios.bin")"};

    auto config = LoadWithArgs({"ymir-headless", "--config", configFile.Path().string(), "--ipl", "cli-bios.bin"});

    CHECK(config.ipl_path == std::filesystem::path{"cli-bios.bin"});
}

TEST_CASE("LoadConfig lets CLI slave flag override config file", "[config]") {
    ScopedEnvVar env{"YMIR_CONFIG"};
    env.Unset();
    TempConfigFile configFile{R"(
ipl_path = "bios.bin"
slave_enabled = true
)"};

    auto config = LoadWithArgs({"ymir-headless", "--config", configFile.Path().string(), "--no-slave"});

    CHECK(config.ipl_path == std::filesystem::path{"bios.bin"});
    CHECK_FALSE(config.slave_enabled);
}

TEST_CASE("LoadConfig returns empty IPL path when not configured", "[config]") {
    ScopedEnvVar env{"YMIR_CONFIG"};
    env.Unset();
    TempConfigFile configFile{R"(slave_enabled = false)"};

    auto config = LoadWithArgs({"ymir-headless", "--config", configFile.Path().string()});

    CHECK(config.ipl_path.empty());
    CHECK_FALSE(config.slave_enabled);
}

TEST_CASE("LoadConfig ignores unknown config keys", "[config]") {
    ScopedEnvVar env{"YMIR_CONFIG"};
    env.Unset();
    TempConfigFile configFile{R"(
ipl_path = "bios.bin"
unknown_key = "ignored"
)"};

    auto config = LoadWithArgs({"ymir-headless", "--config", configFile.Path().string()});

    CHECK(config.ipl_path == std::filesystem::path{"bios.bin"});
}

TEST_CASE("LoadConfig reads YMIR_CONFIG when no explicit config path is provided", "[config]") {
    ScopedEnvVar env{"YMIR_CONFIG"};
    TempConfigFile configFile{R"(ipl_path = "env-bios.bin")"};
    env.Set(configFile.Path());

    auto config = LoadWithArgs({"ymir-headless"});

    CHECK(config.ipl_path == std::filesystem::path{"env-bios.bin"});
}

TEST_CASE("LoadConfig defaults slave SH-2 to enabled", "[config]") {
    ScopedEnvVar env{"YMIR_CONFIG"};
    env.Unset();
    TempConfigFile configFile{R"(ipl_path = "bios.bin")"};

    auto config = LoadWithArgs({"ymir-headless", "--config", configFile.Path().string()});

    CHECK(config.slave_enabled);
}

TEST_CASE("ValidateConfig returns true when ipl_path is non-empty", "[config]") {
    TempConfigFile configFile{"ipl_path = \"test.bin\""};
    ymir::debug::HeadlessConfig config;
    config.ipl_path = configFile.Path();
    CHECK(ymir::debug::ValidateConfig(config));
}

TEST_CASE("ValidateConfig returns false when ipl_path is empty", "[config]") {
    ymir::debug::HeadlessConfig config;
    CHECK_FALSE(ymir::debug::ValidateConfig(config));
}

TEST_CASE("ValidateConfig returns false when ipl_path is a directory", "[config]") {
    ymir::debug::HeadlessConfig config;
    config.ipl_path = std::filesystem::temp_directory_path();
    CHECK_FALSE(ymir::debug::ValidateConfig(config));
}
