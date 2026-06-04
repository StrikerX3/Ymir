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

    Read = (1u << 0u),  ///< Break on reads
    Write = (1u << 1u), ///< Break on writes
};

} // namespace ymir::debug

ENABLE_BITMASK_OPERATORS(ymir::debug::WatchpointFlags);
