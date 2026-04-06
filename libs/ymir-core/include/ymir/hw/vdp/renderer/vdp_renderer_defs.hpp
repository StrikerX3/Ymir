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
};

/// @brief Retrieves the name of a given VDP renderer type.
/// @param[in] type the VDP renderer type
/// @return a string with the human-readable name of the VDP renderer
inline std::string_view GetRendererName(VDPRendererType type) {
    switch (type) {
    case VDPRendererType::Null: return "Null";
    case VDPRendererType::Software: return "Software";
    default: return "Invalid";
    }
}

/// @brief All supported VDP renderer types.
inline constexpr VDPRendererType kRendererTypes[] = {
    VDPRendererType::Null,
    VDPRendererType::Software,
};

// Forward declarations of concrete VDP renderer implementations.
// See the vdp_renderer_* headers.

class NullVDPRenderer;
class SoftwareVDPRenderer;

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
