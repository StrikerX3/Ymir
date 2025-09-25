#pragma once

#include <ymir/state/state_sh1.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <cassert>
#include <string_view>

namespace ymir::sh1 {

// 084  R/W  8,16,32  0000      IPRA    Interrupt priority register A
//
//   bits   r/w  code       description
//   15-12  R/W  -          IRQ0 Interrupt Priority Level
//   11-8   R/W  -          IRQ1 Interrupt Priority Level
//    7-4   R/W  -          IRQ2 Interrupt Priority Level
//    3-0   R/W  -          IRQ3 Interrupt Priority Level
//
// 086  R/W  8,16,32  0000      IPRB    Interrupt priority register B
//
//   bits   r/w  code       description
//   15-12  R/W  -          IRQ4 Interrupt Priority Level
//   11-8   R/W  -          IRQ5 Interrupt Priority Level
//    7-4   R/W  -          IRQ6 Interrupt Priority Level
//    3-0   R/W  -          IRQ7 Interrupt Priority Level
//
// 088  R/W  8,16,32  0000      IPRC    Interrupt priority register C
//
//   bits   r/w  code       description
//   15-12  R/W  -          DMAC0 and DMAC1 Interrupt Priority Level
//   11-8   R/W  -          DMAC2 and DMAC3 Interrupt Priority Level
//    7-4   R/W  -          ITU0 Interrupt Priority Level
//    3-0   R/W  -          ITU1 Interrupt Priority Level
//
// 08A  R/W  8,16,32  0000      IPRD    Interrupt priority register D
//
//   bits   r/w  code       description
//   15-12  R/W  -          ITU2 Interrupt Priority Level
//   11-8   R/W  -          ITU3 Interrupt Priority Level
//    7-4   R/W  -          ITU4 Interrupt Priority Level
//    3-0   R/W  -          SCI0 Interrupt Priority Level
//
// 08C  R/W  8,16,32  0000      IPRE    Interrupt priority register E
//
//   bits   r/w  code       description
//   15-12  R/W  -          SCI1 Interrupt Priority Level
//   11-8   R/W  -          PRT and A/D Interrupt Priority Level
//    7-4   R/W  -          WDT and REF Interrupt Priority Level
//    3-0   R/W  -          Reserved - must be zero
//
// Interrupt priority levels range from 0 to 15.

// 08E  R/W  8,16,32  0000      ICR     Interrupt control register
//
//   bits   r/w  code   description
//     15   R    NMIL   NMI Input Level
//   14-9   R    -      Reserved - must be zero
//      8   R/W  NMIE   NMI Edge Select (0=falling, 1=rising)
//      7   R/W  IRQ7S  IRQ7 Sense Select (0=low input, 1=falling edge)
//      6   R/W  IRQ6S  IRQ6 Sense Select (0=low input, 1=falling edge)
//      5   R/W  IRQ5S  IRQ5 Sense Select (0=low input, 1=falling edge)
//      4   R/W  IRQ4S  IRQ4 Sense Select (0=low input, 1=falling edge)
//      3   R/W  IRQ3S  IRQ3 Sense Select (0=low input, 1=falling edge)
//      2   R/W  IRQ2S  IRQ2 Sense Select (0=low input, 1=falling edge)
//      1   R/W  IRQ1S  IRQ1 Sense Select (0=low input, 1=falling edge)
//      0   R/W  IRQ0S  IRQ0 Sense Select (0=low input, 1=falling edge)
//
// The default value may be either 8000 or 0000 because NMIL is an external signal.
struct RegICR {
    uint8 IRQnS; // bitmap
    bool NMIE;
    bool NMIL;

    RegICR() {
        NMIL = false;
        Reset();
    }

