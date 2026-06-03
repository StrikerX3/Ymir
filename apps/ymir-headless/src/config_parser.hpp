#pragma once

#include "config.hpp"

#include <toml++/toml.hpp>
#include <ymir/debug/util/env.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace ymir::debug {

namespace detail {

    struct CliConfig {
        std::optional<std::filesystem::path> ipl_path;
        std::optional<std::filesystem::path> game_path;
        std::optional<std::filesystem::path> bram_path;
        std::optional<std::filesystem::path> config_path;
        std::optional<bool> slave_enabled;
    };

    static constexpr std::string_view kYmirConfigName = "Ymir.toml";
    static constexpr std::string_view kDbgConfigName = "Ymir-dbg.toml";

    /// @brief Checks if a TOML key belongs to the headless configuration subset.
    /// This is used to warn users about keys that are ignored in headless mode.
    inline constexpr bool IsHeadlessConfigKey(std::string_view key) {
        return key == "ipl_path" || key == "game_path" || key == "bram_path" || key == "slave_enabled";
    }

    /// @brief Parses a minimal subset of CLI flags into a CliConfig struct.
    /// This manual parser is used for the headless worker to avoid SDL/UI dependencies.
    inline CliConfig ParseCliConfig(int argc, char *argv[]) {
        CliConfig cli;
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg{argv[i]};
            auto readPath = [&](std::optional<std::filesystem::path> &out) {
                if (i + 1 < argc) {
                    out = std::filesystem::path{argv[++i]};
                }
            };

            if (arg == "--ipl") {
                readPath(cli.ipl_path);
            } else if (arg == "--game") {
                readPath(cli.game_path);
            } else if (arg == "--config") {
                readPath(cli.config_path);
            } else if (arg == "--bram") {
                readPath(cli.bram_path);
            } else if (arg == "--slave") {
                cli.slave_enabled = true;
            } else if (arg == "--no-slave") {
                cli.slave_enabled = false;
            }
        }
        return cli;
    }

    /// @brief Resolves the standard platform-specific configuration directory.
    /// Follows SDL_GetPrefPath patterns: APPDATA on Win32, AppSupport on macOS, XDG on Linux.
    inline std::optional<std::filesystem::path> GetStandardConfigDir() {
#if defined(_WIN32)
        if (auto appdata = ymir::debug::util::EnvGetPath("APPDATA")) {
            return *appdata / "StrikerX3" / "Ymir";
        }
#elif defined(__APPLE__)
        if (auto home = ymir::debug::util::EnvGetPath("HOME")) {
            return *home / "Library" / "Application Support" / "Ymir";
        }
#else
        if (auto xdg = ymir::debug::util::EnvGetPath("XDG_CONFIG_HOME")) {
            return *xdg / "Ymir";
        }
        if (auto home = ymir::debug::util::EnvGetPath("HOME")) {
            return *home / ".config" / "Ymir";
        }
#endif
        return std::nullopt;
    }

    inline std::optional<std::filesystem::path> StandardYmirConfigPath() {
        auto dir = GetStandardConfigDir();
        return dir ? std::optional{(*dir) / kYmirConfigName} : std::nullopt;
    }

    inline std::optional<std::filesystem::path> StandardDbgConfigPath() {
        auto dir = GetStandardConfigDir();
        return dir ? std::optional{(*dir) / kDbgConfigName} : std::nullopt;
    }

    /// @brief Resolves the authoritative path for Ymir.toml using ENV -> Standard -> CWD priority.
    inline std::optional<std::filesystem::path> ResolveYmirPath() {
        if (auto envPath = ymir::debug::util::EnvGetPath("YMIR_CONFIG")) {
            if (std::filesystem::is_regular_file(*envPath)) {
                return envPath;
            }
        }
        if (auto stdPath = StandardYmirConfigPath(); stdPath && std::filesystem::is_regular_file(*stdPath)) {
            return stdPath;
        }
        if (std::filesystem::is_regular_file(kYmirConfigName)) {
            return std::filesystem::path{kYmirConfigName};
        }
        return std::nullopt;
    }

    /// @brief Resolves the authoritative path for Ymir-dbg.toml using Standard -> CWD priority.
    inline std::optional<std::filesystem::path> ResolveDbgPath() {
        if (auto stdPath = StandardDbgConfigPath(); stdPath && std::filesystem::is_regular_file(*stdPath)) {
            return stdPath;
        }
        if (std::filesystem::is_regular_file(kDbgConfigName)) {
            return std::filesystem::path{kDbgConfigName};
        }
        return std::nullopt;
    }

    /// @brief Loads a configuration file and populates the provided HeadlessConfig.
    /// Handles both TOML_EXCEPTIONS compilation modes for cross-platform robustness.
    inline bool LoadConfigFile(const std::filesystem::path &path, HeadlessConfig &config) {
        toml::table table;
#if TOML_EXCEPTIONS
        try {
            table = toml::parse_file(path.native());
        } catch (const toml::parse_error &error) {
            std::cerr << "ymir-headless: failed to parse config '" << path.string() << "': " << error.description()
                      << '\n';
            return false;
        }
#else
        auto result = toml::parse_file(path.native());
        if (!result) {
            std::cerr << "ymir-headless: failed to parse config '" << path.string()
                      << "': " << result.error().description() << '\n';
            return false;
        }
        table = std::move(result).table();
#endif

        for (const auto &[key, value] : table) {
            if (!value.is_table() && !value.is_array() && !IsHeadlessConfigKey(key.str())) {
                std::cerr << "ymir-headless: ignoring unknown config key '" << key.str() << "'\n";
            }
        }

        if (auto val = table["ipl_path"].value<std::string>()) {
            config.ipl_path = *val;
        }
        if (auto val = table["game_path"].value<std::string>()) {
            config.game_path = std::filesystem::path{*val};
        }
        if (auto val = table["bram_path"].value<std::string>()) {
            config.bram_path = std::filesystem::path{*val};
        }
        if (auto val = table["slave_enabled"].value<bool>()) {
            config.slave_enabled = *val;
        }
        return true;
    }

    /// @brief Overrides configuration fields with values provided via CLI.
    inline void ApplyCliConfig(const CliConfig &cli, HeadlessConfig &config) {
        if (cli.ipl_path) {
            config.ipl_path = *cli.ipl_path;
        }
        if (cli.game_path) {
            config.game_path = cli.game_path;
        }
        if (cli.bram_path) {
            config.bram_path = cli.bram_path;
        }
        if (cli.slave_enabled) {
            config.slave_enabled = *cli.slave_enabled;
        }
    }

    /// @brief Saves the debug-specific subset of configuration to a file.
    inline bool SaveDbgConfig(const HeadlessConfig &config, const std::filesystem::path &path) {
        std::error_code ec;
        const auto parentPath = path.parent_path();
        if (!parentPath.empty()) {
            std::filesystem::create_directories(parentPath, ec);
            if (ec) {
                return false;
            }
        }

        toml::table table;
        table.insert_or_assign("ipl_path", config.ipl_path.string());
        if (config.game_path) {
            table.insert_or_assign("game_path", config.game_path->string());
        }
        if (config.bram_path) {
            table.insert_or_assign("bram_path", config.bram_path->string());
        }
        table.insert_or_assign("slave_enabled", config.slave_enabled);

        std::ofstream out{path};
        if (!out) {
            return false;
        }
        out << table;
        return true;
    }

} // namespace detail

