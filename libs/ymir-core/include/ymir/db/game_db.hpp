#pragma once

/**
@file
@brief Game database.

Contains information about specific games that require special handling.
*/

#include <ymir/core/hash.hpp>
#include <ymir/core/types.hpp>

#include <ymir/util/bitmask_enum.hpp>

#include <string_view>

namespace ymir::db {

/// @brief Information about a game in the database.
struct GameInfo {
    /// @brief Required cartridge, tweaks and hacks needed to improve stability
    enum class Flags : uint64 {
        None = 0ull,

        // Required cartridge

        Cart_None = 0ull << 0ull,         ///< No cartridge required
        Cart_DRAM8Mbit = 1ull << 0ull,    ///< 8 Mbit DRAM cartridge required to boot
        Cart_DRAM32Mbit = 2ull << 0ull,   ///< 16 Mbit DRAM cartridge required to boot
        Cart_DRAM48Mbit = 3ull << 0ull,   ///< 32 Mbit DRAM cartridge required to boot
        Cart_ROM_KOF95 = 4ull << 0ull,    ///< The King of Fighters '95 ROM cartridge required to boot
        Cart_ROM_Ultraman = 5ull << 0ull, ///< Ultraman - Hikari no Kyojin Densetsu ROM cartridge required to boot
        Cart_BackupRAM = 6ull << 0ull,    ///< Backup RAM cartridge required for some features

        Cart_MASK = 0b111ull << 0ull, ///< Bitmask for cartridge options

        // Hacks

        ForceSH2Cache = 1ull << 3ull,  ///< SH-2 cache emulation required for the game to work
        FastBusTimings = 1ull << 4ull, ///< Fast bus timings required to fix stability issues
        FastMC68EC000 = 1ull << 5ull,  ///< Overclocked MC68EC000 required to fix stability issues
    };

    Flags flags = Flags::None;        ///< Game compatibility flags
    const char *cartReason = nullptr; ///< Text describing why the cartridge is required

    Flags GetCartridge() const {
        return static_cast<Flags>(static_cast<uint64>(flags) & static_cast<uint64>(Flags::Cart_MASK));
    }
};

/// @brief Retrieves information about a game image given its product code or hash.
///
/// Returns `nullptr` if there is no information for the given product code or hash.
///
/// The product code is prioritized.
///
/// @param[in] productCode the product code to check
/// @return a pointer to `GameInfo` containing information about the game, or `nullptr` if no matching information was
/// found
const GameInfo *GetGameInfo(std::string_view productCode, XXH128Hash hash);

} // namespace ymir::db

ENABLE_BITMASK_OPERATORS(ymir::db::GameInfo::Flags);