    void Reset() {
        IRQnS = 0;
        NMIE = false;
    }
};

// -----------------------------------------------------------------------------

// Interrupt sources, sorted by default priority from lowest to highest
enum class InterruptSource : uint8 {
    None,       //  #  Source        Priority       Vector  Trigger
    REF_CMI,    //  1  BSC REF CMI   IPRE.WDT_REF   0x71    (TODO)
    WDT_ITI,    //  2  WDT ITI       IPRE.WDT_REF   0x70    (TODO)
    AD_ITI,     //  3  A/D ITI       IPRE.PRT_AD    0x6D    (TODO)
    PRT_PEI,    //  4  BSC PRT PEI   IPRE.PRT_AD    0x6C    (TODO)
    SCI1_TEI1,  //  5  SCI1 TEI1     IPRE.SCI1      0x6B    (TODO)
    SCI1_TxI1,  //  6  SCI1 TxI1     IPRE.SCI1      0x6A    (TODO)
    SCI1_RxI1,  //  7  SCI1 RxI1     IPRE.SCI1      0x69    (TODO)
    SCI1_ERI1,  //  8  SCI1 ERI1     IPRE.SCI1      0x68    (TODO)
    SCI0_TEI0,  //  9  SCI0 TEI0     IPRD.SCI0      0x67    (TODO)
    SCI0_TxI0,  // 10  SCI0 TxI0     IPRD.SCI0      0x66    (TODO)
    SCI0_RxI0,  // 11  SCI0 RxI0     IPRD.SCI0      0x65    (TODO)
    SCI0_ERI0,  // 12  SCI0 ERI0     IPRD.SCI0      0x64    (TODO)
    ITU4_OVI4,  // 13  ITU4 OVI4     IPRD.ITU4      0x62    (TODO)
    ITU4_IMIB4, // 14  ITU4 IMIB4    IPRD.ITU4      0x61    (TODO)
    ITU4_IMIA4, // 15  ITU4 IMIA4    IPRD.ITU4      0x60    (TODO)
    ITU3_OVI3,  // 16  ITU3 OVI3     IPRD.ITU3      0x5E    (TODO)
    ITU3_IMIB3, // 17  ITU3 IMIB3    IPRD.ITU3      0x5D    (TODO)
    ITU3_IMIA3, // 18  ITU3 IMIA3    IPRD.ITU3      0x5C    (TODO)
    ITU2_OVI2,  // 19  ITU2 OVI2     IPRD.ITU2      0x5A    (TODO)
    ITU2_IMIB2, // 20  ITU2 IMIB2    IPRD.ITU2      0x59    (TODO)
    ITU2_IMIA2, // 21  ITU2 IMIA2    IPRD.ITU2      0x58    (TODO)
    ITU1_OVI1,  // 22  ITU1 OVI1     IPRC.ITU1      0x56    (TODO)
    ITU1_IMIB1, // 23  ITU1 IMIB1    IPRC.ITU1      0x55    (TODO)
    ITU1_IMIA1, // 24  ITU1 IMIA1    IPRC.ITU1      0x54    (TODO)
    ITU0_OVI0,  // 25  ITU0 OVI0     IPRC.ITU0      0x52    (TODO)
    ITU0_IMIB0, // 26  ITU0 IMIB0    IPRC.ITU0      0x51    (TODO)
    ITU0_IMIA0, // 27  ITU0 IMIA0    IPRC.ITU0      0x50    (TODO)
    DMAC3_DEI3, // 28  DMAC3 DEI3    IPRC.DMAC23    0x4E    (TODO)
    DMAC2_DEI2, // 29  DMAC2 DEI2    IPRC.DMAC23    0x4C    (TODO)
    DMAC1_DEI1, // 30  DMAC1 DEI1    IPRC.DMAC01    0x4A    (TODO)
    DMAC0_DEI0, // 31  DMAC0 DEI0    IPRC.DMAC01    0x48    (TODO)
    IRQ7,       // 32  IRQ7          IPRA.IRQ7      0x47    IRQ7# pin - IsIRQAsserted(7)
    IRQ6,       // 33  IRQ6          IPRA.IRQ6      0x46    IRQ6# pin - IsIRQAsserted(6)
    IRQ5,       // 34  IRQ5          IPRA.IRQ5      0x45    IRQ5# pin - IsIRQAsserted(5)
    IRQ4,       // 35  IRQ4          IPRA.IRQ4      0x44    IRQ4# pin - IsIRQAsserted(4)
    IRQ3,       // 36  IRQ3          IPRA.IRQ3      0x43    IRQ3# pin - IsIRQAsserted(3)
    IRQ2,       // 37  IRQ2          IPRA.IRQ2      0x42    IRQ2# pin - IsIRQAsserted(2)
    IRQ1,       // 38  IRQ1          IPRA.IRQ1      0x41    IRQ1# pin - IsIRQAsserted(1)
    IRQ0,       // 39  IRQ0          IPRA.IRQ0      0x40    IRQ0# pin - IsIRQAsserted(0)
    UserBreak,  // 40  UBC break     15             0x0C    (TODO)
    NMI         // 41  NMI           16             0x0B    INTC.NMIL
};

inline std::string_view GetInterruptSourceName(InterruptSource source) {
    constexpr std::string_view kInterruptSourceNames[] = {
        "(none)",     "BSC REF CMI", "WDT ITI",    "A/D ITI",    "BSC PRT PEI", "SCI1 TEI1",  "SCI1 TxI1",
        "SCI1 RxI1",  "SCI1 ERI1",   "SCI0 TEI0",  "SCI0 TxI0",  "SCI0 RxI0",   "SCI0 ERI0",  "ITU4 OVI4",
        "ITU4 IMIB4", "ITU4 IMIA4",  "ITU3 OVI3",  "ITU3 IMIB3", "ITU3 IMIA3",  "ITU2 OVI2",  "ITU2 IMIB2",
        "ITU2 IMIA2", "ITU1 OVI1",   "ITU1 IMIB1", "ITU1 IMIA1", "ITU0 OVI0",   "ITU0 IMIB0", "ITU0 IMIA0",
        "DMAC3 DEI3", "DMAC2 DEI2",  "DMAC1 DEI1", "DMAC0 DEI0", "IRQ7",        "IRQ6",       "IRQ5",
        "IRQ4",       "IRQ3",        "IRQ2",       "IRQ1",       "IRQ0",        "UBC BRK",    "NMI"};

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

        SetVector(InterruptSource::REF_CMI, 0x71);
        SetVector(InterruptSource::WDT_ITI, 0x70);
        SetVector(InterruptSource::AD_ITI, 0x6D);
        SetVector(InterruptSource::PRT_PEI, 0x6C);
        SetVector(InterruptSource::SCI1_TEI1, 0x6B);
        SetVector(InterruptSource::SCI1_TxI1, 0x6A);
        SetVector(InterruptSource::SCI1_RxI1, 0x69);
        SetVector(InterruptSource::SCI1_ERI1, 0x68);
        SetVector(InterruptSource::SCI0_TEI0, 0x67);
        SetVector(InterruptSource::SCI0_TxI0, 0x66);
        SetVector(InterruptSource::SCI0_RxI0, 0x65);
        SetVector(InterruptSource::SCI0_ERI0, 0x64);
        SetVector(InterruptSource::ITU4_OVI4, 0x62);
        SetVector(InterruptSource::ITU4_IMIB4, 0x61);
        SetVector(InterruptSource::ITU4_IMIA4, 0x60);
        SetVector(InterruptSource::ITU3_OVI3, 0x5E);
        SetVector(InterruptSource::ITU3_IMIB3, 0x5D);
        SetVector(InterruptSource::ITU3_IMIA3, 0x5C);
        SetVector(InterruptSource::ITU2_OVI2, 0x5A);
        SetVector(InterruptSource::ITU2_IMIB2, 0x59);
        SetVector(InterruptSource::ITU2_IMIA2, 0x58);
        SetVector(InterruptSource::ITU1_OVI1, 0x56);
        SetVector(InterruptSource::ITU1_IMIB1, 0x55);
        SetVector(InterruptSource::ITU1_IMIA1, 0x54);
        SetVector(InterruptSource::ITU0_OVI0, 0x52);
        SetVector(InterruptSource::ITU0_IMIB0, 0x51);
        SetVector(InterruptSource::ITU0_IMIA0, 0x50);
        SetVector(InterruptSource::DMAC3_DEI3, 0x4E);
        SetVector(InterruptSource::DMAC2_DEI2, 0x4C);
        SetVector(InterruptSource::DMAC1_DEI1, 0x4A);
        SetVector(InterruptSource::DMAC0_DEI0, 0x48);
        SetVector(InterruptSource::IRQ7, 0x47);
        SetVector(InterruptSource::IRQ6, 0x46);
        SetVector(InterruptSource::IRQ5, 0x45);
        SetVector(InterruptSource::IRQ4, 0x44);
        SetVector(InterruptSource::IRQ3, 0x43);
        SetVector(InterruptSource::IRQ2, 0x42);
        SetVector(InterruptSource::IRQ1, 0x41);
        SetVector(InterruptSource::IRQ0, 0x40);

        SetLevel(InterruptSource::UserBreak, 15);
        SetVector(InterruptSource::UserBreak, 0x0C);

        SetLevel(InterruptSource::NMI, 16);
        SetVector(InterruptSource::NMI, 0x0B);

        NMI = false;

        pending.source = InterruptSource::None;
        pending.level = 0;

        irqs = 0xFF;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SH1State::INTC &state) const {
        state.ICR = ReadICR();
        state.levels = m_levels;
        state.vectors = m_vectors;
        state.pendingSource = static_cast<uint8>(pending.source);
        state.pendingLevel = pending.level;
        state.NMI = NMI;
        state.irqs = irqs;
    }

