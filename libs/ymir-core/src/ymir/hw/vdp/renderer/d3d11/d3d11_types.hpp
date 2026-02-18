#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::vdp {

using D3DInt = sint32;
using D3DUint = uint32;

union D3DUint2 {
    std::array<D3DUint, 2> array;
    struct {
        D3DUint x, y;
    };
    struct {
        D3DUint r, g;
    };
};
static_assert(sizeof(D3DUint2) == sizeof(D3DUint) * 2);

union D3DUint4 {
    std::array<D3DUint, 4> array;
    struct {
        D3DUint x, y, z, w;
    };
    struct {
        D3DUint r, g, b, a;
    };
};
static_assert(sizeof(D3DUint4) == sizeof(D3DUint) * 4);

union D3DInt2 {
    std::array<D3DInt, 2> array;
    struct {
        D3DInt x, y;
    };
    struct {
        D3DInt r, g;
    };
};
static_assert(sizeof(D3DInt2) == sizeof(D3DInt) * 2);

union D3DInt3 {
    std::array<D3DInt, 3> array;
    struct {
        D3DInt x, y, z;
    };
    struct {
        D3DInt r, g, b;
    };
};
static_assert(sizeof(D3DInt3) == sizeof(D3DInt) * 3);

// -----------------------------------------------------------------------------

struct alignas(16) VDP2RenderConfig {
    struct DisplayParams {            //  bits  use
        D3DUint interlaced : 1;       //     0  Interlaced
        D3DUint oddField : 1;         //     1  Field                    0=even; 1=odd
        D3DUint exclusiveMonitor : 1; //     2  Exclusive monitor mode   0=normal; 1=exclusive
        D3DUint colorRAMMode : 2;     //   3-4  Color RAM mode
                                      //          0 = RGB 5:5:5, 1024 words
                                      //          1 = RGB 5:5:5, 2048 words
                                      //          2 = RGB 8:8:8, 1024 words
                                      //          3 = RGB 8:8:8, 1024 words  (same as mode 2, undocumented)
        D3DUint hiResH : 1;           //     5  Horizontal resolution    0=320/352; 1=640/704
    } displayParams;

    // Top Y coordinate of target rendering area
    D3DUint startY;

    // Bits 0-5 hold the layer enable state based on BGON and other factors:
    //
    // bit  RBG0+RBG1   RBG0        RBG1        no RBGs
    //   0  Sprite      Sprite      Sprite      Sprite
    //   1  RBG0        RBG0        -           -
    //   2  RBG1        NBG0        RBG1        NBG0
    //   3  EXBG        NBG1/EXBG   NBG1/EXBG   NBG1/EXBG
    //   4  -           NBG2        NBG2        NBG2
    //   5  -           NBG3        NBG3        NBG3
    //
    // Bits 16-21 hold the individual layer enable flags:
    // bit  layer
    //  16  NBG0
    //  17  NBG1
    //  18  NBG2
    //  19  NBG3
    //  20  RBG0
    //  21  RBG1
    D3DUint layerEnabled;
};

