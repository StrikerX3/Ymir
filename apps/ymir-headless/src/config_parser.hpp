#pragma once

#include "config.hpp"

#include <toml++/toml.hpp>
#include <ymir/debug/util/env.hpp>

#include <filesystem>
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

    inline constexpr bool IsHeadlessConfigKey(std::string_view key) {
        return key == "ipl_path" || key == "game_path" || key == "bram_path" || key == "slave_enabled";
    }

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

    inline std::optional<std::filesystem::path> StandardConfigPath() {
#if defined(_WIN32)
        if (auto appdata = ymir::debug::util::EnvGet("APPDATA")) {
            return std::filesystem::path{*appdata} / "StrikerX3" / "Ymir" / "Ymir.toml";
        }
#elif defined(__APPLE__)
        if (auto home = ymir::debug::util::EnvGet("HOME")) {
            return std::filesystem::path{*home} / "Library" / "Application Support" / "Ymir" / "Ymir.toml";
        }
#else
        if (auto xdgConfigHome = ymir::debug::util::EnvGet("XDG_CONFIG_HOME")) {
            return std::filesystem::path{*xdgConfigHome} / "Ymir" / "Ymir.toml";
        }
        if (auto home = ymir::debug::util::EnvGet("HOME")) {
            return std::filesystem::path{*home} / ".config" / "Ymir" / "Ymir.toml";
        }
#endif
        return std::nullopt;
    }

    inline std::optional<std::filesystem::path> FindConfigPath(const std::optional<std::filesystem::path> &cliPath) {
        if (cliPath) {
            if (std::filesystem::is_regular_file(*cliPath)) {
                return cliPath;
            }
            std::cerr << "ymir-headless: config file not found: " << cliPath->string() << '\n';
            return std::nullopt;
        }

        if (auto envPath = ymir::debug::util::EnvGet("YMIR_CONFIG")) {
            const std::filesystem::path path{*envPath};
            if (std::filesystem::is_regular_file(path)) {
                return path;
            }
        }

        if (auto path = StandardConfigPath(); path && std::filesystem::is_regular_file(*path)) {
            return path;
        }

        const std::filesystem::path cwdConfig{"Ymir.toml"};
        if (std::filesystem::is_regular_file(cwdConfig)) {
            return cwdConfig;
        }

        return std::nullopt;
    }

    inline bool LoadConfigFile(const std::filesystem::path &path, HeadlessConfig &config) {
        toml::table table;
        // toml++ parse API differs between exceptions and noex compilation modes.
        // The headless binary compiles with TOML_EXCEPTIONS=0 (propagated from ymir-core PUBLIC).
        // The test binary does not link ymir-core so compiles with TOML_EXCEPTIONS=1 (default).
#if TOML_EXCEPTIONS
        try {
            table = toml::parse_file(path.native());
        } catch (const toml::parse_error &error) {
            std::cerr << "ymir-headless: failed to parse config '" << path.string() << "': " << error.description()
                      << '\n';
            // Return false to propagate the error up
            return false;
        }
#else
        auto result = toml::parse_file(path.native());
        if (!result) {
            std::cerr << "ymir-headless: failed to parse config '" << path.string()
                      << "': " << result.error().description() << '\n';
            // Return false to propagate the error up
            return false;
        }
        table = std::move(result).table();
#endif

        for (const auto &[key, value] : table) {
            // Silently skip table sections (SDL3, video, etc.) — they belong to other frontends.
            // Only warn for unexpected scalar keys in the flat headless namespace.
            if (!value.is_table() && !value.is_array() && !IsHeadlessConfigKey(key.str())) {
                std::cerr << "ymir-headless: ignoring unknown config key '" << key.str() << "'\n";
            }
        }

        if (auto value = table["ipl_path"].value<std::string>()) {
            config.ipl_path = *value;
        }
        if (auto value = table["game_path"].value<std::string>()) {
            config.game_path = std::filesystem::path{*value};
        }
        if (auto value = table["bram_path"].value<std::string>()) {
            config.bram_path = std::filesystem::path{*value};
        }
        if (auto value = table["slave_enabled"].value<bool>()) {
            config.slave_enabled = *value;
        }
        return true;
    }

    inline void ApplyCliConfig(const CliConfig &cli, HeadlessConfig &config) {
        if (cli.ipl_path) {
            config.ipl_path = *cli.ipl_path;
        }
        if (cli.game_path) {
            config.game_path = *cli.game_path;
        }
        if (cli.bram_path) {
            config.bram_path = *cli.bram_path;
        }
        if (cli.slave_enabled) {
            config.slave_enabled = *cli.slave_enabled;
        }
    }

} // namespace detail

inline bool ValidateConfig(const HeadlessConfig &config) {
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

inline HeadlessConfig LoadConfig(int argc, char *argv[]) {
    HeadlessConfig config;
    const auto cli = detail::ParseCliConfig(argc, argv);

    if (auto configPath = detail::FindConfigPath(cli.config_path)) {
        // Load config file if found, but ignore parse errors and proceed to CLI overrides
        detail::LoadConfigFile(*configPath, config);
    }

    detail::ApplyCliConfig(cli, config);
    return config;
}

} // namespace ymir::debug
