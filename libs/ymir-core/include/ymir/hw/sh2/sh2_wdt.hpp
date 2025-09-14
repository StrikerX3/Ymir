#pragma once

#include <ymir/state/state_sh2.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <cassert>

namespace ymir::sh2 {

struct WatchdogTimer {
    static constexpr uint64 kDividerShifts[] = {1, 6, 7, 8, 9, 10, 12, 13};

    enum class Event { None, Reset, RaiseInterrupt };

    WatchdogTimer() {
        Reset(false);
    }

    void Reset(bool watchdogInitiated) {
        WTCSR.Reset();
        WTCNT = 0x00;
        if (!watchdogInitiated) {
            RSTCSR.Reset();
        }

        m_cycleCount = 0;
        m_clockDividerShift = kDividerShifts[WTCSR.CKSn];
    }

    // Advances the cycle counter to the specified amount
    FORCE_INLINE Event AdvanceTo(uint64 cycles) {
        // Must be monotonically increasing
        assert(cycles >= m_cycleCount);

        if (!WTCSR.TME) {
            m_cycleCount = cycles;
            return Event::None;
        }

        const uint64 steps = (cycles >> m_clockDividerShift) - (m_cycleCount >> m_clockDividerShift);
        m_cycleCount = cycles;
        if (steps == 0) {
            return Event::None;
        }

        Event event = Event::None;

        uint64 nextCount = WTCNT + steps;
        if (nextCount >= 0x100) {
            if (WTCSR.WT_nIT) {
                // Watchdog timer mode
                RSTCSR.WOVF = 1;
                if (RSTCSR.RSTE) {
                    event = Event::Reset;
                }
            } else {
                // Interval timer mode
                WTCSR.OVF = 1;
                event = Event::RaiseInterrupt;
            }
        }
        WTCNT = nextCount;

        return event;
    }

    // -------------------------------------------------------------------------
    // Registers

    // addr r/w  access   init      code    name
    // 080  R    8        18        WTCSR   Watchdog Timer Control/Status Register
    // 080  W    8        18        WTCSR   Watchdog Timer Control/Status Register
    // 088  R    8        18        WTCSR   Watchdog Timer Control/Status Register
    // 088  W    8        18        WTCSR   Watchdog Timer Control/Status Register
    //
    //   bits   r/w  code     description
    //      7   R/W  OVF      Overflow Flag
    //      6   R/W  WT/!IT   Timer Mode Select (0=interval timer (ITI), 1=watchdog timer)
    //      5   R/W  TME      Timer Enable
    //    4-3   R    -        Reserved - must be one
    //    2-0   R/W  CKS2-0   Clock Select
    //                          000 (0) = phi/2
    //                          001 (1) = phi/64
    //                          010 (2) = phi/128
    //                          011 (3) = phi/256
    //                          100 (4) = phi/512
    //                          101 (5) = phi/1024
    //                          110 (6) = phi/4096
    //                          111 (7) = phi/8192
    struct RegWTCSR {
        RegWTCSR() {
            Reset();
        }

        void Reset() {
            OVF = false;
            WT_nIT = false;
            TME = false;
            CKSn = 0;

            OVFread = false;
        }

        bool OVF;    //   7   R/W  OVF      Overflow Flag
        bool WT_nIT; //   6   R/W  WT/!IT   Timer Mode Select (0=interval timer (ITI), 1=watchdog timer)
        bool TME;    //   5   R/W  TME      Timer Enable
        uint8 CKSn;  // 2-0   R/W  CKS2-0   Clock Select

        // Has the OVF been read as true?
        // Necessary to mask clears.
        mutable bool OVFread;
    } WTCSR;

    template <bool peek>
    FORCE_INLINE uint8 ReadWTCSR() const {
        uint8 value = 0;
        bit::deposit_into<7>(value, WTCSR.OVF);
        bit::deposit_into<6>(value, WTCSR.WT_nIT);
        bit::deposit_into<5>(value, WTCSR.TME);
        bit::deposit_into<3, 4>(value, 0b11);
        bit::deposit_into<0, 2>(value, WTCSR.CKSn);
        if constexpr (!peek) {
            WTCSR.OVFread = WTCSR.OVF;
        }
        return value;
    }