struct BGRenderParams {
    // Entries 0 and 1 - common parameters
    struct Common {
        /* Entry 0 (X) */               //  bits  use
        D3DUint charPatAccess : 4;      //   0-3  Character pattern access per VRAM bank
        D3DUint vramAccessOffset : 4;   //   4-7  VRAM access offset per bank      0=no delay; 1=8-byte delay
        D3DUint cramOffset : 3;         //  8-10  CRAM offset
        D3DUint colorFormat : 3;        // 11-13  Color format
                                        //          0 =   16-color palette   3 = RGB 5:5:5
                                        //          1 =  256-color palette   4 = RGB 8:8:8
                                        //          2 = 2048-color palette   (other values invalid/unused)
        D3DUint specColorCalcMode : 2;  // 14-15  Special color calculation mode
                                        //          0 = per screen      2 = per dot
                                        //          1 = per character   3 = color data MSB
        D3DUint specFuncSelect : 1;     //    16  Special function select          0=A; 1=B
        D3DUint priorityNumber : 3;     // 17-19  Priority number
        D3DUint priorityMode : 2;       // 20-21  Priority mode
                                        //          0 = per screen      2 = per dot
                                        //          1 = per character   3 = (invalid/unused)
        D3DUint supplPalNum : 3;        // 22-24  Supplementary palette number
        D3DUint supplColorCalcBit : 1;  //    25  Supplementary special color calculation bit
        D3DUint supplSpecPrioBit : 1;   //    26  Supplementary special priority bit
        D3DUint charPatDelay : 1;       //    27  Character pattern delay
        D3DUint transparencyEnable : 1; //    28  Transparency enable              0=disable; 1=enable
        D3DUint colorCalcEnable : 1;    //    29  Color calculation enable         0=disable; 1=enable
        D3DUint enabled : 1;            //    30  Background enabled               0=disable; 1=enable
        D3DUint bitmap : 1;             //    31  Background type                  0=scroll; 1=bitmap

        /* Entry 1 (Y) */                   //  bits  use
        D3DUint lineZoomEnable : 1;         //     0  Line zoom enable             0=disable; 1=enable  (NBG0/1 only)
        D3DUint lineScrollXEnable : 1;      //     1  X line scroll enable         0=disable; 1=enable  (NBG0/1 only)
        D3DUint lineScrollYEnable : 1;      //     2  Y line scroll enable         0=disable; 1=enable  (NBG0/1 only)
        D3DUint lineScrollInterval : 2;     //   3-4  Line scroll table interval   (1 << x)             (NBG0/1 only)
        D3DUint lineScrollTableAddress : 3; //   5-7  Line scroll table address    (x << 17)            (NBG0/1 only)
        D3DUint vertCellScrollEnable : 1;   //     8  Vertical cell scroll enable  0=disable; 1=enable  (NBG0/1 only)
        D3DUint vertCellScrollDelay : 1;    //     9  Vertical cell scroll delay   0=none; 1=one entry  (NBG0/1 only)
        D3DUint vertCellScrollOffset : 1;   //    10  Vertical cell scroll offset  0=none; 1=4 bytes    (NBG0/1 only)
        D3DUint vertCellScrollRepeat : 1;   //    11  Vertical cell scroll repeat  0=none; 1=once       (NBG0 only)
        D3DUint mosaicEnable : 1;           //    12  Mosaic enable                0=disable; 1=enable
        D3DUint windowLogic : 1;            //    13  Window logic                 0=OR; 1=AND
        D3DUint window0Enable : 1;          //    14  Window 0 enable              0=disable; 1=enable
        D3DUint window0Invert : 1;          //    15  Window 0 invert              0=disable; 1=enable
        D3DUint window1Enable : 1;          //    16  Window 1 enable              0=disable; 1=enable
        D3DUint window1Invert : 1;          //    17  Window 1 invert              0=disable; 1=enable
        D3DUint spriteWindowEnable : 1;     //    18  Sprite window enable         0=disable; 1=enable
        D3DUint spriteWindowInvert : 1;     //    19  Sprite window invert         0=disable; 1=enable
    } common;
    static_assert(sizeof(Common) == sizeof(D3DUint) * 2);

    // Entry 2 (Z) - rotation parameters
    struct RotParams {                      //  bits  use
        D3DUint screenOverPatternName : 16; //  0-15  Screen-over pattern name
        D3DUint screenOverProcess : 2;      // 16-17  Screen-over process
                                            //          0 = repeat planes      2 = transparent
                                            //          1 = repeat character   3 = transparent + restrict to 512x512
    } rotParams;
    static_assert(sizeof(RotParams) == sizeof(D3DUint));

