#pragma once

/**
@file
@brief CD Block ROM database.
*/

#include <ymir/core/hash.hpp>

#include <string_view>

namespace ymir::db {

/// @brief Information about a CD Block ROM in the database.
struct CDBlockROMInfo {
    std::string_view version; ///< CD Block ROM version string
};

/// @brief Retrieves information about a CD Block ROM image given its XXH128 hash.
///
/// Returns `nullptr` if there is no information for the given hash.
///
/// @param[in] hash the CD Block ROM hash to check
/// @return a pointer to `CDBlockROMInfo` containing information about the CD Block ROM, or `nullptr` if no matching ROM
/// was found
const CDBlockROMInfo *GetCDBlockROMInfo(XXH128Hash hash);

} // namespace ymir::db