    template <bool poke>
    FORCE_INLINE void WriteWTCSR(uint8 value) {
        if constexpr (poke) {
            WTCSR.OVF = bit::test<7>(value);
        } else {
            WTCSR.OVF &= bit::test<7>(value) || !WTCSR.OVFread;
        }
        WTCSR.WT_nIT = bit::test<6>(value);
        WTCSR.TME = bit::test<5>(value);
        WTCSR.CKSn = bit::extract<0, 2>(value);
        if (!WTCSR.TME) {
            WTCNT = 0;
            WTCSR.OVF = 0;
        }

        m_clockDividerShift = kDividerShifts[WTCSR.CKSn];
    }

    // 081  R    8        00        WTCNT   Watchdog Timer Counter
    // 080  W    8        00        WTCNT   Watchdog Timer Counter
    // 089  R    8        00        WTCNT   Watchdog Timer Counter
    // 088  W    8        00        WTCNT   Watchdog Timer Counter
    //
    //   bits   r/w  code     description
    //    7-0   R/W  WTCNT    Watchdog timer counter
    uint8 WTCNT;

    FORCE_INLINE uint8 ReadWTCNT() const {
        return WTCNT;
    }

    FORCE_INLINE void WriteWTCNT(uint8 value) {
        if (WTCSR.TME) {
            WTCNT = value;
        }
    }

    // 083  R    8        1F        RSTCSR  Reset Control/Status Register
    // 082  W    8        1F        RSTCSR  Reset Control/Status Register
    // 08B  R    8        1F        RSTCSR  Reset Control/Status Register
    // 08A  W    8        1F        RSTCSR  Reset Control/Status Register
    //
    //   bits   r/w  code     description
    //      7   R/W  WOVF     Watchdog Timer Overflow Flag
    //      6   R/W  RSTE     Reset Enable
    //      5   R/W  RSTS     Reset Select (0=power-on reset, 1=manual reset)
    //    4-0   R    -        Reserved - must be one
    struct RegRSTCSR {
        RegRSTCSR() {
            Reset();
        }

        void Reset() {
            WOVF = false;
            RSTE = false;
            RSTS = false;
        }

        bool WOVF; // 7   R/W  WOVF     Watchdog Timer Overflow Flag
        bool RSTE; // 6   R/W  RSTE     Reset Enable
        bool RSTS; // 5   R/W  RSTS     Reset Select (0=power-on reset, 1=manual reset)
    } RSTCSR;

    FORCE_INLINE uint8 ReadRSTCSR() const {
        uint8 value = 0;
        bit::deposit_into<7>(value, RSTCSR.WOVF);
        bit::deposit_into<6>(value, RSTCSR.RSTE);
        bit::deposit_into<5>(value, RSTCSR.RSTS);
        bit::deposit_into<0, 4>(value, 0b11111);
        return value;
    }

    template <bool poke>
    FORCE_INLINE void WriteRSTCSR(uint8 value) {
        if constexpr (poke) {
            RSTCSR.WOVF = bit::test<7>(value);
        } else {
            RSTCSR.WOVF &= bit::test<7>(value);
        }
        RSTCSR.RSTE = bit::test<6>(value);
        RSTCSR.RSTS = bit::test<5>(value);
    }

    FORCE_INLINE void WriteRSTE_RSTS(uint8 value) {
        RSTCSR.RSTE = bit::test<6>(value);
        RSTCSR.RSTS = bit::test<5>(value);
    }

    template <bool poke>
    FORCE_INLINE void WriteWOVF(uint8 value) {
        if constexpr (poke) {
            RSTCSR.WOVF = bit::test<7>(value);
        } else {
            RSTCSR.WOVF &= bit::test<7>(value);
        }
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SH2State::WDT &state) const {
        state.WTCSR = ReadWTCSR<true>();
        state.WTCNT = ReadWTCNT();
        state.RSTCSR = ReadRSTCSR();
        state.cycleCount = m_cycleCount;
    }

    void LoadState(const state::SH2State::WDT &state) {
        WriteWTCSR<true>(state.WTCSR);
        WriteWTCNT(state.WTCNT);
        WriteRSTCSR<true>(state.RSTCSR);
        m_cycleCount = state.cycleCount;
    }

private:
    // -------------------------------------------------------------------------
    // State

    uint64 m_cycleCount;
    uint64 m_clockDividerShift; // derived from WTCSR.CKS
};

} // namespace ymir::sh2
