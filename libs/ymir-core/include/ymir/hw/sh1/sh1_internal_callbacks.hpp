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

/// @brief Invoked to raise an IRQ signal on the SH-1.
using CBAssertIRQ = util::RequiredCallback<void()>;

/// @brief Invoked to set the DREQ0/1# signals on the SH-1.
using CBSetDREQn = util::RequiredCallback<void(bool level)>;

/// @brief Invoked to step a DMA channel on the SH-1.
using CBStepDMAC = util::RequiredCallback<void()>;

} // namespace ymir::sh1
