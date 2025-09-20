#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <array>

namespace ymir::sh1 {

struct IntegratedTimerPulseUnit {
    IntegratedTimerPulseUnit() {
        Reset();
    }

    void Reset() {
        for (auto &timer : timers) {
            timer.Reset();
        }

        ch2PhaseCountingMode = false;
        ch2OVFOnUnderflow = false;
        ch34ComboMode = CombinationMode::Normal0;

        invertOutputLevel3 = false;
        invertOutputLevel4 = false;
    }

    // 100  R/W  8        E0/60     TSTR    Timer start register
    //
    //   bits   r/w  code       description
    //      7   -    -          Reserved - floating value
    //    6-5   -    -          Reserved - must be one
    //      4   R/W  STR4       Count Start 4 (0=halted, 1=counting)
    //      3   R/W  STR3       Count Start 3 (0=halted, 1=counting)
    //      2   R/W  STR2       Count Start 2 (0=halted, 1=counting)
    //      1   R/W  STR1       Count Start 1 (0=halted, 1=counting)
    //      0   R/W  STR0       Count Start 0 (0=halted, 1=counting)

    FORCE_INLINE uint8 ReadTSTR() const {
        uint8 value = 0;
        bit::deposit_into<5, 6>(value, 0b11);
        bit::deposit_into<4>(value, timers[4].started);
        bit::deposit_into<3>(value, timers[3].started);
        bit::deposit_into<2>(value, timers[2].started);
        bit::deposit_into<1>(value, timers[1].started);
        bit::deposit_into<0>(value, timers[0].started);
        return value;
    }

    FORCE_INLINE void WriteTSTR(uint8 value) {
        timers[4].started = bit::test<4>(value);
        timers[3].started = bit::test<3>(value);
        timers[2].started = bit::test<2>(value);
        timers[1].started = bit::test<1>(value);
        timers[0].started = bit::test<0>(value);
    }

    // 101  R/W  8        E0/60     TSNC    Timer synchro register
    //
    //   bits   r/w  code       description
    //      7   -    -          Reserved - floating value
    //    6-5   -    -          Reserved - must be one
    //      4   R/W  SYNC4      Timer Synchro 4 (0=independent, 1=synchronized preset/clear)
    //      3   R/W  SYNC3      Timer Synchro 3 (0=independent, 1=synchronized preset/clear)
    //      2   R/W  SYNC2      Timer Synchro 2 (0=independent, 1=synchronized preset/clear)
    //      1   R/W  SYNC1      Timer Synchro 1 (0=independent, 1=synchronized preset/clear)
    //      0   R/W  SYNC0      Timer Synchro 0 (0=independent, 1=synchronized preset/clear)

    FORCE_INLINE uint8 ReadTSNC() const {
        uint8 value = 0;
        bit::deposit_into<5, 6>(value, 0b11);
        bit::deposit_into<4>(value, timers[4].sync);
        bit::deposit_into<3>(value, timers[3].sync);
        bit::deposit_into<2>(value, timers[2].sync);
        bit::deposit_into<1>(value, timers[1].sync);
        bit::deposit_into<0>(value, timers[0].sync);
        return value;
    }

    FORCE_INLINE void WriteTSNC(uint8 value) {
        timers[4].sync = bit::test<4>(value);
        timers[3].sync = bit::test<3>(value);
        timers[2].sync = bit::test<2>(value);
        timers[1].sync = bit::test<1>(value);
        timers[0].sync = bit::test<0>(value);
    }

    // 102  R/W  8        80/00     TMDR    Timer mode register
    //
    //   bits   r/w  code       description
    //      7   -    -          Reserved - floating value
    //      6   R/W  MDF        Phase Counting Mode
    //                            Affects channel 2 only.
    //                            0 = Normal
    //                            1 = Phase counting mode
    //      5   R/W  FDIR       Flag Direction (TSR2.OVF set when TCNT2: 0=overflows or underflows, 1=overflows only)
    //      4   R/W  PWM4       PWM Mode 4 (0=normal, 1=PWM mode)
    //      3   R/W  PWM3       PWM Mode 3 (0=normal, 1=PWM mode)
    //      2   R/W  PWM2       PWM Mode 2 (0=normal, 1=PWM mode)
    //      1   R/W  PWM1       PWM Mode 1 (0=normal, 1=PWM mode)
    //      0   R/W  PWM0       PWM Mode 0 (0=normal, 1=PWM mode)

