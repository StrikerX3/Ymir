#pragma once

#include <ymir/hw/vdp/vdp_defs.hpp>

#include <ymir/core/types.hpp>

namespace ymir::vdp::d3d11 {

inline constexpr uint32 kVRAMPageBits = 12;

inline constexpr uint32 kVDP1FBRAMPages = kVDP1FramebufferRAMSize >> kVRAMPageBits;
inline constexpr uint32 kVDP1VRAMPages = kVDP1VRAMSize >> kVRAMPageBits;
inline constexpr uint32 kVDP2VRAMPages = kVDP2VRAMSize >> kVRAMPageBits;

inline constexpr uint32 kVDP1PolyAtlasH = 2048;
inline constexpr uint32 kVDP1PolyAtlasV = 2048;

static_assert(kVDP1PolyAtlasH >= kVDP1MaxFBSizeH);
static_assert(kVDP1PolyAtlasV >= kVDP1MaxFBSizeV);

inline constexpr uint32 kColorCacheSize = kVDP2CRAMSize / sizeof(uint16); // in color entries
inline constexpr uint32 kCoeffCacheSize = kVDP2CRAMSize / 2;              // in bytes; top-half only

} // namespace ymir::vdp::d3d11
