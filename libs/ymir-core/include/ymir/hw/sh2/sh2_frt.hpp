#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <cassert>

namespace ymir::sh2 {

struct FreeRunningTimer {
    static constexpr uint64 kDividerShifts[] = {3, 5, 7, 64};

    enum class Event { None, OVI, OCI };

    FreeRunningTimer() {
        Reset();
    }

    void Reset() {
        TIER.Reset();
        FTCSR.Reset();
        FRC = 0x0000;
        OCRA = OCRB = 0xFFFF;
        TCR.Reset();
        TOCR.Reset();
        ICR = 0x0000;

        TEMP = 0x00;

        m_cycleCount = 0;
        m_clockDividerShift = kDividerShifts[TCR.CKSn];
    }

    // Advances the cycle counter to the specified amount
    FORCE_INLINE Event AdvanceTo(uint64 cycles) {
        // Must be monotonically increasing
        assert(cycles >= m_cycleCount);

        if (m_clockDividerShift >= 64) [[unlikely]] {
            m_cycleCount = cycles;
            return Event::None;
        }

        const uint64 steps = (cycles >> m_clockDividerShift) - (m_cycleCount >> m_clockDividerShift);
        m_cycleCount = cycles;
        if (steps == 0) {
            return Event::None;
        }

        Event event = Event::None;

        uint64 nextFRC = FRC + steps;
        if (nextFRC >= 0x10000) {
            FTCSR.OVF = 1;
            if (TIER.OVIE) {
                event = Event::OVI;
            }
        }
        if (static_cast<uint16>(OCRA - FRC) < steps) {
            FTCSR.OCFA = 1;
            if (FTCSR.CCLRA) {
                nextFRC = 0;
            }
            if (TIER.OCIAE) {
                event = Event::OCI;
            }
        }
        if (static_cast<uint16>(OCRB - FRC) < steps) {
            FTCSR.OCFB = 1;
            if (TIER.OCIBE) {
                event = Event::OCI;
            }
        }
        FRC = nextFRC;

        return event;
    }

    // -------------------------------------------------------------------------
    // Registers

    // addr r/w  access   init      code    name
    // 010  R/W  8        01        TIER    Timer interrupt enable register
    //
    //   bits   r/w  code     description
    //      7   R/W  ICIE     Input Capture Interrupt Enable
    //    6-4   R/W  -        Reserved - must be zero
    //      3   R/W  OCIAE    Output Compare Interrupt A Enable
    //      2   R/W  OCIBE    Output Compare Interrupt B Enable
    //      1   R/W  OVIE     Timer Overflow Interrupt Enable
    //      0   R/W  -        Reserved - must be one
    struct RegTIER {
        RegTIER() {
            Reset();
        }

        void Reset() {
            ICIE = false;
            unused = 0x00;
            OCIAE = false;
            OCIBE = false;
            OVIE = false;
        }

        bool ICIE;    // 7   R/W  ICIE     Input Capture Interrupt Enable
        uint8 unused; // 6-4 R/W  -        (unused but writable bits)
        bool OCIAE;   // 3   R/W  OCIAE    Output Compare Interrupt A Enable
        bool OCIBE;   // 2   R/W  OCIBE    Output Compare Interrupt B Enable
        bool OVIE;    // 1   R/W  OVIE     Timer Overflow Interrupt Enable

        bool anyEnabled; // whether any of the interrupt enable bits are set (cached for performance)
    } TIER;

    FORCE_INLINE uint8 ReadTIER() const {
        uint8 value = 0;
        bit::deposit_into<7>(value, TIER.ICIE);
        bit::deposit_into<4, 6>(value, bit::extract<4, 6>(TIER.unused));
        bit::deposit_into<3>(value, TIER.OCIAE);
        bit::deposit_into<2>(value, TIER.OCIBE);
        bit::deposit_into<1>(value, TIER.OVIE);
        bit::deposit_into<0>(value, 1);
        return value;
    }

    FORCE_INLINE void WriteTIER(uint8 value) {
        TIER.ICIE = bit::test<7>(value);
        TIER.unused = bit::extract<4, 6>(value) << 4u;
        TIER.OCIAE = bit::test<3>(value);
        TIER.OCIBE = bit::test<2>(value);
        TIER.OVIE = bit::test<1>(value);

        TIER.anyEnabled = (value & 0b10001110) != 0;
    }

    // 011  R/W  8        00        FTCSR   Free-running timer control/status register
    //
    //   bits   r/w  code     description
    //      7   R/W  ICF      Input Capture Flag (clear on zero write)
    //    6-4   R/W  -        Reserved - must be zero
    //      3   R/W  OCFA     Output Compare Flag A (clear on zero write)
    //      2   R/W  OCFB     Output Compare Flag B (clear on zero write)
    //      1   R/W  OVF      Timer Overflow Flag (clear on zero write)
    //      0   R/W  CCLRA    Counter Clear A
    struct RegFTCSR {
        RegFTCSR() {
            Reset();
        }

