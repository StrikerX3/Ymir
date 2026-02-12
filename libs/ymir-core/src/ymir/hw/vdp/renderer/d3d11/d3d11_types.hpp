#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::vdp {

union D3DUint2 {
    std::array<uint32, 2> array;
    struct {
        uint32 x, y;
    };
};
static_assert(sizeof(D3DUint2) == sizeof(uint32) * 2);

struct alignas(16) VDP2RenderConfig {
    struct DisplayParams {           //  bits  use
        uint32 interlaced : 1;       //     0  Interlaced
        uint32 oddField : 1;         //     1  Field                    0=even; 1=odd
        uint32 exclusiveMonitor : 1; //     2  Exclusive monitor mode   0=normal; 1=exclusive
        uint32 colorRAMMode : 2;     //   3-4  Color RAM mode
                                     //          0 = RGB 5:5:5, 1024 words
                                     //          1 = RGB 5:5:5, 2048 words
                                     //          2 = RGB 8:8:8, 1024 words
                                     //          3 = RGB 8:8:8, 1024 words  (same as mode 2, undocumented)
    } displayParams;
    uint32 startY; // Top Y coordinate of target rendering area
};

struct NBGRenderParams {
    // Entry 0 - common properties
    struct Common {                    //  bits  use
        uint32 charPatAccess : 4;      //   0-3  Character pattern access per bank
        uint32 charPatDelay : 1;       //     4  Character pattern delay
        uint32 mosaicEnable : 1;       //     5  Mosaic enable                   0=disable; 1=enable
        uint32 transparencyEnable : 1; //     6  Transparency enable             0=disable; 1=enable
        uint32 colorCalcEnable : 1;    //     7  Color calculation enable        0=disable; 1=enable
        uint32 cramOffset : 3;         //  8-10  CRAM offset
        uint32 colorFormat : 3;        // 11-13  Color format
                                       //          0 =   16-color palette   3 = RGB 5:5:5
                                       //          1 =  256-color palette   4 = RGB 8:8:8
                                       //          2 = 2048-color palette   (other values invalid/unused)
        uint32 specColorCalcMode : 2;  // 14-15  Special color calculation mode
                                       //          0 = per screen      2 = per dot
                                       //          1 = per character   3 = color data MSB
        uint32 specFuncSelect : 1;     //    16  Special function select         0=A; 1=B
        uint32 priorityNumber : 3;     // 17-19  Priority number
        uint32 priorityMode : 2;       // 20-21  Priority mode
                                       //          0 = per screen      2 = per dot
                                       //          1 = per character   3 = invalid/unused
        uint32 supplPalNum : 3;        // 22-24  Supplementary palette number
        uint32 supplColorCalcBit : 1;  //    25  Supplementary special color calculation bit
        uint32 supplSpecPrioBit : 1;   //    26  Supplementary special priority bit
        uint32 : 3;                    // 27-29  -
        uint32 enabled : 1;            //    30  Background enabled              0=disable; 1=enable
        uint32 bitmap : 1;             //    31  Background type (= 0)           0=scroll; 1=bitmap
    } common;
    static_assert(sizeof(Common) == sizeof(uint32));

    // Entry 1 - type-specific parameters
    union TypeSpecific {
        struct Scroll {                //  bits  use
            uint32 patNameAccess : 4;  //   0-3  Pattern name access per bank
            uint32 pageShiftH : 1;     //     4  Horizontal page size shift
            uint32 pageShiftV : 1;     //     5  Vertical page size shift
            uint32 extChar : 1;        //     6  Extended character number     0=10 bits; 1=12 bits, no H/V flip
            uint32 twoWordChar : 1;    //     7  Two-word character            0=one-word (16-bit); 1=two-word (32-bit)
            uint32 cellSizeShift : 1;  //     8  Character cell size           0=1x1 cell; 1=2x2 cells
            uint32 vertCellScroll : 1; //     9  Vertical cell scroll enable   0=disable; 1=enable  (NBG0 and NBG1 only)
            uint32 supplCharNum : 5;   // 10-14  Supplementary character number
        } scroll;

        struct Bitmap {                   //  bits  use
            uint32 bitmapSizeH : 1;       //     0  Horizontal bitmap size shift (512 << x)
            uint32 bitmapSizeV : 1;       //     1  Vertical bitmap size shift (256 << x)
            uint32 bitmapBaseAddress : 3; //   2-4  Bitmap base address (x << 17)
                                          //  5-31  -
        } bitmap;
    } typeSpecific;
    static_assert(sizeof(TypeSpecific) == sizeof(uint32));
};

struct alignas(16) VDP2RenderState {
    std::array<NBGRenderParams, 4> nbgParams;

    std::array<D3DUint2, 4> nbgScrollAmount; // 11.8 fixed-point
    std::array<D3DUint2, 4> nbgScrollInc;    // 11.8 fixed-point

    std::array<std::array<uint32, 4>, 4> nbgPageBaseAddresses;  // [NBG0-3][plane A-D]
    std::array<std::array<uint32, 16>, 2> rbgPageBaseAddresses; // [RBG0-1][plane A-P]
};

} // namespace ymir::vdp
