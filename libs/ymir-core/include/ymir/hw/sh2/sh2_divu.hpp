#pragma once

#include <ymir/state/state_sh2.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <limits>

namespace ymir::sh2 {

// addr r/w  access   init      code    name
// 100  R/W  32       ud        DVSR    Divisor register
//
//   bits   r/w  code   description
//   31-0   R/W  -      Divisor number
using RegDVSR = uint32;

// 104  R/W  32       ud        DVDNT   Dividend register L for 32-bit division
//
//   bits   r/w  code   description
//   31-0   R/W  -      32-bit dividend number
using RegDVDNT = uint32;

// 108  R/W  16,32    00000000  DVCR    Division control register
//
//   bits   r/w  code   description
//   31-2   R    -      Reserved - must be zero
//      1   R/W  OVFIE  OVF interrupt enable (0=disabled, 1=enabled)
//      0   R/W  OVF    Overflow Flag (0=no overflow, 1=overflow)
struct RegDVCR {
    RegDVCR() {
        Reset();
    }

    void Reset() {
        OVF = false;
        OVFIE = false;
    }

    FORCE_INLINE uint32 Read() const {
        uint32 value = 0;
        bit::deposit_into<0>(value, OVF);
        bit::deposit_into<1>(value, OVFIE);
        return value;
    }

    FORCE_INLINE void Write(uint32 value) {
        OVF = bit::test<0>(value);
        OVFIE = bit::test<1>(value);
    }

    bool OVF;
    bool OVFIE;
};

// 10C  R/W  16,32    ud        VCRDIV  Vector number register setting DIV
//
//   bits   r/w  code   description
//   31-7   R    -      Reserved - must be zero
//    6-0   R/W  -      Interrupt Vector Number

// 110  R/W  32       ud        DVDNTH  Dividend register H
//
//   bits   r/w  code   description
//   31-0   R/W  -      64-bit dividend number (upper half)
using RegDVDNTH = uint32;

// 114  R/W  32       ud        DVDNTL  Dividend register L
//
//   bits   r/w  code   description
//   31-0   R/W  -      64-bit dividend number (lower half)
using RegDVDNTL = uint32;

// 118  R/W  32       ud        DVDNTUH Undocumented dividend register H
//
//   bits   r/w  code   description
//   31-0   R/W  -      64-bit dividend number (upper half)
using RegDVDNTUH = uint32;

// 11C  R/W  32       ud        DVDNTUL Undocumented dividend register L
//
//   bits   r/w  code   description
//   31-0   R/W  -      64-bit dividend number (lower half)
using RegDVDNTUL = uint32;

// 120..13F are mirrors of 100..11F

struct DivisionUnit {
    DivisionUnit() {
        Reset();
    }

    void Reset() {
        DVSR = 0x0;  // undefined initial value
        DVDNT = 0x0; // undefined initial value
        DVCR.Reset();
        DVDNTH = 0x0;  // undefined initial value
        DVDNTL = 0x0;  // undefined initial value
        DVDNTUH = 0x0; // undefined initial value
        DVDNTUL = 0x0; // undefined initial value
    }

    RegDVSR DVSR;       // 100  R/W  32       ud        DVSR    Divisor register
    RegDVDNT DVDNT;     // 104  R/W  32       ud        DVDNT   Dividend register L for 32-bit division
    RegDVCR DVCR;       // 108  R/W  16,32    00000000  DVCR    Division control register
                        // 10C  R/W  16,32    ud        VCRDIV  Vector number register setting DIV
    RegDVDNTH DVDNTH;   // 110  R/W  32       ud        DVDNTH  Dividend register H
    RegDVDNTL DVDNTL;   // 114  R/W  32       ud        DVDNTL  Dividend register L
    RegDVDNTUH DVDNTUH; // 118  R/W  32       ud        DVDNTUH Undocumented dividend register H
    RegDVDNTUL DVDNTUL; // 11C  R/W  32       ud        DVDNTUL Undocumented dividend register L

    // 120..13F are mirrors of 100..11F

    // Both division calculations take 39 cycles to complete, or 6 if it results in overflow.
    // On overflow, the OVF bit is set and an overflow interrupt is generated if DVCR.OVFIE=1.
    // DVDNTH and DVDNTL will contain the partial results of the operation after 6 cycles.
    // If DVCR.OFVIE=0, DVDNTL will be saturated to 0x7FFFFFFF or 0x80000000 depending on the sign.
    // For 32-bit by 32-bit divisions, DVDNT receives a copy of DVDNTL.

