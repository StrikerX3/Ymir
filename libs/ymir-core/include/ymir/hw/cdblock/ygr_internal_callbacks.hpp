#pragma once

/**
@file
@brief Internal callback definitions used by the YGR.
*/

#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::cdblock {

/// @brief Invoked when a sector transfer is finished
using CBSectorTransferDone = util::RequiredCallback<void()>;

} // namespace ymir::cdblock