    // Entry 3 (W) - type-specific parameters
    union TypeSpecific {
        struct Scroll {                //  bits  use
            D3DUint patNameAccess : 4; //   0-3  Pattern name access per bank
            D3DUint pageShiftH : 1;    //     4  Horizontal page size shift    (NBG0-3, RotParam A/B)
            D3DUint pageShiftV : 1;    //     5  Vertical page size shift      (NBG0-3, RotParam A/B)
            D3DUint extChar : 1;       //     6  Extended character number     0=10 bits; 1=12 bits, no H/V flip
            D3DUint twoWordChar : 1;   //     7  Two-word character            0=one-word (16-bit); 1=two-word (32-bit)
            D3DUint cellSizeShift : 1; //     8  Character cell size           0=1x1 cell; 1=2x2 cells
            D3DUint supplCharNum : 5;  //  9-13  Supplementary character number
        } scroll;

        struct Bitmap {                    //  bits  use
            D3DUint bitmapSizeH : 1;       //     0  Horizontal bitmap size shift  (512 << x)  (NBG0-3 only)
            D3DUint bitmapSizeV : 1;       //     1  Vertical bitmap size shift    (256 << x)  (NBG0-3 only)
            D3DUint bitmapBaseAddress : 3; //   2-4  Bitmap base address           (x << 17)   (NBG0-3, RotParam A/B)
        } bitmap;
    } typeSpecific;
    static_assert(sizeof(TypeSpecific) == sizeof(D3DUint));
};
static_assert(sizeof(BGRenderParams) == sizeof(D3DUint) * 4);

struct WindowRenderParams {
    D3DUint2 start;
    D3DUint2 end;
    D3DUint lineWindowTableAddress;
    bool lineWindowTableEnable;
};

struct RotParams {             //  bits  use
    D3DUint rotParamMode : 2;  //   0-1  Rotation parameter mode
                               //          0 = always use A   2 = select based on coefficient data
                               //          1 = always use B   3 = select based on window flag
    D3DUint windowLogic : 1;   //     2  Window logic     0=OR; 1=AND
    D3DUint window0Enable : 1; //     3  Window 0 enable  0=disable; 1=enable
    D3DUint window0Invert : 1; //     4  Window 0 invert  0=disable; 1=enable
    D3DUint window1Enable : 1; //     5  Window 1 enable  0=disable; 1=enable
    D3DUint window1Invert : 1; //     6  Window 1 invert  0=disable; 1=enable
};

struct alignas(16) VDP2BGRenderState {
    std::array<BGRenderParams, 4> nbgParams;
    std::array<BGRenderParams, 2> rbgParams;

    std::array<D3DUint2, 4> nbgScrollAmount; // 11.8 fixed-point
    std::array<D3DUint2, 4> nbgScrollInc;    // 11.8 fixed-point

    std::array<std::array<D3DUint, 4>, 4> nbgPageBaseAddresses;                 // [NBG0-3][plane A-D]
    std::array<std::array<std::array<D3DUint, 16>, 2>, 2> rbgPageBaseAddresses; // [RotParam A/B][RBG0-1][plane A-P]

    std::array<WindowRenderParams, 2> windows; // Window 0 and 1

    RotParams commonRotParams;

    D3DUint specialFunctionCodes; //  bits  use
                                  //   0-7  Special function code A
                                  //  8-15  Special function code B
};

// -----------------------------------------------------------------------------

struct RotationRenderParams {      //  bits  use
    D3DUint coeffTableEnable : 1;  //     0  Coefficient table enabled          0=disable; 1=enable
    D3DUint coeffTableCRAM : 1;    //     1  Coefficient table location         0=VRAM; 1=CRAM
    D3DUint coeffDataSize : 1;     //     2  Coefficient data size              0=2 words; 1=1 word
    D3DUint coeffDataMode : 2;     //   3-4  Coefficient data mode              0=kx/ky; 1=kx; 2=ky; 3=Px
    D3DUint coeffDataAccessA0 : 1; //     5  Coefficient data access for VRAM bank A0/A
    D3DUint coeffDataAccessA1 : 1; //     6  Coefficient data access for VRAM bank A1
    D3DUint coeffDataAccessB0 : 1; //     7  Coefficient data access for VRAM bank B0/B
    D3DUint coeffDataAccessB1 : 1; //     8  Coefficient data access for VRAM bank B1
    D3DUint coeffDataPerDot : 1;   //     9  Per-dot coefficients               0=per line; 1=per dot
    D3DUint fbRotEnable : 1;       //    10  VDP1 framebuffer rotation enable   0=disable; 1=enable

