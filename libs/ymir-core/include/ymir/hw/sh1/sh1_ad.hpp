#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <array>
#include <cassert>

namespace ymir::sh1 {

struct ADConverter {
    ADConverter() {
        Reset();
    }

    void Reset() {
        convEnded = false;
        intrEnable = false;
        enable = false;
        scanMode = false;
        fastClock = false;
        chanSelect = 0;

        triggerEnable = false;

        convEndedMask = false;

        m_dataRegs.fill(0);
        TEMP = 0x00;
    }

    // Updates the converted value for the given data register.
    FORCE_INLINE void SetValue(uint8 index, uint16 value) {
        assert(index < 4);
        m_dataRegs[index] = bit::deposit<0, 5>(value, 0);
    }

    // 0E0  R    8,16     00        ADDRAH  A/D data register A (high)
    // 0E1  R    16       00        ADDRAL  A/D data register A (low)
    // 0E2  R    8,16     00        ADDRBH  A/D data register B (high)
    // 0E3  R    16       00        ADDRBL  A/D data register B (low)
    // 0E4  R    8,16     00        ADDRCH  A/D data register C (high)
    // 0E5  R    16       00        ADDRCL  A/D data register C (low)
    // 0E6  R    8,16     00        ADDRDH  A/D data register D (high)
    // 0E7  R    16       00        ADDRDL  A/D data register D (low)
    //
    //   bits   r/w  code       description
    //   15-6   R    AD9-0      A/D Data Register
    //    5-0   -    -          Reserved - must be zero

    template <bool peek>
    FORCE_INLINE uint16 ReadADDR(uint8 index) const {
        assert(index < 4);
        if constexpr (peek) {
            return m_dataRegs[index];
        } else {
            uint16 value = ReadADDRH<false>(index) << 8u;
            value |= ReadADDRL<false>(index);
            return value;
        }
    }

    template <bool peek>
    FORCE_INLINE uint8 ReadADDRH(uint8 index) const {
        assert(index < 4);
        if constexpr (!peek) {
            TEMP = bit::extract<0, 7>(m_dataRegs[index]);
        }
        return m_dataRegs[index] >> 8u;
    }

    template <bool peek>
    FORCE_INLINE uint8 ReadADDRL(uint8 index) const {
        assert(index < 4);
        if constexpr (peek) {
            return m_dataRegs[index];
        } else {
            return TEMP;
        }
    }

    template <bool poke>
    FORCE_INLINE void WriteADDR(uint8 index, uint16 value) {
        assert(index < 4);
        if constexpr (poke) {
            m_dataRegs[index] = bit::deposit<0, 5>(value, 0);
        }
    }

    template <bool poke>
    FORCE_INLINE void WriteADDRH(uint8 index, uint8 value) {
        assert(index < 4);
        if constexpr (poke) {
            bit::deposit_into<8, 15>(m_dataRegs[index], value);
        }
    }

    template <bool poke>
    FORCE_INLINE void WriteADDRL(uint8 index, uint8 value) {
        assert(index < 4);
        if constexpr (poke) {
            bit::deposit_into<0, 7>(m_dataRegs[index], bit::deposit<0, 5>(value, 0));
        }
    }

