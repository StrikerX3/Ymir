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

/// @brief Cartridge required to boot a game or to make certain functions work.
enum class Cartridge : uint8 { None, DRAM8Mbit, DRAM32Mbit, DRAM48Mbit, ROM_KOF95, ROM_Ultraman, BackupRAM };

/// @brief Information about a game in the database.
struct GameInfo {
    /// @brief Required cartridge, tweaks and hacks needed to improve stability
    enum class Flags : uint64 {
        None = 0ull,

        // Required cartridge. Must match the Cartridge enum.
        Cart_SHIFT = 0ull,                  ///< Bit shift for cartridge options
        Cart_MASK = 0b111ull << Cart_SHIFT, ///< Bitmask for cartridge options

#define CART_VAL(name) Cart_##name = static_cast<uint64>(Cartridge::name) << Cart_SHIFT

        CART_VAL(None),         ///< No cartridge required
        CART_VAL(DRAM8Mbit),    ///< 8 Mbit DRAM cartridge required to boot
        CART_VAL(DRAM32Mbit),   ///< 32 Mbit DRAM cartridge required to boot
        CART_VAL(DRAM48Mbit),   ///< 48 Mbit DRAM cartridge required to boot
        CART_VAL(ROM_KOF95),    ///< The King of Fighters '95 ROM cartridge required to boot
        CART_VAL(ROM_Ultraman), ///< Ultraman - Hikari no Kyojin Densetsu ROM cartridge required to boot
        CART_VAL(BackupRAM),    ///< Backup RAM cartridge required for some features

#undef CART_VAL

        // Hacks

        ForceSH2Cache = 1ull << 3ull,                   ///< SH-2 cache emulation required for the game to work
        FastBusTimings = 1ull << 4ull,                  ///< Fast bus timings required to fix stability issues
        FastMC68EC000 = 1ull << 5ull,                   ///< Overclocked MC68EC000 required to fix stability issues
        StallVDP1OnVRAMWrites = 1ull << 6ull,           ///< Stall/slow down VDP1 drawing on VDP1 VRAM writes
        SlowVDP1 = 1ull << 7ull,                        ///< Slow down VDP1 processing overall
        RelaxedVDP2BitmapCPAccessChecks = 1ull << 8ull, ///< Allow bitmap CP accesses during SH2 cycles

        // Proper fixes for each flag:
        // - ForceSH2Cache: SH-2 cache emulation *is* the accurate choice, so the flag should stay as is
        //   - it simply forces-on the cache emulation option, which isn't needed by most games and introduces a
        //     noticeable performance penalty for no gain.
        // - FastBusTimings: advanced bus timing emulation
        // - FastMC68EC000: advanced bus timing emulation on the SH2 and SCSP sides, probably
        // - StallVDP1OnVRAMWrites: advanced bus timing emulation plus accurate VDP1 timings
        // - SlowVDP1: accurate VDP1 timings
        // - RelaxedVDP2BitmapCPAccessChecks: in some cases, SCU DMA timings; generally speaking, advanced bus timings
    };

    Flags flags = Flags::None;        ///< Game compatibility flags
    const char *cartReason = nullptr; ///< Text describing why the cartridge is required

    Cartridge GetCartridge() const {
        return static_cast<Cartridge>(static_cast<uint64>(flags) & static_cast<uint64>(Flags::Cart_MASK));
    }
};

/// @brief Retrieves information about a game image given its product code or hash.
///
/// Returns `nullptr` if there is no information for the given product code or hash.
///
/// The product code is prioritized.
///
/// @param[in] productCode the product code to check
/// @param[in] hash the disc hash to check
/// @return a pointer to `GameInfo` containing information about the game, or `nullptr` if no matching information was
/// found
const GameInfo *GetGameInfo(std::string_view productCode, XXH128Hash hash);

} // namespace ymir::db

ENABLE_BITMASK_OPERATORS(ymir::db::GameInfo::Flags);
