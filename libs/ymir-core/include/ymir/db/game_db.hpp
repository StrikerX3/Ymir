#pragma once

/**
@file
@brief Game database.

Contains information about specific games that require special handling.
*/

#include <ymir/core/hash.hpp>

#include <string_view>

namespace ymir::db {

/// @brief The cartridge required for the game to work
enum class Cartridge { None, DRAM8Mbit, DRAM32Mbit, DRAM48Mbit, ROM_KOF95, ROM_Ultraman, BackupRAM };

/// @brief Information about a game in the database.
struct GameInfo {
    Cartridge cartridge = Cartridge::None; ///< Cartridge required for the game to work
    const char *cartReason = nullptr;      ///< Text describing why the cartridge is required
    bool sh2Cache = false;                 ///< SH-2 cache emulation required for the game to work
    bool fastBusTimings = false;           ///< Fast bus timings required to fix stability issues
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
