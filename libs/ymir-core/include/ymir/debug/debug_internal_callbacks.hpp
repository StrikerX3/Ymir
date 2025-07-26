#pragma once

/**
@file
@brief Internal callback definitions used by the debugger.
*/

#include <ymir/util/callback.hpp>

namespace ymir::debug {

/// @brief Raises the debug break signal on the system, causing emulation to be suspended as soon as possible.
using CBRaiseDebugBreak = util::RequiredCallback<void(/* const DebugBreakInfo &info */)>;

} // namespace ymir::debug
