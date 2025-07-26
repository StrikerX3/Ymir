#pragma once

/**
@file
@brief Debugger callbacks.
*/

#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::debug {

/// @brief Invoked when a debug break signal is raised.
using CBDebugBreakRaised = util::OptionalCallback<void(/* TODO: const DebugBreakInfo &info */)>;

} // namespace ymir::debug
