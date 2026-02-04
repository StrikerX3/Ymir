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
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
class Direct3D11VDPRenderer;
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
        using type = Direct3D11VDPRenderer;
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
    uint8 palNum = 0;           // Palette number, 7 bits
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

        bitmapData.fill(0);
        bitmapDataAddress = 0xFFFFFFFF;

        lastVCellScroll = 0xFFFFFFFF;
    }

    bool UpdateBitmapDataAddress(uint32 address) {
        address &= ~7;
        if (address != bitmapDataAddress) {
            bitmapDataAddress = address;
            return true;
        }
        return false;
    }

    // Character patterns (for scroll BGs)
    Character currChar;
    Character nextChar;
    uint32 lastCharIndex;
    uint8 lastCellX;

    // Bitmap data (for bitmap BGs)
    alignas(uint64) std::array<uint8, 8> bitmapData;
    uint32 bitmapDataAddress;

    // Vertical cell scroll data
    uint32 lastVCellScroll;
};

/// @brief NBG layer state, including coordinate counters, increments and addresses.
struct NormBGLayerState {
    NormBGLayerState() {
        Reset();
    }

    void Reset() {
        fracScrollX = 0;
        fracScrollY = 0;
        scrollIncH = 0x100;
        lineScrollTableAddress = 0;
        vertCellScrollOffset = 0;
        vertCellScrollDelay = 0;
        vertCellScrollRepeat = 0;
        mosaicCounterY = 0;
    }

    // Initial fractional X scroll coordinate.
    uint32 fracScrollX;

    // Fractional Y scroll coordinate.
    // Reset at the start of every frame and updated every scanline.
    uint32 fracScrollY;

    // Vertical scroll amount with 8 fractional bits.
    // Initialized at VBlank OUT.
    // Derived from SCYINn and SCYDNn
    uint32 scrollAmountV;

    // Fractional X scroll coordinate increment.
    // Applied every pixel and updated every scanline.
    uint32 scrollIncH;

    // Current line scroll table address.
    // Reset at the start of every frame and incremented every 1/2/4/8/16 lines.
    uint32 lineScrollTableAddress;

    // Vertical cell scroll offset.
    // Only valid for NBG0 and NBG1.
    // Based on CYCA0/A1/B0/B1 parameters.
    uint32 vertCellScrollOffset;

    // Is the vertical cell scroll read delayed by one cycle?
    // Only valid for NBG0 and NBG1.
    // Based on CYCA0/A1/B0/B1 parameters.
    bool vertCellScrollDelay;

    // Is the first vertical cell scroll entry repeated?
    // Only valid for NBG0.
    // Based on CYCA0/A1/B0/B1 parameters.
    bool vertCellScrollRepeat;

    // Vertical mosaic counter.
    // Reset at the start of every frame and incremented every line.
    // The value is mod mosaicV.
    uint8 mosaicCounterY;
};

/// @brief Rotation Parameters A and B counters and coordinates.
struct RotationParamState {
    RotationParamState() {
        Reset();
    }

    void Reset() {
        Xst = Yst = 0;
        KA = 0;
    }

    // Current base screen coordinates (signed 13.10 fixed point), updated every scanline.
    sint32 Xst, Yst;

    // Current base coefficient address (unsigned 16.10 fixed point), updated every scanline.
    uint32 KA;
};

/// @brief LNCL and BACK screen colors for the current scanline.
struct LineBackLayerState {
    LineBackLayerState() {
        Reset();
    }

    void Reset() {
        lineColor.u32 = 0;
        backColor.u32 = 0;
    }

    Color888 lineColor;
    Color888 backColor;
};

} // namespace ymir::vdp
