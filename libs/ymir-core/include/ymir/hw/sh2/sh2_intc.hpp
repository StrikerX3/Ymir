#pragma once

#include <ymir/savestate/savestate_sh2.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <string_view>

namespace ymir::sh2 {

// 0E2  R/W  8,16     0000      IPRA    Interrupt priority setting register A
//
//   bits   r/w  code       description
//   15-12  R/W  DIVUIP3-0  Division Unit (DIVU) Interrupt Priority Level
//   11-8   R/W  DMACIP3-0  DMA Controller (DMAC) Interrupt Priority Level
//    7-4   R/W  WDTIP3-0   Watchdog Timer (WDT) Interrupt Priority Level
//    3-0   R    -          Reserved - must be zero
//
//   Interrupt priority levels range from 0 to 15.
//
//   The DMAC priority level is assigned to both channels.
//   If both channels raise an interrupt, channel 0 is prioritized.
//
//   WDTIP3-0 includes both the watchdog timer and bus state controller (BSC).
//   WDT interrupt has priority over BSC.

// addr r/w  access   init      code    name
// 060  R/W  8,16     0000      IPRB    Interrupt priority setting register B
//
//   bits   r/w  code       description
//   15-12  R/W  SCIIP3-0   Serial Communication Interface (SCI) Interrupt Priority Level
//   11-8   R/W  FRTIP3-0   Free-Running Timer (FRT) Interrupt Priority Level
//    7-0   R/W  Reserved   Must be zero
//
//   Interrupt priority levels range from 0 to 15.

// 062  R/W  8,16     0000      VCRA    Vector number setting register A
//
//   bits   r/w  code     description
//     15   R    -        Reserved - must be zero
//   14-8   R/W  SERV6-0  Serial Communication Interface (SCI) Receive-Error Interrupt Vector Number
//      7   R    -        Reserved - must be zero
//    6-0   R/W  SRXV6-0  Serial Communication Interface (SCI) Receive-Data-Full Interrupt Vector Number

// 064  R/W  8,16     0000      VCRB    Vector number setting register B
//
//   bits   r/w  code     description
//     15   R    -        Reserved - must be zero
//   14-8   R/W  STXV6-0  Serial Communication Interface (SCI) Transmit-Data-Empty Interrupt Vector Number
//      7   R    -        Reserved - must be zero
//    6-0   R/W  STEV6-0  Serial Communication Interface (SCI) Transmit-End Interrupt Vector Number

// 066  R/W  8,16     0000      VCRC    Vector number setting register C
//
//   bits   r/w  code     description
//     15   R    -        Reserved - must be zero
//   14-8   R/W  FICV6-0  Free-Running Timer (FRT) Input-Capture Interrupt Vector Number
//      7   R    -        Reserved - must be zero
//    6-0   R/W  FOCV6-0  Free-Running Timer (FRT) Output-Compare Interrupt Vector Number

// 068  R/W  8,16     0000      VCRD    Vector number setting register D
//
//   bits   r/w  code     description
//     15   R    -        Reserved - must be zero
//   14-8   R/W  FOVV6-0  Free-Running Timer (FRT) Overflow Interrupt Vector Number
//    7-0   R    -        Reserved - must be zero

// 0E4  R/W  8,16     0000      VCRWDT  Vector number setting register WDT
//
//   bits   r/w  code     description
//     15   R    -        Reserved - must be zero
//   14-8   R/W  WITV6-0  Watchdog Timer (WDT) Interval Interrupt Vector Number
//      7   R    -        Reserved - must be zero
//    6-0   R/W  BCMV6-0  Bus State Controller (BSC) Compare Match Interrupt Vector Number

// 0E0  R/W  8,16     0000      ICR     Interrupt control register
//
//   bits   r/w  code   description
//     15   R    NMIL   NMI Input Level
//   14-9   R    -      Reserved - must be zero
//      8   R/W  NMIE   NMI Edge Select (0=falling, 1=rising)
//    7-1   R    -      Reserved - must be zero
//      0   R/W  VECMD  IRL Interrupt Vector Mode Select (0=auto, 1=external)
//                      Auto-vector mode assigns 71 to IRL15 and IRL14, 70 to IRL13 and IRL12,
//                      and so on, down to 64 to IRL1. IRL0 does not exist.
//                      External vector mode reads from external vector number input pins D7-D0.
//
//    The default value may be either 8000 or 0000 because NMIL is an external signal.
struct RegICR {
    bool VECMD;
    bool NMIE;
    bool NMIL;

    RegICR() {
        NMIL = false;
        Reset();
    }