    D3DUint _reserved;
};
static_assert(sizeof(RotationRenderParams) == sizeof(D3DUint) * 2);

// Base Xst, Yst, KA for params A and B relative to config.startY
struct alignas(16) RotParamBase {
    D3DUint tableAddress;
    D3DInt Xst, Yst;
    D3DUint KA;
};
static_assert(sizeof(RotParamBase) == sizeof(D3DUint) * 4);

struct alignas(16) VDP2RotParamData {
    D3DInt2 screenCoords; // Screen coordinates (26.0)
    D3DUint spriteCoords; // Sprite coordinates (13.0) (packed 2x 16-bit ints)
    D3DUint coeffData;    // Raw coefficient line color data (bits 0-6) + transparency (bit 7)
};

// -----------------------------------------------------------------------------

struct alignas(16) VDP2ComposeParams { //  bits  use
    D3DUint colorCalcEnable : 8;       //   0-7  Use color calculation per layer  0=disable; 1=enable
                                       //          0 = sprite
                                       //          1 = RBG0
                                       //          2 = RBG1/NBG0
                                       //          3 = NBG1/EXBG
                                       //          4 = NBG2
                                       //          5 = NBG3
                                       //          6 = Back screen
                                       //          7 = Line screen
    D3DUint extendedColorCalc : 1;     //     8  Use extended color calculation   0=disable; 1=enable
                                       //          (always disabled in hi-res modes)
    D3DUint blendMode : 1;             //     9  Blend mode                       0=alpha; 1=additive
    D3DUint useSecondScreenRatio : 1;  //    10  Use second screen ratio          0=top screen; 1=second screen
    D3DUint colorOffsetEnable : 7;     // 11-17  Color offset enable per layer    0=disable; 1=enable
                                       //          0 = Sprite
                                       //          1 = RBG0
                                       //          2 = NBG0/RBG1
                                       //          3 = NBG1/EXBG
                                       //          4 = NBG2
                                       //          5 = NBG3
                                       //          6 = Back screen
    D3DUint colorOffsetSelect : 7;     // 18-24  Color offset select per layer    0=A; 1=B
                                       //          0 = Sprite
                                       //          1 = RBG0
                                       //          2 = NBG0/RBG1
                                       //          3 = NBG1/EXBG
                                       //          4 = NBG2
                                       //          5 = NBG3
                                       //          6 = Back screen
    D3DUint lineColorEnable : 7;       // 25-31  Line color enable per layer      0=disable; 1=enable
                                       //          0 = Sprite
                                       //          1 = RBG0
                                       //          2 = NBG0/RBG1
                                       //          3 = NBG1/EXBG
                                       //          4 = NBG2
                                       //          5 = NBG3
                                       //          6 = Back screen (always false), but simplifies shader implementation

    D3DInt3 colorOffsetA; // Color offset A (RGB999)
    D3DInt3 colorOffsetB; // Color offset B (RGB999)

    D3DUint bgColorCalcRatios; // NBG/RBG color calculation ratios (bit packed)
                               //  bits  layer
                               //   0-4  RBG0
                               //   5-9  NBG0/RBG1
                               // 10-14  NBG1/EXBG
                               // 15-19  NBG2
                               // 20-24  NBG3

    D3DUint backLineColorCalcRatios; // Back/line screen color calculation ratios (bit packed)
                                     //  bits  layer
                                     //   0-4  Back screen
                                     //   5-9  Line screen
};

} // namespace ymir::vdp