    FORCE_INLINE uint8 ReadTMDR() const {
        uint8 value = 0;
        bit::deposit_into<6>(value, ch2PhaseCountingMode);
        bit::deposit_into<5>(value, ch2OVFOnUnderflow);
        bit::deposit_into<4>(value, timers[4].pwm);
        bit::deposit_into<3>(value, timers[3].pwm);
        bit::deposit_into<2>(value, timers[2].pwm);
        bit::deposit_into<1>(value, timers[1].pwm);
        bit::deposit_into<0>(value, timers[0].pwm);
        return value;
    }

    FORCE_INLINE void WriteTMDR(uint8 value) {
        ch2PhaseCountingMode = bit::test<6>(value);
        ch2OVFOnUnderflow = bit::test<5>(value);
        timers[4].pwm = bit::test<4>(value);
        timers[3].pwm = bit::test<3>(value);
        timers[2].pwm = bit::test<2>(value);
        timers[1].pwm = bit::test<1>(value);
        timers[0].pwm = bit::test<0>(value);
    }

    // 103  R/W  8        C0/40     TFCR    Timer function register
    //
    //   bits   r/w  code       description
    //      7   -    -          Reserved - floating value
    //      6   -    -          Reserved - must be one
    //    5-4   R/W  CMD1-0     Combination Mode
    // 	                          00 (0) = Channels 3 and 4 operate normally
    // 	                          01 (1) = Channels 3 and 4 operate normally
    // 	                          10 (2) = Channels 3 and 4 operate together in complementary PWM mode
    // 	                          11 (3) = Channels 3 and 4 operate together in reset-synchronized PWM mode
    //      3   R/W  BFB4       Buffer Mode B4 (0=GRB4 normal, 1=GRB4+BRB4 in buffer mode)
    //      2   R/W  BFA4       Buffer Mode A4 (0=GRA4 normal, 1=GRA4+BRA4 in buffer mode)
    //      1   R/W  BFB3       Buffer Mode B3 (0=GRB3 normal, 1=GRB3+BRB3 in buffer mode)
    //      0   R/W  BFA3       Buffer Mode A3 (0=GRA3 normal, 1=GRA3+BRA3 in buffer mode)

    FORCE_INLINE uint8 ReadTFCR() const {
        uint8 value = 0;
        bit::deposit_into<6>(value, 0b1);
        bit::deposit_into<4, 5>(value, static_cast<uint8>(ch34ComboMode));
        bit::deposit_into<3>(value, timers[4].bufferModeB);
        bit::deposit_into<2>(value, timers[4].bufferModeA);
        bit::deposit_into<1>(value, timers[3].bufferModeB);
        bit::deposit_into<0>(value, timers[3].bufferModeA);
        return value;
    }

    FORCE_INLINE void WriteTFCR(uint8 value) {
        ch34ComboMode = static_cast<CombinationMode>(bit::extract<4, 5>(value));
        timers[4].bufferModeB = bit::test<3>(value);
        timers[4].bufferModeA = bit::test<2>(value);
        timers[3].bufferModeB = bit::test<1>(value);
        timers[3].bufferModeA = bit::test<0>(value);
    }

    // 131  R/W  8        FF/7F     TOCR    Timer output control register
    //
    //   bits   r/w  code       description
    //      7   -    -          Reserved - floating value
    //    6-2   -    -          Reserved - must be one
    //      1   R/W  OLS4       Output Level Select 4 (TIOCA3, TIOCA4, TIOCB4 output: 0=inverted, 1=normal)
    //      0   R/W  OLS3       Output Level Select 3 (TIOCB3, TOCXA4, TOCXB4 output: 0=inverted, 1=normal)

    FORCE_INLINE uint8 ReadTOCR() const {
        uint8 value = 0;
        bit::deposit_into<2, 6>(value, 0b11111);
        bit::deposit_into<1>(value, !invertOutputLevel4);
        bit::deposit_into<0>(value, !invertOutputLevel3);
        return value;
    }

    FORCE_INLINE void WriteTOCR(uint8 value) {
        invertOutputLevel4 = !bit::test<1>(value);
        invertOutputLevel3 = !bit::test<0>(value);
    }

    struct Timer {
        Timer() {
            Reset();
        }

