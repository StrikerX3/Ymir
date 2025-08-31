#pragma once

#include <ymir/hw/vdp/vdp_defs.hpp>

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::state {

struct VDPState {
    alignas(16) std::array<uint8, vdp::kVDP1VRAMSize> VRAM1;
    alignas(16) std::array<uint8, vdp::kVDP2VRAMSize> VRAM2;
    alignas(16) std::array<uint8, vdp::kVDP2CRAMSize> CRAM;
    alignas(16) std::array<std::array<uint8, vdp::kVDP1FramebufferRAMSize>, 2> spriteFB;
    uint8 displayFB;

    uint64 VDP1TimingPenalty;

    struct VDP1RegsState {
        uint16 TVMR;
        uint16 FBCR;
        uint16 PTMR;
        uint16 EWDR;
        uint16 EWLR;
        uint16 EWRR;
        uint16 EDSR;
        uint16 LOPR;
        uint16 COPR;
        uint16 MODR;
        bool manualSwap;
        bool manualErase;
    } regs1;

    struct VDP2RegsState {
        uint16 TVMD;
        uint16 EXTEN;
        uint16 TVSTAT;
        uint16 VRSIZE;
        uint16 HCNT;
        uint16 VCNT;
        uint16 RAMCTL;
        uint16 CYCA0L;
        uint16 CYCA0U;
        uint16 CYCA1L;
        uint16 CYCA1U;
        uint16 CYCB0L;
        uint16 CYCB0U;
        uint16 CYCB1L;
        uint16 CYCB1U;
        uint16 BGON;
        uint16 MZCTL;
        uint16 SFSEL;
        uint16 SFCODE;
        uint16 CHCTLA;
        uint16 CHCTLB;
        uint16 BMPNA;
        uint16 BMPNB;
        uint16 PNCNA;
        uint16 PNCNB;
        uint16 PNCNC;
        uint16 PNCND;
        uint16 PNCR;
        uint16 PLSZ;
        uint16 MPOFN;
        uint16 MPOFR;
        uint16 MPABN0;
        uint16 MPCDN0;
        uint16 MPABN1;
        uint16 MPCDN1;
        uint16 MPABN2;
        uint16 MPCDN2;
        uint16 MPABN3;
        uint16 MPCDN3;
        uint16 MPABRA;
        uint16 MPCDRA;
        uint16 MPEFRA;
        uint16 MPGHRA;
        uint16 MPIJRA;
        uint16 MPKLRA;
        uint16 MPMNRA;
        uint16 MPOPRA;
        uint16 MPABRB;
        uint16 MPCDRB;
        uint16 MPEFRB;
        uint16 MPGHRB;
        uint16 MPIJRB;
        uint16 MPKLRB;
        uint16 MPMNRB;
        uint16 MPOPRB;
        uint16 SCXIN0;
        uint16 SCXDN0;
        uint16 SCYIN0;
        uint16 SCYDN0;
        uint16 ZMXIN0;
        uint16 ZMXDN0;
        uint16 ZMYIN0;
        uint16 ZMYDN0;
        uint16 SCXIN1;
        uint16 SCXDN1;
        uint16 SCYIN1;
        uint16 SCYDN1;
        uint16 ZMXIN1;
        uint16 ZMXDN1;
        uint16 ZMYIN1;
        uint16 ZMYDN1;
        uint16 SCXIN2;
        uint16 SCYIN2;
        uint16 SCXIN3;
        uint16 SCYIN3;
        uint16 ZMCTL;
        uint16 SCRCTL;
        uint16 VCSTAU;
        uint16 VCSTAL;
        uint16 LSTA0U;
        uint16 LSTA0L;
        uint16 LSTA1U;
        uint16 LSTA1L;
        uint16 LCTAU;
        uint16 LCTAL;
        uint16 BKTAU;
        uint16 BKTAL;
        uint16 RPMD;
        uint16 RPRCTL;
        uint16 KTCTL;
        uint16 KTAOF;
        uint16 OVPNRA;
        uint16 OVPNRB;
        uint16 RPTAU;
        uint16 RPTAL;
        uint16 WPSX0;
        uint16 WPSY0;
        uint16 WPEX0;
        uint16 WPEY0;
        uint16 WPSX1;
        uint16 WPSY1;
        uint16 WPEX1;
        uint16 WPEY1;
        uint16 WCTLA;
        uint16 WCTLB;
        uint16 WCTLC;
        uint16 WCTLD;
        uint16 LWTA0U;
        uint16 LWTA0L;
        uint16 LWTA1U;
        uint16 LWTA1L;
        uint16 SPCTL;
        uint16 SDCTL;
        uint16 CRAOFA;
        uint16 CRAOFB;
        uint16 LNCLEN;
        uint16 SFPRMD;
        uint16 CCCTL;
        uint16 SFCCMD;
        uint16 PRISA;
        uint16 PRISB;
        uint16 PRISC;
        uint16 PRISD;
        uint16 PRINA;
        uint16 PRINB;
        uint16 PRIR;
        uint16 CCRSA;
        uint16 CCRSB;
        uint16 CCRSC;
        uint16 CCRSD;
        uint16 CCRNA;
        uint16 CCRNB;
        uint16 CCRR;
        uint16 CCRLB;
        uint16 CLOFEN;
        uint16 CLOFSL;
        uint16 COAR;
        uint16 COAG;
        uint16 COAB;
        uint16 COBR;
        uint16 COBG;
        uint16 COBB;
    } regs2;

    enum class HorizontalPhase {
        Active = 0,
        RightBorder = 1,
        Sync = 2,
        LeftBorder = 4,
    };
    HorizontalPhase HPhase;

    enum class VerticalPhase {
        Active = 0,
        BottomBorder = 1,
        BlankingAndSync = 2,
        VCounterSkip = 5,
        TopBorder = 3,
        LastLine = 4,
    };
    VerticalPhase VPhase;

    struct VDPRendererState {
        struct VDP1RenderState {
            uint16 sysClipH;
            uint16 sysClipV;
            uint16 doubleV;

            uint16 userClipX0;
            uint16 userClipY0;
            uint16 userClipX1;
            uint16 userClipY1;

            sint32 localCoordX;
            sint32 localCoordY;

            bool rendering;

            bool erase;

            uint64 cycleCount;
            uint64 cyclesSpent;
        };

        struct NormBGLayerState {
            uint32 fracScrollX;
            uint32 fracScrollY;
            uint32 scrollAmountV;
            uint32 scrollIncH;
            uint32 lineScrollTableAddress;
            uint32 vertCellScrollOffset;
            bool vertCellScrollDelay;
            bool vertCellScrollRepeat;
            uint8 mosaicCounterY;
        };

        struct RotationParamState {
            std::array<std::array<uint32, 16>, 2> pageBaseAddresses;
            sint32 Xst, Yst;
            uint32 KA;
        };

        struct LineBackLayerState {
            uint32 lineColor;
            uint32 backColor;
        };

        struct Character {
            uint16 charNum;
            uint8 palNum;
            bool specColorCalc;
            bool specPriority;
            bool flipH;
            bool flipV;
        };

        struct VRAMFetcherState {
            Character currChar;
            Character nextChar;
            uint32 lastCharIndex;
            alignas(uint64) std::array<uint8, 8> bitmapData;
            uint32 bitmapDataAddress;
            uint32 lastVCellScroll;
        };

        VDP1RenderState vdp1State;
        std::array<NormBGLayerState, 4> normBGLayerStates;
        std::array<RotationParamState, 2> rotParamStates;
        LineBackLayerState lineBackLayerState;
        std::array<std::array<VRAMFetcherState, 6>, 2> vramFetchers;
        uint32 vertCellScrollInc;

        uint8 displayFB;
        bool vdp1Done;
    } renderer;
};

} // namespace ymir::state
