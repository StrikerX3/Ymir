#pragma once

/**
@file
@brief General VDP1 definitions.
*/

#include <ymir/core/types.hpp>

#include <ymir/util/inline.hpp>

namespace ymir::vdp {

// VDP1 command structure in VRAM
//   00  CMDCTRL  Control Words
//   02  CMDLINK  Link Specification
//   04  CMDPMOD  Draw Mode Word
//   06  CMDCOLR  Color Control Word
//   08  CMDSRCA  Character Address
//   0A  CMDSIZE  Character Size
//   0C  CMDXA    Vertex A X Coordinate
//   0E  CMDYA    Vertex A Y Coordinate
//   10  CMDXB    Vertex B X Coordinate
//   12  CMDYB    Vertex B Y Coordinate
//   14  CMDXC    Vertex C X Coordinate
//   16  CMDYC    Vertex C Y Coordinate
//   18  CMDXD    Vertex D X Coordinate
//   1A  CMDYD    Vertex D Y Coordinate
//   1C  CMDGRDA  Gouraud Shading Table
struct VDP1Command {
    enum class CommandType : uint16 {
        // Textured drawing
        DrawNormalSprite = 0x0,
        DrawScaledSprite = 0x1,
        DrawDistortedSprite = 0x2,
        DrawDistortedSpriteAlt = 0x3,

        // Untextured drawing
        DrawPolygon = 0x4,
        DrawPolylines = 0x5,
        DrawPolylinesAlt = 0x7,
        DrawLine = 0x6,

        // Clipping coordinate setting
        UserClipping = 0x8,
        UserClippingAlt = 0xB,
        SystemClipping = 0x9,

        // Local coordinate setting
        SetLocalCoordinates = 0xA,
    };

    enum class JumpType : uint16 {
        Next = 0x0,
        Assign = 0x1,
        Call = 0x2,
        Return = 0x3,
    };

    // CMDCTRL
    union Control {
        uint16 u16;
        struct {
            CommandType command : 4;
            uint16 flipH : 1;
            uint16 flipV : 1;
            uint16 : 2;
            uint16 zoomPoint : 4;
            JumpType jumpMode : 2;
            uint16 skip : 1;
            uint16 end : 1;
        };

        [[nodiscard]] FORCE_INLINE bool IsValid() const noexcept {
            return static_cast<uint16>(command) <= 0xB;
        }
    };

    // CMDPMOD
    //   15   MSB On
    //   12   High-Speed Shrink
    //   11   Pre-clipping Disable
    //   10   User Clipping Enable
    //    9   User Clipping Mode
    //    8   Mesh Enable
    //    7   End Code Disable
    //    6   Transparent Pixel Disable
    //  5-3   Color Mode
    //          000 (0) = 4 bpp, 16 colors, bank mode
    //          001 (1) = 4 bpp, 16 colors, lookup table mode
    //          010 (2) = 8 bpp, 64 colors, bank mode
    //          011 (3) = 8 bpp, 128 colors, bank mode
    //          100 (4) = 8 bpp, 256 colors, bank mode
    //          101 (5) = 16 bpp, 32768 colors, RGB mode
    //  2-0   Color Calculation Bits
    //    2     Gouraud shading enable
    //    1     Half-source ("original graphic")
    //    0     Half-destination ("background")
    union DrawMode {
        uint16 u16;
        struct {
            uint16 colorCalcBits : 2; // except Gouraud enable
            uint16 gouraudEnable : 1;
            uint16 colorMode : 3;
            uint16 transparentPixelDisable : 1;
            uint16 endCodeDisable : 1;
            uint16 meshEnable : 1;
            uint16 clippingMode : 1;
            uint16 userClippingEnable : 1;
            uint16 preClippingDisable : 1;
            uint16 highSpeedShrink : 1;
            uint16 : 2;
            uint16 msbOn : 1;
        };
    };

    // CMDSIZE
    //  13-8   Character Size X / 8
    //   7-0   Character Size Y
    union Size {
        uint16 u16;
        struct {
            uint16 V : 8;
            uint16 H : 6;
        };
    };
};

} // namespace ymir::vdp
