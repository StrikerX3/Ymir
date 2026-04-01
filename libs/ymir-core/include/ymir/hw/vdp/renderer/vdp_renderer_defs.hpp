#pragma once

#include <ymir/hw/vdp/vdp_defs.hpp>

#include <ymir/core/types.hpp>

#include <array>
#include <string_view>

namespace ymir::vdp {

/// @brief VDP renderer type enumeration.
enum class VDPRendererType {
    Null,
    Software,
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    Direct3D11,
#endif
};

/// @brief Retrieves the name of a given VDP renderer type.
/// @param[in] type the VDP renderer type
/// @return a string with the human-readable name of the VDP renderer
inline std::string_view GetRendererName(VDPRendererType type) {
    switch (type) {
    case VDPRendererType::Null: return "Null";
    case VDPRendererType::Software: return "Software";
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    case VDPRendererType::Direct3D11: return "Direct3D 11";
#endif
    default: return "Invalid";
    }
}

/// @brief All supported VDP renderer types.
inline constexpr VDPRendererType kRendererTypes[] = {
    VDPRendererType::Null,
    VDPRendererType::Software,
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    VDPRendererType::Direct3D11,
#endif
};

// Forward declarations of concrete VDP renderer implementations.
// See the vdp_renderer_* headers.

class NullVDPRenderer;
class SoftwareVDPRenderer;
class HardwareVDPRendererBase;
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
namespace d3d11 {
    class Direct3D11VDPRenderer;
}
#endif

namespace detail {

    /// @brief Metadata about VDP renderer types.
    /// @tparam type the VDP renderer type
    template <VDPRendererType type>
    struct VDPRendererTypeMeta {};

    /// @brief Metadata about the null VDP renderer.
    template <>
    struct VDPRendererTypeMeta<VDPRendererType::Null> {
        using type = NullVDPRenderer;
    };

    /// @brief Metadata about the software VDP renderer.
    template <>
    struct VDPRendererTypeMeta<VDPRendererType::Software> {
        using type = SoftwareVDPRenderer;
    };

#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    /// @brief Metadata about the Direct3D 11 VDP renderer.
    template <>
    struct VDPRendererTypeMeta<VDPRendererType::Direct3D11> {
        using type = d3d11::Direct3D11VDPRenderer;
    };
#endif

    /// @brief Retrieves the class type of the given `VDPRendererType`.
    /// @tparam type the VDP renderer type
    template <VDPRendererType type>
    using VDPRendererType_t = typename VDPRendererTypeMeta<type>::type;

} // namespace detail

// -----------------------------------------------------------------------------

/// @brief Describes a Pattern Name Data entry - parameters for a character or tile.
struct Character {
    uint16 charNum = 0;         // Character number, 15 bits
    uint16 palNum = 0;          // Palette number, 7 bits (shifted left by 4 for optimized rendering performance)
    bool specColorCalc = false; // Special color calculation
    bool specPriority = false;  // Special priority
    bool flipH = false;         // Horizontal flip
    bool flipV = false;         // Vertical flip
};

/// @brief Pipelined VDP2 VRAM fetcher. Used by tile and bitmap data.
struct VRAMFetcher {
    VRAMFetcher() {
        Reset();
    }

    void Reset() {
        currChar = {};
        nextChar = {};
        lastCharIndex = 0xFFFFFFFF;

        charData.fill(0);
        charDataAddress = 0xFFFFFFFF;

        lastVCellScroll = 0xFFFFFFFF;
    }

    bool UpdateCharacterDataAddress(uint32 address) {
        address &= ~7;
        if (address != charDataAddress) {
            charDataAddress = address;
            return true;
        }
        return false;
    }

    // Character patterns (for scroll BGs)
    Character currChar;
    Character nextChar;
    uint32 lastCharIndex;
    uint8 lastCellX;

    // Character data (scroll and bitmap BGs)
    alignas(uint64) std::array<uint8, 8> charData;
    uint32 charDataAddress;

    // Vertical cell scroll data
    uint32 lastVCellScroll;
};

} // namespace ymir::vdp
