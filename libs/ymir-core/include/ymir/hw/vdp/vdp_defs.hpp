#pragma once

/**
@file
@brief General definitions common to VDP1 and VDP2.
*/

#include "vdp1_defs.hpp"
#include "vdp2_defs.hpp"

#include <ymir/core/types.hpp>

#include <ymir/util/inline.hpp>
#include <ymir/util/size_ops.hpp>

#include <array>
#include <concepts>

namespace ymir::vdp {

// -----------------------------------------------------------------------------
// Memory chip sizes

inline constexpr std::size_t kVDP1VRAMSize = 512_KiB;
inline constexpr std::size_t kVDP1FramebufferRAMSize = 256_KiB;
inline constexpr std::size_t kVDP2VRAMSize = 512_KiB;
inline constexpr std::size_t kVDP2CRAMSize = 4_KiB;

// -----------------------------------------------------------------------------
// Common constants

inline constexpr uint32 kMaxResH = 704; // Maximum horizontal resolution
inline constexpr uint32 kMaxResV = 512; // Maximum vertical resolution

// -----------------------------------------------------------------------------
// VDP1

using SpriteFB = std::array<uint8, kVDP1FramebufferRAMSize>;

inline constexpr uint64 kVDP1CycleBudgetPerFrame = 595000; // rough estimate for 8-bit fillrate per frame

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

FORCE_INLINE Color555 ConvertRGB888to555(Color888 color) {
    return Color555{
        .r = static_cast<uint16>(color.r >> 3u),
        .g = static_cast<uint16>(color.g >> 3u),
        .b = static_cast<uint16>(color.b >> 3u),
        .msb = static_cast<uint16>(color.msb),
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

    [[nodiscard]] const T &x() const {
        return elements[0];
    }

    [[nodiscard]] const T &y() const {
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
// Display phases

enum class HorizontalPhase { Active, RightBorder, Sync, LeftBorder };
enum class VerticalPhase { Active, BottomBorder, BlankingAndSync, VCounterSkip, TopBorder, LastLine };

// -----------------------------------------------------------------------------
// Layers

enum class Layer { Sprite, RBG0, NBG0_RBG1, NBG1_EXBG, NBG2, NBG3 };

} // namespace ymir::vdp

// -----------------------------------------------------------------------------
// std::tuple helpers

namespace std {

template <std::integral T>
struct tuple_size<ymir::vdp::Coord<T>> : integral_constant<size_t, 2> {};

template <size_t I, std::integral T>
struct tuple_element<I, ymir::vdp::Coord<T>> : tuple_element<I, decltype(ymir::vdp::Coord<T>::elements)> {};

} // namespace std