        void Reset() {
            started = false;
            sync = false;
            pwm = false;

            bufferModeA = false;
            bufferModeB = false;

            clearMode = ClearMode::NoClear;
            extClockEdgeMode = ExtClockEdgeMode::Rising;
            prescaler = Prescaler::Phi;

            GRAMode = GRMode::OutputNone;
            GRBMode = GRMode::OutputNone;

            IMFAIntrEnable = false;
            IMFBIntrEnable = false;
            OVFIntrEnable = false;

            IMFA = false;
            IMFB = false;
            OVF = false;

            IMFAread = false;
            IMFBread = false;
            OVFread = false;

            counter = 0x0000;

            GRA = GRB = 0xFFFF;
            BRA = BRB = 0xFFFF;

            currCycles = 0;
        }

        // 104  R/W  8        80/00     TCR0    Timer control register 0
        // 10E  R/W  8        80/00     TCR1    Timer control register 1
        // 118  R/W  8        80/00     TCR2    Timer control register 2
        // 122  R/W  8        80/00     TCR3    Timer control register 3
        // 132  R/W  8        80/00     TCR4    Timer control register 4
        //
        //   bits   r/w  code       description
        //      7   -    -          Reserved - floating value
        //    6-5   R/W  CCLR1-0    Counter Clear
        // 	                          00 (0) = TCNT is not cleared
        // 	                          01 (1) = TCNT cleared on GRA compare match or input capture based on GR mode
        // 	                          10 (2) = TCNT cleared on GRB compare match or input capture based on GR mode
        // 	                          11 (3) = TCNT cleared in sync with clear of other timers in sync mode (see TSNC)
        //    4-3   R/W  CKEG1-0    External Clock Edge
        // 	                          00 (0) = Count rising edges
        // 	                          01 (1) = Count falling edges
        // 	                          10 (2) = Count both rising and falling edges
        // 	                          11 (3) = Count both rising and falling edges
        //    2-0   R/W  TPS2-0     Timer Prescaler
        // 	                          000 (0) = Internal clock phi
        // 	                          001 (1) = Internal clock phi/2
        // 	                          010 (2) = Internal clock phi/4
        // 	                          011 (3) = Internal clock phi/8
        // 	                          100 (4) = External clock A (TCLKA)
        // 	                          101 (5) = External clock A (TCLKB)
        // 	                          110 (6) = External clock A (TCLKC)
        // 	                          111 (7) = External clock A (TCLKD)

        FORCE_INLINE uint8 ReadTCR() const {
            uint8 value = 0;
            bit::deposit_into<5, 6>(value, static_cast<uint8>(clearMode));
            bit::deposit_into<3, 4>(value, static_cast<uint8>(extClockEdgeMode));
            bit::deposit_into<0, 2>(value, static_cast<uint8>(prescaler));
            return value;
        }

        FORCE_INLINE void WriteTCR(uint8 value) {
            clearMode = static_cast<ClearMode>(bit::extract<5, 6>(value));
            extClockEdgeMode = static_cast<ExtClockEdgeMode>(bit::extract<3, 4>(value));
            prescaler = static_cast<Prescaler>(bit::extract<0, 2>(value));
        }

        // 105  R/W  8        88/08     TIOR0   Timer I/O control register 0
        // 10F  R/W  8        88/08     TIOR1   Timer I/O control register 1
        // 119  R/W  8        88/08     TIOR2   Timer I/O control register 2
        // 123  R/W  8        88/08     TIOR3   Timer I/O control register 3
        // 133  R/W  8        88/08     TIOR4   Timer I/O control register 4
        //
        //   bits   r/w  code       description
        //      7   -    -          Reserved - floating value
        //    6-4   R/W  IOB2-0     I/O Control B
        // 	                          000 (0) = GRB=output compare; Compare match with pin output disabled
        // 	                          001 (1) = GRB=output compare; 0 output at GRB compare match
        // 	                          010 (2) = GRB=output compare; 1 output at GRB compare match
        // 	                          011 (3) = GRB=output compare; Output toggles at GRB compare match
        //                                                          (except channel 2, which outputs 1 instead)
        // 	                          100 (4) = GRB=input capture on rising edge
        // 	                          101 (5) = GRB=input capture on falling edge
        // 	                          110 (6) = GRB=input capture on both edges
        // 	                          111 (7) = GRB=input capture on both edges
        //      3   R    -          Reserved - must be zero
        //    2-0   R/W  IOA2-0     I/O Control A
        // 	                          000 (0) = GRA=output compare; Compare match with pin output disabled
        // 	                          001 (1) = GRA=output compare; 0 output at GRA compare match
        // 	                          010 (2) = GRA=output compare; 1 output at GRA compare match
        // 	                          011 (3) = GRA=output compare; Output toggles at GRA compare match
        //                                                          (except channel 2, which outputs 1 instead)
        // 	                          100 (4) = GRA=input capture on rising edge
        // 	                          101 (5) = GRA=input capture on falling edge
        // 	                          110 (6) = GRA=input capture on both edges
        // 	                          111 (7) = GRA=input capture on both edges

