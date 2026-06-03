#include <catch2/catch_test_macros.hpp>

#include <config_parser.hpp>

#include <cstdint>
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
        : m_name(name)
        , m_prior(ymir::debug::util::EnvGet(name)) {}

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

TEST_CASE("LoadConfig falls back when YMIR_CONFIG points to a missing file", "[config]") {
    ScopedEnvVar envHome{"HOME"};
    ScopedEnvVar envXdg{"XDG_CONFIG_HOME"};
    ScopedEnvVar envYmirConfig{"YMIR_CONFIG"};
#if defined(_WIN32)
    ScopedEnvVar envAppdata{"APPDATA"};
#endif

    envHome.Unset();
    envXdg.Unset();
    envYmirConfig.Set(std::filesystem::temp_directory_path() / "missing-ymir-config.toml");

    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "ymir-test-missing-env";
    std::filesystem::path configDir = tempDir;
#if defined(__APPLE__)
    configDir = tempDir / "Library" / "Application Support" / "Ymir";
    envHome.Set(tempDir);
#elif defined(_WIN32)
    configDir = tempDir / "StrikerX3" / "Ymir";
    envAppdata.Set(tempDir);
#else
    configDir = tempDir / ".config" / "Ymir";
    envHome.Set(tempDir);
#endif

    std::filesystem::create_directories(configDir);
    {
        std::ofstream out{configDir / "Ymir.toml"};
        out << "ipl_path = \"standard-bios.bin\"\n";
    }

    auto config = LoadWithArgs({"ymir-headless"});

    CHECK_FALSE(config.config_load_failed);
    CHECK(config.ipl_path == std::filesystem::path{"standard-bios.bin"});

    std::filesystem::remove_all(tempDir);
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

TEST_CASE("ValidateConfig returns false when config load failed", "[config]") {
    TempConfigFile configFile{"ipl_path = \"test.bin\""};
    ymir::debug::HeadlessConfig config;
    config.config_load_failed = true;
    config.ipl_path = configFile.Path();
    CHECK_FALSE(ymir::debug::ValidateConfig(config));
}

TEST_CASE("LoadConfig marks missing explicit config as failed even when CLI supplies IPL", "[config]") {
    auto missingPath = std::filesystem::temp_directory_path() / "missing-explicit-ymir-config.toml";

    auto config = LoadWithArgs({"ymir-headless", "--config", missingPath.string(), "--ipl", "cli-bios.bin"});

    CHECK(config.config_load_failed);
    CHECK(config.ipl_path == std::filesystem::path{"cli-bios.bin"});
    CHECK_FALSE(ymir::debug::ValidateConfig(config));
}

TEST_CASE("LoadConfig marks malformed explicit config as failed", "[config]") {
    TempConfigFile configFile{"ipl_path ="};

    auto config = LoadWithArgs({"ymir-headless", "--config", configFile.Path().string()});

    CHECK(config.config_load_failed);
    CHECK_FALSE(ymir::debug::ValidateConfig(config));
}

TEST_CASE("LoadConfig implements hierarchical cascade", "[config]") {
    // Isolate from actual environment
    ScopedEnvVar envHome{"HOME"};
    ScopedEnvVar envXdg{"XDG_CONFIG_HOME"};
    ScopedEnvVar envYmirConfig{"YMIR_CONFIG"};
#if defined(_WIN32)
    ScopedEnvVar envAppdata{"APPDATA"};
#endif

    envHome.Unset();
    envXdg.Unset();
    envYmirConfig.Unset();

    // Create a temp directory to act as the "standard" config root
    std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "ymir-test-cascade";
    std::filesystem::path configDir = tempDir;
#if defined(__APPLE__)
    configDir = tempDir / "Library" / "Application Support" / "Ymir";
    envHome.Set(tempDir);
#elif defined(_WIN32)
    configDir = tempDir / "StrikerX3" / "Ymir";
    envAppdata.Set(tempDir);
#else
    configDir = tempDir / ".config" / "Ymir";
    envHome.Set(tempDir);
#endif

    std::filesystem::create_directories(configDir);

    SECTION("Ymir-dbg.toml overrides Ymir.toml") {
        {
            std::ofstream out{configDir / "Ymir.toml"};
            out << "ipl_path = \"base-bios.bin\"\n";
        }
        {
            std::ofstream out{configDir / "Ymir-dbg.toml"};
            out << "ipl_path = \"dbg-bios.bin\"\n";
        }

        auto config = LoadWithArgs({"ymir-headless"});
        CHECK(config.ipl_path == std::filesystem::path{"dbg-bios.bin"});
    }

    SECTION("Ymir.toml is used when Ymir-dbg.toml is absent") {
        {
            std::ofstream out{configDir / "Ymir.toml"};
            out << "ipl_path = \"base-bios.bin\"\n";
        }

        auto config = LoadWithArgs({"ymir-headless"});
        CHECK(config.ipl_path == std::filesystem::path{"base-bios.bin"});
    }

    SECTION("CLI overrides both config files") {
        {
            std::ofstream out{configDir / "Ymir.toml"};
            out << "ipl_path = \"base-bios.bin\"\n";
        }
        {
            std::ofstream out{configDir / "Ymir-dbg.toml"};
            out << "ipl_path = \"dbg-bios.bin\"\n";
        }

        auto config = LoadWithArgs({"ymir-headless", "--ipl", "cli-bios.bin"});
        CHECK(config.ipl_path == std::filesystem::path{"cli-bios.bin"});
    }

    SECTION("--config bypasses cascade") {
        {
            std::ofstream out{configDir / "Ymir.toml"};
            out << "ipl_path = \"base-bios.bin\"\n";
        }
        TempConfigFile otherConfig{"ipl_path = \"other-bios.bin\""};

        auto config = LoadWithArgs({"ymir-headless", "--config", otherConfig.Path().string()});
        CHECK(config.ipl_path == std::filesystem::path{"other-bios.bin"});
    }

    std::filesystem::remove_all(tempDir);
}

TEST_CASE("SaveDbgConfig writes headless-owned keys", "[config]") {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "ymir-save-test.toml";

    ymir::debug::HeadlessConfig config;
    config.ipl_path = "save-bios.bin";
    config.game_path = "save-game.cue";
    config.bram_path = "save-bram.bin";
    config.slave_enabled = false;

    REQUIRE(ymir::debug::detail::SaveDbgConfig(config, path));

    // Read back and verify
    ymir::debug::HeadlessConfig loaded;
    REQUIRE(ymir::debug::detail::LoadConfigFile(path, loaded));

    CHECK(loaded.ipl_path == config.ipl_path);
    REQUIRE(loaded.game_path.has_value());
    CHECK(*loaded.game_path == *config.game_path);
    REQUIRE(loaded.bram_path.has_value());
    CHECK(*loaded.bram_path == *config.bram_path);
    CHECK(loaded.slave_enabled == config.slave_enabled);

    std::filesystem::remove(path);
}

TEST_CASE("SaveDbgConfig writes a bare relative path", "[config]") {
    std::filesystem::path path = "ymir-save-relative-test.toml";

    ymir::debug::HeadlessConfig config;
    config.ipl_path = "save-bios.bin";

    REQUIRE(ymir::debug::detail::SaveDbgConfig(config, path));
    CHECK(std::filesystem::is_regular_file(path));

    std::filesystem::remove(path);
}
