#pragma once

/**
@file
@brief IPL ROM database.
*/

#include <ymir/core/hash.hpp>

namespace ymir::db {

/// @brief Sega Saturn system variants.
enum class SystemVariant { None, Saturn, HiSaturn, VSaturn, DevKit };

/// @brief The regions supported by the Sega Saturn.
enum class SystemRegion { None, US_EU, JP, KR };

/// @brief Information about an IPL ROM in the database.
struct IPLROMInfo {
    const char *version;   ///< IPL ROM version string
    uint16_t year;         ///< Year of release
    uint8_t month;         ///< Month of release
    uint8_t day;           ///< Day of release
    SystemVariant variant; ///< System variant which included this IPL ROM
    SystemRegion region;   ///< System region for which this IPL ROM was designed
    bool regionFree;       ///< Whether this is a region-free IPL ROM
};

/// @brief Retrieves information about an IPL ROM image given its XXH128 hash.
///
/// Returns `nullptr` if there is no information for the given hash.
///
/// @param[in] hash the IPL ROM hash to check
/// @return a pointer to `IPLROMInfo` containing information about the IPL ROM, or `nullptr` if no matching ROM was
/// found
const IPLROMInfo *GetIPLROMInfo(XXH128Hash hash);

} // namespace ymir::db
