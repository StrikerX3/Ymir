#pragma once

/**
@file
@brief VDP callbacks.
*/

#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::vdp {

// Invoked when the VDP1 finishes drawing a frame.
using CBVDP1DrawFinished = util::OptionalCallback<void()>;

// Invoked when the VDP1 swaps framebuffers.
using CBVDP1FramebufferSwap = util::OptionalCallback<void()>;

// Invoked when the VDP2 finishes drawing a frame.
using CBVDP2DrawFinished = util::OptionalCallback<void()>;

} // namespace ymir::vdp
