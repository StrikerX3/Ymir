#pragma once

/**
@file
@brief Internal callback definitions used by the SH-1.
*/

#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::sh1 {

/// @brief Receive a bit from one of the SH-1's SCI channels.
using CbSerialRx = util::RequiredCallback<bool()>;

/// @brief Send a bit to one of the SH-1's SCI channels.
using CbSerialTx = util::RequiredCallback<void(bool bit)>;

/// @brief Invoked to raise the IRQ6 signal on the SH-1.
using CBAssertIRQ6 = util::RequiredCallback<void()>;

} // namespace ymir::sh1