        FORCE_INLINE uint8 ReadTIOR() const {
            uint8 value = 0;
            bit::deposit_into<4, 6>(value, static_cast<uint8>(GRBMode));
            bit::deposit_into<0, 2>(value, static_cast<uint8>(GRAMode));
            return value;
        }

        FORCE_INLINE void WriteTIOR(uint8 value) {
            GRBMode = static_cast<GRMode>(bit::extract<4, 6>(value));
            GRAMode = static_cast<GRMode>(bit::extract<0, 2>(value));
        }

        // 106  R/W  8        F8/78     TIER0   Timer interrupt enable register 0
        // 110  R/W  8        F8/78     TIER1   Timer interrupt enable register 1
        // 11A  R/W  8        F8/78     TIER2   Timer interrupt enable register 2
        // 124  R/W  8        F8/78     TIER3   Timer interrupt enable register 3
        // 134  R/W  8        F8/78     TIER4   Timer interrupt enable register 4
        //
        //   bits   r/w  code       description
        //      7   -    -          Reserved - floating value
        //    6-3   -    -          Reserved - must be one
        //      2   R/W  OVIE       Overflow Interrupt Enable (0=disable, 1=enable)
        //      1   R/W  IMIEB      Input Capture/Output Match Interrupt Enable B (0=disable, 1=enable)
        //      0   R/W  IMIEA      Input Capture/Output Match Interrupt Enable A (0=disable, 1=enable)

        FORCE_INLINE uint8 ReadTIER() const {
            uint8 value = 0;
            bit::deposit_into<3, 6>(value, 0b1111);
            bit::deposit_into<2>(value, OVFIntrEnable);
            bit::deposit_into<1>(value, IMFBIntrEnable);
            bit::deposit_into<0>(value, IMFAIntrEnable);
            return value;
        }

        FORCE_INLINE void WriteTIER(uint8 value) {
            OVFIntrEnable = bit::test<2>(value);
            IMFBIntrEnable = bit::test<1>(value);
            IMFAIntrEnable = bit::test<0>(value);
        }

        // 107  R/W* 8        F8/78     TSR0    Timer status register 0
        // 111  R/W* 8        F8/78     TSR1    Timer status register 1
        // 11B  R/W* 8        F8/78     TSR2    Timer status register 2
        // 125  R/W* 8        F8/78     TSR3    Timer status register 3
        // 135  R/W* 8        F8/78     TSR4    Timer status register 4
        //
        //   bits   r/w  code       description
        //      7   -    -          Reserved - floating value
        //    6-3   -    -          Reserved - must be one
        //      2   R/W* OVF        Overflow Flag
        //                            Set when TCNT overflows from FFFF to 0000 or underflows from 0000 to FFFF
        //      1   R/W* IMFB       Input Capture/Compare Match B
        //                            Set when GRB is output compare and TCNT == GRB, or GRB is input capture and input
        //                            capture signal is raised
        //      0   R/W* IMFA       Input Capture/Compare Match A
        //                            Set when GRA is output compare and TCNT == GRA, or GRA is input capture and input
        //                            capture signal is raised
        //
        // All three flags are cleared by reading 1 then writing 0. Writing 1 has no effect.

        FORCE_INLINE uint8 ReadTSR() const {
            uint8 value = 0;
            bit::deposit_into<3, 6>(value, 0b1111);
            bit::deposit_into<2>(value, OVF);
            bit::deposit_into<1>(value, IMFB);
            bit::deposit_into<0>(value, IMFA);
            OVFread = OVF;
            IMFBread = IMFB;
            IMFAread = IMFA;
            return value;
        }

        template <bool poke>
        FORCE_INLINE void WriteTSR(uint8 value) {
            if constexpr (poke) {
                OVF = bit::test<2>(value);
                IMFB = bit::test<1>(value);
                IMFA = bit::test<0>(value);
            } else {
                OVF &= bit::test<2>(value) || !OVFread;
                IMFB &= bit::test<1>(value) || !IMFBread;
                IMFA &= bit::test<0>(value) || !IMFAread;
            }
        }