/// @brief Validates that the minimum required configuration for booting is present.
inline bool ValidateConfig(const HeadlessConfig &config) {
    if (config.config_load_failed) {
        std::cerr << "ymir-headless: configuration failed to load\n";
        return false;
    }
    if (config.ipl_path.empty()) {
        std::cerr << "ymir-headless: IPL path is required; provide --ipl or set ipl_path in Ymir.toml\n";
        return false;
    }
    if (!std::filesystem::is_regular_file(config.ipl_path)) {
        std::cerr << "ymir-headless: IPL file not found: '" << config.ipl_path.string() << "'\n";
        return false;
    }
    return true;
}

/// @brief Loads the final HeadlessConfig by merging defaults, config files, and CLI flags.
/// Hierarchical merge order: CLI args > Ymir-dbg.toml > Ymir.toml > Defaults.
/// This ensures local developer overrides are respected without mutating the main SDL3 config.
inline HeadlessConfig LoadConfig(int argc, char *argv[]) {
    HeadlessConfig config;
    const auto cli = detail::ParseCliConfig(argc, argv);

    if (cli.config_path) {
        if (std::filesystem::is_regular_file(*cli.config_path)) {
            config.config_load_failed = !detail::LoadConfigFile(*cli.config_path, config);
        } else {
            std::cerr << "ymir-headless: config file not found: " << cli.config_path->string() << '\n';
            config.config_load_failed = true;
        }
    } else {
        // Cascade: load the base global config, then overlay the local debug overrides.
        if (auto path = detail::ResolveYmirPath()) {
            config.config_load_failed |= !detail::LoadConfigFile(*path, config);
        }
        if (auto path = detail::ResolveDbgPath()) {
            config.config_load_failed |= !detail::LoadConfigFile(*path, config);
        }
    }

    detail::ApplyCliConfig(cli, config);
    return config;
}

} // namespace ymir::debug
