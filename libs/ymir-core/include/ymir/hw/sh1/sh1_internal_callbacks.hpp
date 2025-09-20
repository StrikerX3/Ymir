#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::sh1 {

// Receive a bit from one of the SH-1's SCI channels.
using CbSerialRx = util::RequiredCallback<bool()>;

// Send a bit to one of the SH-1's SCI channels.
using CbSerialTx = util::RequiredCallback<void(bool bit)>;

} // namespace ymir::sh1
