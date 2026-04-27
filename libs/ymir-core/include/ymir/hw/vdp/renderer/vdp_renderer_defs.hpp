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
union Character {
    uint32 u32 = 0;
    struct {
        uint32 charNum : 15;      //  0-14  Character number
        uint32 : 1;               //    15  <unused>
        uint32 palNum : 7;        // 16-22  Palette number
        uint32 : 5;               // 23-27  <unused>
        uint32 specColorCalc : 1; //    28  Special color calculation
        uint32 specPriority : 1;  //    29  Special priority
        uint32 flipH : 1;         //    30  Horizontal flip
        uint32 flipV : 1;         //    31  Vertical flip
    };
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
