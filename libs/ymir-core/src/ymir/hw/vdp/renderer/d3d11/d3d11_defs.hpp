#pragma once

#include <ymir/hw/vdp/vdp_defs.hpp>

#include <ymir/core/types.hpp>

namespace ymir::vdp::d3d11 {

inline constexpr uint32 kVRAMPageBits = 10;

inline constexpr uint32 kVDP1FBRAMPages = kVDP1FBRAMSize >> kVRAMPageBits;
inline constexpr uint32 kVDP1VRAMPages = kVDP1VRAMSize >> kVRAMPageBits;
inline constexpr uint32 kVDP2VRAMPages = kVDP2VRAMSize >> kVRAMPageBits;

inline constexpr uint32 kColorCacheSize = kVDP2CRAMSize / sizeof(uint16); // in color entries
inline constexpr uint32 kCoeffCacheSize = kVDP2CRAMSize / 2;              // in bytes; top-half only

} // namespace ymir::vdp::d3d11