        void Reset() {
            ICF = false;
            OCFA = false;
            OCFB = false;
            OVF = false;
            CCLRA = false;
            mask = 0x00;
        }

        bool ICF;   // 7   R/W  ICF      Input Capture Flag (clear on zero write)
        bool OCFA;  // 3   R/W  OCFA     Output Compare Flag A (clear on zero write)
        bool OCFB;  // 2   R/W  OCFB     Output Compare Flag B (clear on zero write)
        bool OVF;   // 1   R/W  OVF      Timer Overflow Flag (clear on zero write)
        bool CCLRA; // 0   R/W  CCLRA    Counter Clear A

        // Which bits have been read as true?
        // Necessary to mask clears.
        mutable uint8 mask;
    } FTCSR;

    template <bool peek>
    FORCE_INLINE uint8 ReadFTCSR() const {
        uint8 value = 0;
        bit::deposit_into<7>(value, FTCSR.ICF);
        bit::deposit_into<3>(value, FTCSR.OCFA);
        bit::deposit_into<2>(value, FTCSR.OCFB);
        bit::deposit_into<1>(value, FTCSR.OVF);
        bit::deposit_into<0>(value, FTCSR.CCLRA);
        if constexpr (!peek) {
            FTCSR.mask = value;
        }
        return value;
    }

    template <bool poke>
    FORCE_INLINE void WriteFTCSR(uint8 value) {
        if constexpr (poke) {
            FTCSR.ICF = bit::test<7>(value);
            FTCSR.OCFA = bit::test<3>(value);
            FTCSR.OCFB = bit::test<2>(value);
            FTCSR.OVF = bit::test<1>(value);
            FTCSR.CCLRA = bit::test<0>(value);
        } else {
            FTCSR.ICF &= bit::test<7>(value) || !bit::test<7>(FTCSR.mask);
            FTCSR.OCFA &= bit::test<3>(value) || !bit::test<3>(FTCSR.mask);
            FTCSR.OCFB &= bit::test<2>(value) || !bit::test<2>(FTCSR.mask);
            FTCSR.OVF &= bit::test<1>(value) || !bit::test<1>(FTCSR.mask);
            FTCSR.CCLRA = bit::test<0>(value);
            FTCSR.mask &= value;
        }
    }

    // 012  R/W  8        00        FRC H     Free-running counter H
    // 013  R/W  8        00        FRC L     Free-running counter L
    uint16 FRC;

    template <bool peek>
    FORCE_INLINE uint8 ReadFRCH() const {
        if constexpr (!peek) {
            TEMP = FRC;
        }
        return FRC >> 8u;
    }

    template <bool peek>
    FORCE_INLINE uint8 ReadFRCL() const {
        if constexpr (peek) {
            return FRC;
        } else {
            return TEMP;
        }
    }

    template <bool poke>
    FORCE_INLINE void WriteFRCH(uint8 value) {
        if constexpr (poke) {
            bit::deposit_into<8, 15>(FRC, value);
        } else {
            TEMP = value;
        }
    }

    template <bool poke>
    FORCE_INLINE void WriteFRCL(uint8 value) {
        if constexpr (poke) {
            bit::deposit_into<0, 7>(FRC, value);
        } else {
            FRC = value | (TEMP << 8u);
        }
    }

    // 014  R/W  8        FF        OCRA/B H  Output compare register A/B H
    // 015  R/W  8        FF        OCRA/B L  Output compare register A/B L
    uint16 OCRA, OCRB;

    FORCE_INLINE uint16 &CurrOCR() {
        return TOCR.OCRS ? OCRB : OCRA;
    }

    FORCE_INLINE uint16 CurrOCR() const {
        return TOCR.OCRS ? OCRB : OCRA;
    }

    FORCE_INLINE uint8 ReadOCRH() const {
        return CurrOCR() >> 8u;
    }

    FORCE_INLINE uint8 ReadOCRL() const {
        return CurrOCR() >> 0u;
    }

    template <bool poke>
    FORCE_INLINE void WriteOCRH(uint8 value) {
        if constexpr (poke) {
            bit::deposit_into<8, 15>(CurrOCR(), value);
        } else {
            TEMP = value;
        }
    }

    template <bool poke>
    FORCE_INLINE void WriteOCRL(uint8 value) {
        if constexpr (poke) {
            bit::deposit_into<0, 7>(CurrOCR(), value);
        } else {
            CurrOCR() = value | (TEMP << 8u);
        }
    }

    // 016  R/W  8        00        TCR       Timer control register
    //
    //   bits   r/w  code     description
    //      7   R/W  IEDGA    Input Edge Select (0=falling, 1=rising)
    //    6-2   R/W  -        Reserved - must be zero
    //    1-0   R/W  CKS1-0   Clock Select
    //                          00 (0) = Internal clock / 8
    //                          01 (1) = Internal clock / 32
    //                          10 (2) = Internal clock / 128
    //                          11 (3) = External clock (on rising edge)
    //                                   (the FTCI pin is tied to +5V on the Saturn,
    //                                    so this option effectively disables the timer)
    struct RegTCR {
        RegTCR() {
            Reset();
        }

