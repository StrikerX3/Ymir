#pragma once

#include <ymir/core/hash.hpp>
#include <ymir/core/types.hpp>

#include <array>

namespace ymir::state {

struct SH1State {
    alignas(16) std::array<uint32, 16> R;

    uint32 PC;
    uint32 PR;

    uint32 MACL;
    uint32 MACH;

    uint32 SR;

    uint32 GBR;
    uint32 VBR;

    uint32 delaySlotTarget;
    bool delaySlot;

    uint64 totalCycles;

    XXH128Hash ROMHash;
    std::array<uint8, 4 * 1024> onChipRAM;

    struct BSC {
        uint16 BCR;
        uint16 WCR1;
        uint16 WCR2;
        uint16 WCR3;
        uint16 DCR;
        uint16 PCR;
        uint16 RCR;
        uint16 RTCSR;
        uint8 RTCNT;
        uint8 RTCOR;
    } bsc;

    struct DMAC {
        struct Channel {
            uint32 SAR;
            uint32 DAR;
            uint16 TCR;
            uint16 CHCR;
            bool xferEndedMask;
        };

        std::array<Channel, 4> channels;
        uint32 DMAOR;
        bool AEread;
        bool NMIFread;
    } dmac;

    struct ITU {
        struct Timer {
            uint8 TCR;
            uint8 TIOR;
            uint8 TIER;
            uint8 TSR;
            uint16 TCNT;
            uint16 GRA;
            uint16 GRB;
            uint16 BRA;
            uint16 BRB;

            bool IMFAread;
            bool IMFBread;
            bool OVFread;

            uint64 currCycles;
        };

        std::array<Timer, 5> timers;
        uint8 TSTR;
        uint8 TSNC;
        uint8 TMDR;
        uint8 TFCR;
        uint8 TOCR;
    } itu;

    struct TPC {
        uint8 TPMR;
        uint8 TPCR;
        uint8 NDERB;
        uint8 NDERA;
        uint8 NDRB;
        uint8 NDRA;
    } tpc;

    struct WDT {
        uint8 TCSR;
        uint8 TCNT;
        uint8 RSTCSR;

        bool OVFread;
        uint64 cycleCount;
    } wdt;

    struct SCI {
        struct Channel {
            uint8 SMR;
            uint8 BRR;
            uint8 SCR;
            uint8 TDR;
            bool TDRvalid;
            uint8 SSR;
            uint8 RDR;

            uint8 RSR;
            uint8 RSRbit;
            uint8 TSR;
            uint8 TSRbit;

            bool txEmptyMask;
            bool rxFullMask;
            bool overrunErrorMask;
            bool framingErrorMask;
            bool parityErrorMask;

            uint64 currCycles;
        };

        std::array<Channel, 2> channels;
    } sci;

    struct AD {
        std::array<uint16, 4> ADDR;
        uint8 ADCSR;
        uint8 ADCR;

        uint8 TEMP;

        bool convEndedMask;
    } ad;

    struct PFC {
        uint16 PAIOR;
        uint16 PBIOR;
        uint16 PACR1;
        uint16 PACR2;
        uint16 PBCR1;
        uint16 PBCR2;
        uint16 CASCR;
    } pfc;

    struct INTC {
        uint16 ICR;

        alignas(16) std::array<uint8, 42> levels;
        alignas(16) std::array<uint8, 42> vectors;

        uint8 pendingSource;
        uint8 pendingLevel;

        bool NMI;
        uint8 irqs;
    } intc;

    uint8 SBYCR;
    bool sleep;

    std::array<bool, 2> nDREQ;
    bool PB2;
    bool TIOCB3;
};

} // namespace ymir::state