    // 0E8  R/W* 8,16     00        ADCSR   A/D control/status register
    //
    //   bits   r/w  code       description
    //      7   R/W* ADF        A/D End Flag
    //                            Cleared when:
    //                            - SH1 reads 1 then writes 0.
    //                            - ADI-triggered DMAC accesses the conversion register.
    //                            Set when:
    //                            - A/D conversion is complete in single mode
    //                            - A/D conversion of all selected channels is complete in scan mode
    //      6   R/W  ADIE       A/D Interrupt Enable (0=disable, 1=enable)
    //      5   R/W  ADST       A/D Start (0=halt, 1=run)
    //                            In single mode, runs a single conversion and stops.
    //                            In scan mode, runs conversions continuously on selected channels.
    //      4   R/W  SCAN       Scan Mode (0=single, 1=scan)
    //      3   R/W  CKS        Clock Select (0=max 266 states, 1=max 134 states)
    //    2-0   R/W  CH2-0      Channel Select
    //                            In single mode, selects one from AN0-AN7.
    //                            In scan mode:
    //                              000 (0) = AN0
    //                              001 (1) = AN0, AN1
    //                              010 (2) = AN0, AN1, AN2
    //                              011 (3) = AN0, AN1, AN2, AN3
    //                              100 (4) = AN4
    //                              101 (5) = AN4, AN5
    //                              110 (6) = AN4, AN5, AN6
    //                              111 (7) = AN4, AN5, AN6, AN7
    //
    // AN0-AN3 are part of group 0.
    // AN4-AN7 are part of group 1.
    // AN0 and AN4 output to ADDRA.
    // AN1 and AN5 output to ADDRB.
    // AN2 and AN6 output to ADDRC.
    // AN3 and AN7 output to ADDRD.

    template <bool peek>
    FORCE_INLINE uint8 ReadADCSR() const {
        uint8 value = 0;
        bit::deposit_into<7>(value, convEnded);
        bit::deposit_into<6>(value, intrEnable);
        bit::deposit_into<5>(value, enable);
        bit::deposit_into<4>(value, scanMode);
        bit::deposit_into<3>(value, fastClock);
        bit::deposit_into<0, 2>(value, chanSelect);
        if constexpr (!peek) {
            convEndedMask = convEnded;
        }
        return value;
    }

    template <bool poke>
    FORCE_INLINE void WriteADCSR(uint8 value) {
        if constexpr (poke) {
            convEnded = bit::test<7>(value);
        } else {
            convEnded &= bit::test<7>(value) || !convEndedMask;
        }
        intrEnable = bit::test<6>(value);
        enable = bit::test<5>(value);
        scanMode = bit::test<4>(value);
        fastClock = bit::test<3>(value);
        chanSelect = bit::extract<0, 2>(value);
    }

    // 0E9  R/W  8,16     7F        ADCR    A/D control register
    //
    //   bits   r/w  code       description
    //      7   R/W  TRGE       Trigger Enable
    //                            0 = Ignore external input
    //                            1 = Start A/D conversion at the falling edge of nADTRG
    //    6-0   -    -          Reserved - must be one

    FORCE_INLINE uint8 ReadADCR() const {
        uint8 value = 0;
        bit::deposit_into<7>(value, triggerEnable);
        bit::deposit_into<0, 6>(value, 0b1111111);
        return value;
    }

    FORCE_INLINE void WriteADCR(uint8 value) {
        triggerEnable = bit::test<7>(value);
    }

    // Whether A/D conversion has just completed.
    // Derived from ADCSR.ADF
    bool convEnded;

    // Whether to raise the ADI interrupt signal when a conversion is done.
    // Derived from ADCSR.ADIE
    bool intrEnable;

    // Whether A/D conversion is enabled or running.
    // Derived from ADCSR.ADST
    bool enable;

    // Whether to use single (false) or scan (true) mode.
    // Derived from ADCSR.SCAN
    bool scanMode;

    // Whether to use the slow (false) or fast (true) clock speed.
    // Derived from ADCSR.CKS
    bool fastClock;

    // Which channel to convert in single mode or set of channels to convert in scan mode.
    // Derived from ADCSR.CH2-0
    uint8 chanSelect;

    // Whether to start A/D conversion on the falling edge of the external trigget pin nADTRG.
    // Derived from ADCR.TRGE
    bool triggerEnable;

    mutable bool convEndedMask;

private:
    // A/D data registers A-D.
    // Only bits 6 to 15 are valid.
    std::array<uint16, 4> m_dataRegs;

    // Temporary storage for register reads.
    mutable uint8 TEMP;
};

} // namespace ymir::sh1