        void Reset() {
            IEDGA = false;
            CKSn = 0;
        }

        bool IEDGA; //   7   R/W  IEDGA    Input Edge Select (0=falling, 1=rising)
        uint8 CKSn; // 1-0   R/W  CKS1-0   Clock Select
    } TCR;

    FORCE_INLINE uint8 ReadTCR() const {
        uint8 value = 0;
        bit::deposit_into<7>(value, TCR.IEDGA);
        bit::deposit_into<0, 1>(value, TCR.CKSn);
        return value;
    }

    FORCE_INLINE void WriteTCR(uint8 value) {
        TCR.IEDGA = bit::test<7>(value);
        TCR.CKSn = bit::extract<0, 1>(value);

        m_clockDividerShift = kDividerShifts[TCR.CKSn];
    }

    FORCE_INLINE void WriteTCR_CKSn(uint8 value) {
        TCR.CKSn = bit::extract<0, 1>(value);
        m_clockDividerShift = kDividerShifts[TCR.CKSn];
    }

    // 017  R/W  8        E0        TOCR      Timer output compare control register
    //
    //   bits   r/w  code     description
    //    7-5   R/W  -        Reserved - must be one
    //      4   R/W  OCRS     Output Compare Register Select (0=OCRA, 1=OCRB)
    //    3-2   R/W  -        Reserved - must be zero
    //      1   R/W  OLVLA    Output Level A
    //      0   R/W  OLVLB    Output Level B
    struct RegTOCR {
        RegTOCR() {
            Reset();
        }

        void Reset() {
            OCRS = false;
            OLVLA = false;
            OLVLB = false;
        }

        bool OCRS;    // 4   R/W  OCRS     Output Compare Register Select (0=OCRA, 1=OCRB)
        uint8 unused; // 3-2 R/W  -        (unused but writable)
        bool OLVLA;   // 1   R/W  OLVLA    Output Level A
        bool OLVLB;   // 0   R/W  OLVLB    Output Level B
    } TOCR;

    FORCE_INLINE uint8 ReadTOCR() const {
        uint8 value = 0;
        bit::deposit_into<5, 7>(value, 0b111);
        bit::deposit_into<4>(value, TOCR.OCRS);
        bit::deposit_into<2, 3>(value, bit::extract<2, 3>(TOCR.unused));
        bit::deposit_into<1>(value, TOCR.OLVLA);
        bit::deposit_into<0>(value, TOCR.OLVLB);
        return value;
    }

    FORCE_INLINE void WriteTOCR(uint8 value) {
        TOCR.OCRS = bit::test<4>(value);
        TOCR.unused = bit::extract<2, 3>(value) << 2u;
        TOCR.OLVLA = bit::test<1>(value);
        TOCR.OLVLB = bit::test<0>(value);
    }

    // 018  R    8        00        ICR H     Input capture register H
    // 019  R    8        00        ICR L     Input capture register L
    uint16 ICR;

    template <bool peek>
    FORCE_INLINE uint8 ReadICRH() const {
        if constexpr (!peek) {
            TEMP = ICR;
        }
        return ICR >> 8u;
    }

    template <bool peek>
    FORCE_INLINE uint8 ReadICRL() const {
        if constexpr (peek) {
            return ICR;
        } else {
            return TEMP;
        }
    }

    template <bool poke>
    FORCE_INLINE void WriteICRH(uint8 value) {
        if constexpr (poke) {
            bit::deposit_into<8, 15>(ICR, value);
        }
    }

    template <bool poke>
    FORCE_INLINE void WriteICRL(uint8 value) {
        if constexpr (poke) {
            bit::deposit_into<0, 7>(ICR, value);
        }
    }

    mutable uint8 TEMP; // temporary storage to handle 16-bit transfers

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SH2State::FRT &state) const {
        state.TIER = ReadTIER();
        state.FTCSR = ReadFTCSR<true>();
        state.FRC = FRC;
        state.OCRA = OCRA;
        state.OCRB = OCRB;
        state.TCR = ReadTCR();
        state.TOCR = ReadTOCR();
        state.ICR = ICR;
        state.TEMP = TEMP;
        state.cycleCount = m_cycleCount;
        state.FTCSR_mask = FTCSR.mask;
    }

    void LoadState(const state::SH2State::FRT &state) {
        WriteTIER(state.TIER);
        WriteFTCSR<true>(state.FTCSR);
        FRC = state.FRC;
        OCRA = state.OCRA;
        OCRB = state.OCRB;
        WriteTCR(state.TCR);
        WriteTOCR(state.TOCR);
        ICR = state.ICR;
        TEMP = state.TEMP;
        m_cycleCount = state.cycleCount;
        FTCSR.mask = state.FTCSR_mask;
    }

private:
    // -------------------------------------------------------------------------
    // State

    uint64 m_cycleCount;
    uint64 m_clockDividerShift; // derived from TCR.CKS
};

} // namespace ymir::sh2