    // Begins a 32-bit by 32-bit signed division calculation, storing the 32-bit quotient in DVDNT
    // and the 32-bit remainder in DVDNTH.
    FORCE_INLINE void Calc32() {
        static constexpr sint32 kMinValue = std::numeric_limits<sint32>::min();
        static constexpr sint32 kMaxValue = std::numeric_limits<sint32>::max();

        const sint32 dividend = static_cast<sint32>(DVDNTL);
        const sint32 divisor = static_cast<sint32>(DVSR);

        if (divisor != 0) {
            // TODO: schedule event to run this after 39 cycles

            if (dividend == kMinValue && divisor == -1) [[unlikely]] {
                // Handle extreme case
                DVDNTL = DVDNT = kMinValue;
                DVDNTH = 0;
            } else {
                DVDNTL = DVDNT = dividend / divisor;
                DVDNTH = dividend % divisor;
            }
        } else {
            // Overflow
            // TODO: schedule event to run this after 6 cycles

            // Perform partial division
            // The division unit uses 3 cycles to set up flags, leaving 3 cycles for calculations
            DVDNTH = dividend >> 29;
            if (DVCR.OVFIE) {
                DVDNTL = DVDNT = (dividend << 3) | ((~dividend >> 31) & 7);
            } else {
                // DVDNT/DVDNTL is saturated if the interrupt signal is disabled
                DVDNTL = DVDNT = dividend < 0 ? kMinValue : kMaxValue;
            }

            // Signal overflow
            DVCR.OVF = 1;
        }

        DVDNTUH = DVDNTH;
        DVDNTUL = DVDNTL;
    }

    // Begins a 64-bit by 32-bit signed division calculation, storing the 32-bit quotient in DVDNTL
    // and the 32-bit remainder in DVDNTH.
    FORCE_INLINE void Calc64() {
        static constexpr sint32 kMinValue32 = std::numeric_limits<sint32>::min();
        static constexpr sint32 kMaxValue32 = std::numeric_limits<sint32>::max();
        static constexpr sint64 kMinValue64 = std::numeric_limits<sint64>::min();

        sint64 dividend = (static_cast<sint64>(DVDNTH) << 32ll) | static_cast<sint64>(DVDNTL);
        const sint32 divisor = static_cast<sint32>(DVSR);

        bool overflow = divisor == 0;

        if (dividend == -0x80000000ll && divisor == -1) {
            DVDNTH = DVDNTUH = 0;
            DVDNTL = DVDNTUL = 0x80000000;
            return;
        }

        if (!overflow) {
            if (dividend == kMinValue64 && divisor == -1) [[unlikely]] {
                overflow = true;
            } else {
                const sint64 quotient = dividend / divisor;
                const sint32 remainder = dividend % divisor;

                if (quotient <= kMinValue32 || quotient > kMaxValue32) [[unlikely]] {
                    // Overflow cases
                    overflow = true;
                } else if (dividend == kMinValue64 && divisor == -1) [[unlikely]] {
                    // Handle extreme case
                    overflow = true;
                } else {
                    // TODO: schedule event to run this after 39 cycles
                    DVDNTL = DVDNT = quotient;
                    DVDNTH = remainder;
                }
            }
        }

        if (overflow) {
            // Overflow is detected after 6 cycles

            // Perform partial division
            // The division unit uses 3 cycles to set up flags, leaving 3 cycles for calculations
            const sint64 origDividend = dividend;
            bool Q = dividend < 0;
            const bool M = divisor < 0;
            for (int i = 0; i < 3; i++) {
                if (Q == M) {
                    dividend -= static_cast<uint64>(divisor) << 32ull;
                } else {
                    dividend += static_cast<uint64>(divisor) << 32ull;
                }

                Q = dividend < 0;
                dividend = (dividend << 1ll) | (Q == M);
            }

            // Update output registers
            if (DVCR.OVFIE) {
                DVDNTL = DVDNT = dividend;
            } else {
                // DVDNT/DVDNTL is saturated if the interrupt signal is disabled
                DVDNTL = DVDNT = static_cast<sint32>((origDividend >> 32) ^ divisor) < 0 ? kMinValue32 : kMaxValue32;
            }
            DVDNTH = dividend >> 32ll;

            // Signal overflow
            DVCR.OVF = 1;
        }

        DVDNTUH = DVDNTH;
        DVDNTUL = DVDNTL;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SH2State::DIVU &state) const {
        state.DVSR = DVSR;
        state.DVDNT = DVDNT;
        state.DVCR = DVCR.Read();
        state.DVDNTH = DVDNTH;
        state.DVDNTL = DVDNTL;
        state.DVDNTUH = DVDNTUH;
        state.DVDNTUL = DVDNTUL;
    }

    void LoadState(const state::SH2State::DIVU &state) {
        DVSR = state.DVSR;
        DVDNT = state.DVDNT;
        DVCR.Write(state.DVCR);
        DVDNTH = state.DVDNTH;
        DVDNTL = state.DVDNTL;
        DVDNTUH = state.DVDNTUH;
        DVDNTUL = state.DVDNTUL;
    }
};

} // namespace ymir::sh2