    void Reset() {
        VECMD = false;
        NMIE = false;
    }
};

// -----------------------------------------------------------------------------

// Interrupt sources, sorted by default priority from lowest to highest
enum class InterruptSource : uint8 {
    None,          // #   Source        Priority       Vector                Trigger
    FRT_OVI,       // 1   FRT OVI       IPRB.FRTIPn    VCRD.FOVVn            FRT.FTCSR.OVF && FRT.TIER.OVIE
    FRT_OCI,       // 2   FRT OCI       IPRB.FRTIPn    VCRC.FOCVn            FRT.FTCSR.OCF[AB] && FRT.TIER.OCI[AB]E
    FRT_ICI,       // 3   FRT ICI       IPRB.FRTIPn    VCRC.FICVn            FRT.FTCSR.ICF && FRT.TIER.ICIE
    SCI_TEI,       // 4   SCI TEI       IPRB.SCIIPn    VCRB.STEVn            (TODO)
    SCI_TXI,       // 5   SCI TXI       IPRB.SCIIPn    VCRB.STXVn            (TODO)
    SCI_RXI,       // 6   SCI RXI       IPRB.SCIIPn    VCRA.SRXVn            (TODO)
    SCI_ERI,       // 7   SCI ERI       IPRB.SCIIPn    VCRA.SERVn            (TODO)
    BSC_REF_CMI,   // 8   BSC REF CMI   IPRA.WDTIPn    VCRWDT.BCMVn          (TODO)
    WDT_ITI,       // 9   WDT ITI       IPRA.WDTIPn    VCRWDT.WITVn          WDT.WTCSR.OVF && !WDT.WTCSR.WT_nIT
    DMAC1_XferEnd, // 10  DMAC1 end     IPRA.DMACIPn   VCRDMA1               DMAC1.TE && DMAC1.IE
    DMAC0_XferEnd, // 11  DMAC0 end     IPRA.DMACIPn   VCRDMA0               DMAC0.TE && DMAC0.IE
    DIVU_OVFI,     // 12  DIVU OVFI     IPRA.DIVUIPn   VCRDIV                DIVU.DVCR.OVF && DIVU.DVCR.OVFIE
    IRL,           // 13  IRL#          15-1           0x40 + (level >> 1)   IRL#.level > 0
    UserBreak,     // 14  UBC break     15             0x0C                  (TODO)
    NMI            // 15  NMI           16             0x0B                  INTC.NMIL
};

inline std::string_view GetInterruptSourceName(InterruptSource source) {
    constexpr std::string_view kInterruptSourceNames[] = {
        "(none)",      "FRT OVI", "FRT OCI",  "FRT ICI",  "SCI TEI",   "SCI TXI", "SCI RXI", "SCI ERI",
        "BSC REF CMI", "WDT ITI", "DMAC1 TE", "DMAC0 TE", "DIVU OVFI", "IRL",     "UBC BRK", "NMI"};

    const auto index = static_cast<uint8>(source);
    if (index < std::size(kInterruptSourceNames)) {
        return kInterruptSourceNames[index];
    } else {
        return "(invalid)";
    }
}

struct InterruptController {
    InterruptController() {
        Reset();
    }

    void Reset() {
        ICR.Reset();

        m_levels.fill(0);
        m_vectors.fill(0);

        SetLevel(InterruptSource::IRL, 1);
        SetVector(InterruptSource::IRL, 0x40);

        SetLevel(InterruptSource::UserBreak, 15);
        SetVector(InterruptSource::UserBreak, 0x0C);

        SetLevel(InterruptSource::NMI, 16);
        SetVector(InterruptSource::NMI, 0x0B);

        NMI = false;

        pending.source = InterruptSource::None;
        pending.level = 0;

        externalVector = 0;
    }

    // Gets the interrupt vector number for the specified interrupt source.
    FORCE_INLINE uint8 GetVector(InterruptSource source) const {
        return m_vectors[static_cast<size_t>(source)];
    }

    // Sets the interrupt vector number for the specified interrupt source.
    FORCE_INLINE void SetVector(InterruptSource source, uint8 vector) {
        m_vectors[static_cast<size_t>(source)] = vector;
    }

    // Gets the interrupt level for the specified interrupt source.
    FORCE_INLINE uint8 GetLevel(InterruptSource source) const {
        return m_levels[static_cast<size_t>(source)];
    }

    // Sets the interrupt level for the specified interrupt source.
    FORCE_INLINE void SetLevel(InterruptSource source, uint8 priority) {
        m_levels[static_cast<size_t>(source)] = priority;
    }

    FORCE_INLINE uint16 ReadICR() const {
        uint16 value = 0;
        bit::deposit_into<0>(value, ICR.VECMD);
        bit::deposit_into<8>(value, ICR.NMIE);
        bit::deposit_into<15>(value, ICR.NMIL);
        return value;
    }

    template <bool lowerByte, bool upperByte, bool poke>
    FORCE_INLINE void WriteICR(uint16 value) {
        if constexpr (lowerByte) {
            ICR.VECMD = bit::test<0>(value);
            UpdateIRLVector();
        }
        if constexpr (upperByte) {
            ICR.NMIE = bit::test<8>(value);
            if constexpr (poke) {
                ICR.NMIL = bit::test<15>(value);
            }
        }
    }

    FORCE_INLINE void UpdateIRLVector() {
        if (ICR.VECMD) {
            SetVector(InterruptSource::IRL, externalVector);
        } else {
            const uint8 level = GetLevel(InterruptSource::IRL);
            SetVector(InterruptSource::IRL, 0x40 + (level >> 1u));
        }
    }

    RegICR ICR; // 0E0  R/W  8,16     0000      ICR     Interrupt control register

    struct PendingInterruptInfo {
        InterruptSource source;
        uint8 level;
    } pending;

    bool NMI;
    uint8 externalVector;

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::SH2SaveState::INTC &state) const {
        state.ICR = ReadICR();
        state.levels = m_levels;
        state.vectors = m_vectors;
        state.pendingSource = static_cast<uint8>(pending.source);
        state.pendingLevel = pending.level;
        state.NMI = NMI;
        state.extVec = externalVector;
    }

    void LoadState(const savestate::SH2SaveState::INTC &state) {
        WriteICR<true, true, true>(state.ICR);
        m_levels = state.levels;
        m_vectors = state.vectors;
        pending.source = static_cast<InterruptSource>(state.pendingSource);
        pending.level = state.pendingLevel;
        NMI = state.NMI;
        externalVector = state.extVec;
    }

private:
    std::array<uint8, 16> m_levels;
    std::array<uint8, 16> m_vectors;
};

} // namespace ymir::sh2