        // 108  R/W  8,16,32  0000      TCNT0   Timer counter 0
        // 112  R/W  8,16     0000      TCNT1   Timer counter 1
        // 11C  R/W  8,16,32  0000      TCNT2   Timer counter 2
        // 126  R/W  8,16     0000      TCNT3   Timer counter 3
        // 136  R/W  8,16     0000      TCNT4   Timer counter 4
        //
        //   bits   r/w  code       description
        //   15-0   R/W  TCNT       Timer counter
        //                            Channel 0: Increment counter
        //                            Channel 1: Increment counter
        //                            Channel 2: Increment/decrement in phase counting mode, increment otherwise
        //                            Channel 3: Increment/decrement in complementary PWM mode, increment otherwise
        //                            Channel 4: Increment/decrement in complementary PWM mode, increment otherwise

        FORCE_INLINE uint16 ReadTCNT() const {
            return counter;
        }

        FORCE_INLINE void WriteTCNT(uint16 value) {
            counter = value;
        }
        FORCE_INLINE void WriteTCNTHi(uint8 value) {
            bit::deposit_into<0, 7>(counter, value);
        }
        FORCE_INLINE void WriteTCNTLo(uint8 value) {
            bit::deposit_into<8, 15>(counter, value);
        }

        // 10A  R/W  8,16,32  FFFF      GRA0    General register A0
        // 10C  R/W  8,16     FFFF      GRB0    General register B0
        // 114  R/W  8,16,32  FFFF      GRA1    General register A1
        // 116  R/W  8,16,32  FFFF      GRB1    General register B1
        // 11E  R/W  8,16,32  FFFF      GRA2    General register A2
        // 120  R/W  8,16     FFFF      GRB2    General register B2
        // 128  R/W  8,16,32  FFFF      GRA3    General register A3
        // 12A  R/W  8,16,32  FFFF      GRB3    General register B3
        // 138  R/W  8,16,32  FFFF      GRA4    General register A4
        // 13A  R/W  8,16,32  FFFF      GRB4    General register B4
        //
        //   bits   r/w  code       description
        //   15-0   R/W  GRA/B      General register A/B
        //
        // Can be used as output compare/input capture dual register.
        // Can be used in buffered mode in Channels 3 and 4 with corresponding BRA and BRB registers.

        FORCE_INLINE uint16 ReadGRA() const {
            return GRA;
        }

        FORCE_INLINE void WriteGRA(uint16 value) {
            GRA = value;
        }
        FORCE_INLINE void WriteGRAHi(uint8 value) {
            bit::deposit_into<0, 7>(GRA, value);
        }
        FORCE_INLINE void WriteGRALo(uint8 value) {
            bit::deposit_into<8, 15>(GRA, value);
        }

        FORCE_INLINE uint16 ReadGRB() const {
            return GRB;
        }

        FORCE_INLINE void WriteGRB(uint16 value) {
            GRB = value;
        }
        FORCE_INLINE void WriteGRBHi(uint8 value) {
            bit::deposit_into<0, 7>(GRB, value);
        }
        FORCE_INLINE void WriteGRBLo(uint8 value) {
            bit::deposit_into<8, 15>(GRB, value);
        }

        // 12C  R/W  8,16,32  FFFF      BRA3    Buffer register A3
        // 12E  R/W  8,16,32  FFFF      BRB3    Buffer register B3
        // 13C  R/W  8,16,32  FFFF      BRA4    Buffer register A4
        // 13E  R/W  8,16,32  FFFF      BRB4    Buffer register B4
        //
        //   bits   r/w  code       description
        //   15-0   R/W  BRA/B      Buffer register A/B
        //
        // Only used in buffer operations.
        //
        // When the corresponding GRA and GRB are output compare registers, the buffer registers function as output
        // compare buffer registers that can automatically transfer the BRA and BRB values to GRA and GRB upon a compare
        // match.
        //
        // When the corresponding GRA and GRB are input capture registers, the buffer registers function as input
        // capture buffer registers that can automatically transfer the values stored until an input capture in the GRA
        // and GRB to the BRA and BRB.

        FORCE_INLINE uint16 ReadBRA() const {
            return BRA;
        }

        FORCE_INLINE void WriteBRA(uint16 value) {
            BRA = value;
        }
        FORCE_INLINE void WriteBRAHi(uint8 value) {
            bit::deposit_into<0, 7>(BRA, value);
        }
        FORCE_INLINE void WriteBRALo(uint8 value) {
            bit::deposit_into<8, 15>(BRA, value);
        }