    bool ValidateState(const state::SH1State::INTC &state) const {
        return true;
    }

    void LoadState(const state::SH1State::INTC &state) {
        WriteICR<true, true, true>(state.ICR);
        m_levels = state.levels;
        m_vectors = state.vectors;
        pending.source = static_cast<InterruptSource>(state.pendingSource);
        pending.level = state.pendingLevel;
        NMI = state.NMI;
        irqs = state.irqs;
    }

    // -------------------------------------------------------------------------
    // Registers

    // Gets the interrupt vector number for the specified interrupt source.
    FORCE_INLINE uint8 GetVector(InterruptSource source) const {
        return m_vectors[static_cast<size_t>(source)];
    }

private:
    // Sets the interrupt vector number for the specified interrupt source.
    FORCE_INLINE void SetVector(InterruptSource source, uint8 vector) {
        m_vectors[static_cast<size_t>(source)] = vector;
    }

public:
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
        bit::deposit_into<0, 7>(value, ICR.IRQnS);
        bit::deposit_into<8>(value, ICR.NMIE);
        bit::deposit_into<15>(value, ICR.NMIL);
        return value;
    }

    template <bool lowerByte, bool upperByte, bool poke>
    FORCE_INLINE void WriteICR(uint16 value) {
        if constexpr (lowerByte) {
            ICR.IRQnS = bit::extract<0, 7>(value);
        }
        if constexpr (upperByte) {
            ICR.NMIE = bit::test<8>(value);
            if constexpr (poke) {
                ICR.NMIL = bit::test<15>(value);
            }
        }
    }

    // Updates the level of the specified IRQ.
    // Returns true if the IRQ signal is now raised.
    // The IRQ is raised when level-triggered (sense bit=0) and currently low or when edge-triggered (sense bit=1) and
    // just switched from high to low.
    FORCE_INLINE bool UpdateIRQ(uint8 irqnum, bool level) {
        assert(irqnum < 8);
        const uint8 bit = 1u << irqnum;
        const bool sense = ICR.IRQnS & bit;
        const bool currLevel = irqs & bit;
        const bool trigger = !level && (!sense || currLevel != level);
        irqs &= ~(1u << irqnum);
        irqs |= static_cast<uint8>(level) << irqnum;
        return trigger;
    }

    // Returns true if the specified IRQ is currently raised.
    // The IRQ is raised when level-triggered (sense bit=0) and currently low.
    FORCE_INLINE bool IsIRQAsserted(uint8 irqnum) const {
        assert(irqnum < 8);

        const uint8 bit = 1u << irqnum;
        return ~ICR.IRQnS & ~irqs & bit;
    }

    RegICR ICR; // 08E  R/W  8,16,32  0000      ICR     Interrupt control register

    struct PendingInterruptInfo {
        InterruptSource source;
        uint8 level;
    } pending;

    bool NMI;
    uint8 irqs;

private:
    std::array<uint8, 42> m_levels;
    std::array<uint8, 42> m_vectors;
};

} // namespace ymir::sh1
