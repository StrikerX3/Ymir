#pragma once

/**
@file
@brief SH2 debugger definitions.
*/

#include <ymir/core/types.hpp>

namespace ymir::debug {

/// @brief Possible types of known values pushed to the stack.
enum class SH2StackValueType : uint8 { GBR, VBR, SR, MACH, MACL, PR };

} // namespace ymir::debug