        FORCE_INLINE uint16 ReadBRB() const {
            return BRB;
        }

        FORCE_INLINE void WriteBRB(uint16 value) {
            BRB = value;
        }
        FORCE_INLINE void WriteBRBHi(uint8 value) {
            bit::deposit_into<0, 7>(BRB, value);
        }
        FORCE_INLINE void WriteBRBLo(uint8 value) {
            bit::deposit_into<8, 15>(BRB, value);
        }

        // Is the timer counting?
        // Derived from TSTR.STRn
        bool started;

        // Is the timer in synchronous mode?
        // Derived from TSNC.SYNCn
        bool sync;

        // Is the timer in PWM mode?
        // Derived from TMDR.PWMn
        bool pwm;

        // Whether to use buffered operation with the GRA/B and BRA/B registers.
        // Only valid for channels 3 and 4.
        // Derived from TFCR.BFA3, BFA4, BFB3 and BFB4
        bool bufferModeA, bufferModeB;

        enum class ClearMode : uint8 { NoClear, GRA, GRB, Sync };

        // Counter clear mode.
        // Derived from TCRn.CCLR1-0
        ClearMode clearMode;

        enum class ExtClockEdgeMode : uint8 { Rising, Falling, Both2, Both3 };

        // External clock edge detection mode.
        // Derived from TCRn.CKEG1-0
        ExtClockEdgeMode extClockEdgeMode;

        enum class Prescaler : uint8 { Phi, Phi2, Phi4, Phi8, TCLKA, TCLKB, TCLKC, TCLKD };

        // Timer prescaler selection.
        // Derived from TCRn.TPS2-0
        Prescaler prescaler;

        enum class GRMode : uint8 {
            OutputNone,
            Output0,
            Output1,
            OutputToggle,
            InputRising,
            InputFalling,
            InputBoth6,
            InputBoth7
        };

        // General register output compare/input capture mode selection.
        // For channel 2, OutputToggle is treated as Output1.
        // Derived from TIORn.IOA/B2-0
        GRMode GRAMode, GRBMode;

        // Whether to output an interrupt signal on input capture/output match A.
        // Derived from TIERn.IMIEA
        bool IMFAIntrEnable;

        // Whether to output an interrupt signal on input capture/output match B.
        // Derived from TIERn.IMIEB
        bool IMFBIntrEnable;

        // Whether to output an interrupt signal on overflow/underflow.
        // Derived from TIERn.OVIE
        bool OVFIntrEnable;

        // Input capture/compare match A signal state.
        // Derived from TSRn.IMFA
        bool IMFA;

        // Input capture/compare match B signal state.
        // Derived from TSRn.IMFB
        bool IMFB;

        // Overflow signal state.
        // Derived from TSRn.OVF
        bool OVF;

        // Input capture/compare match A signal state on last read.
        // Used for clearing the flag on writes.
        mutable bool IMFAread;

        // Input capture/compare match B signal state on last read.
        // Used for clearing the flag on writes.
        mutable bool IMFBread;

        // Overflow signal state on last read.
        // Used for clearing the flag on writes.
        mutable bool OVFread;

        // Timer counter (TCNTn).
        uint16 counter;

        // General registers.
        uint16 GRA, GRB;

        // Buffer registers.
        // Only valid for channels 3 and 4.
        uint16 BRA, BRB;

        // Current cycles count.
        uint64 currCycles;
    };

    std::array<Timer, 5> timers;

    // Whether channel 2 is in normal mode (false) or phase counting mode (true).
    // Derived from TMDR.MDF
    bool ch2PhaseCountingMode;

    // Whether channel 2 triggers OVF when TCNT2 underflows from 0000 to FFFF.
    // OVF is always triggered on overflows from FFFF to 0000.
    // Derived from TMDR.FDIR
    bool ch2OVFOnUnderflow;

    enum class CombinationMode : uint8 { Normal0, Normal1, ComplementaryPWM, ResetSyncPWM };

    // Combination mode for channels 3 and 4.
    // Derived from TFCR.CMD1-0
    CombinationMode ch34ComboMode;

    // Invert output levels for TIOCB3, TOCXA4, and TOCXB4.
    // Derived from TOCR.OLS3 (inverted value)
    bool invertOutputLevel3;

    // Invert output levels for TIOCA3, TIOCA4, and TIOCB4.
    // Derived from TOCR.OLS4 (inverted value)
    bool invertOutputLevel4;
};

} // namespace ymir::sh1
