#pragma once

/**
@file
@brief VDP callbacks.
*/

#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::vdp {

/// @brief Invoked when the VDP1 finishes drawing a frame.
using CBVDP1DrawFinished = util::OptionalCallback<void()>;

/// @brief Invoked when the VDP1 swaps framebuffers.
using CBVDP1FramebufferSwap = util::OptionalCallback<void()>;

/// @brief Invoked when the VDP2 resolution is changed.
/// @param[in] width the new framebuffer width
/// @param[in] height the new framebuffer height
using CBVDP2ResolutionChanged = util::OptionalCallback<void(uint32 width, uint32 height)>;

/// @brief Invoked when the VDP2 finishes drawing a frame.
using CBVDP2DrawFinished = util::OptionalCallback<void()>;

} // namespace ymir::vdp
