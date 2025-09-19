#pragma once

/**
@file
@brief Internal callback definitions used by the YGR.
*/

#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::cdblock {

/// @brief Invoked when the YGR raises IRQ6 on the SH-1.
using CBAssertIRQ6 = util::RequiredCallback<void()>;

} // namespace ymir::cdblock
