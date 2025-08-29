#pragma once

/**
@file
@brief Common watchpoint definitions.
*/

#include <ymir/util/bitmask_enum.hpp>

#include <ymir/core/types.hpp>

namespace ymir::debug {

/// @brief Watchpoint flags.
enum class WatchpointFlags : uint8 {
    None = 0u,

    Read8 = (1u << 0u),   ///< Break on 8-bit reads
    Read16 = (1u << 1u),  ///< Break on 16-bit reads
    Read32 = (1u << 2u),  ///< Break on 32-bit reads
    Write8 = (1u << 3u),  ///< Break on 8-bit writes
    Write16 = (1u << 4u), ///< Break on 16-bit writes
    Write32 = (1u << 5u), ///< Break on 32-bit writes
};

} // namespace ymir::debug

ENABLE_BITMASK_OPERATORS(ymir::debug::WatchpointFlags);
