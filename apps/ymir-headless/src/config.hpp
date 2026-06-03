#pragma once

#include <filesystem>
#include <optional>

namespace ymir::debug {

// Resolved configuration for a headless Saturn instance.
// Populated by merging Ymir.toml safe-subset keys with CLI flag overrides;
// CLI flags always win. DebugService receives this struct, not raw argc/argv.
struct HeadlessConfig {
    // Set when an explicit or discovered config file exists but cannot be
    // loaded. ValidateConfig rejects this before checking boot paths.
    bool config_load_failed{false};

    // Empty path means not yet resolved; DebugService must reject this.
    std::filesystem::path ipl_path;

    // Absent = boot to IPL shell without a game disc.
    std::optional<std::filesystem::path> game_path;

    // Absent = use the standard platform path shared with the SDL3 frontend.
    // Do NOT run the SDL3 frontend and headless simultaneously with the same
    // BRAM path; no cross-process coordination exists and concurrent writes corrupt.
    std::optional<std::filesystem::path> bram_path;

    bool slave_enabled{true};
};

} // namespace ymir::debug
