#pragma once

/**
@file
@brief General definitions common to VDP1 and VDP2.
*/

#include "vdp1_defs.hpp"
#include "vdp2_defs.hpp"

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/size_ops.hpp>

#include <array>
#include <concepts>

namespace ymir::vdp {

// -----------------------------------------------------------------------------
// Memory region sizes

inline constexpr std::size_t kVDP1VRAMSize = 512_KiB;
inline constexpr std::size_t kVDP1FramebufferRAMSize = 256_KiB;
inline constexpr std::size_t kVDP2VRAMSize = 512_KiB;
inline constexpr std::size_t kVDP2CRAMSize = 4_KiB;

// -----------------------------------------------------------------------------
// Common constants

inline constexpr uint32 kMaxResH = 704; // Maximum horizontal resolution
inline constexpr uint32 kMaxResV = 512; // Maximum vertical resolution

// -----------------------------------------------------------------------------
// Colors

union Color555 {
    uint16 u16;
    struct {
        uint16 r : 5;
        uint16 g : 5;
        uint16 b : 5;
        uint16 msb : 1; // CC in CRAM, transparency in cells when using RGB format
    };
};
static_assert(sizeof(Color555) == sizeof(uint16));

union Color888 {
    uint32 u32;
    struct {
        uint32 r : 8;
        uint32 g : 8;
        uint32 b : 8;
        uint32 : 7;
        uint32 msb : 1; // CC in CRAM, transparency in cells when using RGB format
    };
};
static_assert(sizeof(Color888) == sizeof(uint32));

// Gets the truncated-average between two RGB888 pixels
// Averages the unused "alpha" channel as well
FORCE_INLINE Color888 AverageRGB888(Color888 lhs, Color888 rhs) {
    return Color888{.u32 = (((lhs.u32 ^ rhs.u32) & 0xFEFEFEFE) >> 1u) + (lhs.u32 & rhs.u32)};
}

FORCE_INLINE Color888 ConvertRGB555to888(Color555 color) {
    return Color888{
        .r = static_cast<uint32>(color.r) << 3u,
        .g = static_cast<uint32>(color.g) << 3u,
        .b = static_cast<uint32>(color.b) << 3u,
        .msb = color.msb,
    };
}

// -----------------------------------------------------------------------------
// Coordinates

template <std::integral T>
struct Coord {
    static_assert(std::is_trivial_v<T> && std::is_standard_layout_v<T>);

    std::array<T, 2> elements;

    T &x() {
        return elements[0];
    }

    T &y() {
        return elements[1];
    }

    const T &x() const {
        return elements[0];
    }

    const T &y() const {
        return elements[1];
    }
};

template <std::size_t I, std::integral T>
FORCE_INLINE constexpr auto &get(Coord<T> &coord) noexcept {
    return std::get<I>(coord.elements);
}

template <std::size_t I, std::integral T>
FORCE_INLINE constexpr const auto &get(const Coord<T> &coord) noexcept {
    return std::get<I>(coord.elements);
}

template <std::size_t I, std::integral T>
FORCE_INLINE constexpr auto &&get(Coord<T> &&coord) noexcept {
    return std::move(std::get<I>(coord.elements));
}

template <std::size_t I, std::integral T>
FORCE_INLINE constexpr const auto &&get(const Coord<T> &&coord) noexcept {
    return std::move(std::get<I>(coord.elements));
}

using CoordS32 = Coord<sint32>;
using CoordU32 = Coord<uint32>;

static_assert(std::is_trivial_v<CoordS32> && std::is_standard_layout_v<CoordS32>);
static_assert(std::is_trivial_v<CoordU32> && std::is_standard_layout_v<CoordU32>);

// -----------------------------------------------------------------------------
// Dimensions

struct Dimensions {
    uint32 width;
    uint32 height;
};

// -----------------------------------------------------------------------------
// Horizontal and vertical phases

enum class HorizontalPhase { Active, RightBorder, Sync, VBlankOut, LeftBorder, LastDot };
enum class VerticalPhase { Active, BottomBorder, BlankingAndSync, TopBorder, LastLine };

// -----------------------------------------------------------------------------
// CRAM address mapping

FORCE_INLINE uint32 MapCRAMAddress(uint32 address, uint8 colorRAMMode) {
    // RAMCTL.CRMD modes 2 and 3 shuffle address bits as follows:
    //   10 09 08 07 06 05 04 03 02 01 11 00
    //   in short, bits 10-01 are shifted left and bit 11 takes place of bit 01
    static constexpr auto kCRAMAddressMapping = [] {
        std::array<std::array<uint32, 4096>, 2> addrs{};
        for (uint32 addr = 0; addr < 4096; addr++) {
            addrs[0][addr] = addr;
            addrs[1][addr] = bit::extract<0>(addr) | (bit::extract<11>(addr) << 1u) | (bit::extract<1, 10>(addr) << 2u);
        }
        return addrs;
    }();

    return kCRAMAddressMapping[colorRAMMode >> 1][address & 0xFFF];
}

} // namespace ymir::vdp

// -----------------------------------------------------------------------------
// std::tuple functionality for Coord

namespace std {

using namespace ymir::vdp;

template <std::integral T>
struct tuple_size<Coord<T>> : integral_constant<size_t, 2> {};

template <size_t I, std::integral T>
struct tuple_element<I, Coord<T>> : tuple_element<I, decltype(Coord<T>::elements)> {};

} // namespace std