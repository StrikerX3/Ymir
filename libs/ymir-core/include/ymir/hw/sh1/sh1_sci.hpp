#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

#include <array>

namespace ymir::sh1 {

struct SerialCommunicationInterface {
    SerialCommunicationInterface() {
        Reset();
    }

    void Reset() {
        channels[0].Reset();
        channels[1].Reset();
    }

    struct Channel {
        Channel() {
            Reset();
        }

        void Reset() {
            currCycles = 0;

            TDR = 0xFF;
            TDRvalid = false;
            RDR = 0x00;

            RSR = 0x00;
            RSRbit = 1u;

            TSR = 0x00;
            TSRbit = 0u;

            sync = false;
            char7Bit = false;
            parityEnable = false;
            parityOdd = false;
            stopBitTwo = false;
            multiprocessor = false;
            clockShift = 0;

            BRR = 0xFF;
            cyclesPerBit = (BRR + 1ull) << 4ull;

            txIntrEnable = false;
            rxIntrEnable = false;
            txEnable = false;
            rxEnable = false;
            multiprocessorIntrEnable = false;
            txEndIntrEnable = false;
            clockEnable = 0;

            txEmpty = true;
            rxFull = false;
            framingError = false;
            parityError = false;
            txEnd = true;
            multiprocessorRxBit = false;
            multiprocessorTxBit = false;

            txEmptyMask = false;
            rxFullMask = false;
            overrunErrorMask = false;
            framingErrorMask = false;
            parityErrorMask = false;
        }

        // Transmits a bit. Returns the value of the bit to be transmitted.
        bool TransmitBit() {
            const bool value = TSR & TSRbit;
            TSRbit <<= 1u;
            if (TSRbit == 0u) {
                // TODO: should probably check txEmpty instead
                if (TDRvalid) {
                    TSRbit = 1u;
                    TSR = TDR;
                    TDRvalid = false;
                    txEmpty = true;
                } else {
                    txEnd = true;
                }
            }
            return value;
        }

        // Receives the specified bit.
        void ReceiveBit(bool value) {
            if (value) {
                RSR |= RSRbit;
            }
            RSRbit <<= 1u;
            if (RSRbit == 0u) {
                RSRbit = 1u;
                RDR = RSR;
                RSR = 0u;
            }
        }

        // Current absolute cycle count.
        uint64 currCycles;

        // 0C0  R/W  8,16     00        SMR0    Serial mode register 0
        // 0C8  R/W  8,16     00        SMR1    Serial mode register 1
        //
        //   bits   r/w  code       description
        //      7   R/W  C/!A       Communication Mode (0=async, 1=sync)
        //      6   R/W  CHR        Character Length (0=8-bit, 1=7-bit)
        //      5   R/W  PE         Parity Enable (0=disable, 1=enable)
        //      4   R/W  O/!E       Parity Mode (0=even, 1=odd)
        //      3   R/W  STOP       Stop Bit Length (0=one, 1=two)
        //      2   R/W  MP         Multiprocessor Mode (0=disable, 1=enable)
        //    1-0   R/W  CKS1-0     Clock Select
        //                            00 (0) = System clock (phi)
        //                            01 (1) = phi/4
        //                            10 (2) = phi/16
        //                            11 (3) = phi/64

        FORCE_INLINE uint8 ReadSMR() const {
            uint8 value = 0;
            bit::deposit_into<7>(value, sync);
            bit::deposit_into<6>(value, char7Bit);
            bit::deposit_into<5>(value, parityEnable);
            bit::deposit_into<4>(value, parityOdd);
            bit::deposit_into<3>(value, stopBitTwo);
            bit::deposit_into<2>(value, multiprocessor);
            bit::deposit_into<0, 1>(value, clockShift >> 1u);
            return value;
        }

        FORCE_INLINE void WriteSMR(uint8 value) {
            sync = bit::test<7>(value);
            char7Bit = bit::test<6>(value);
            parityEnable = bit::test<5>(value);
            parityOdd = bit::test<4>(value);
            stopBitTwo = bit::test<3>(value);
            multiprocessor = bit::test<2>(value);
            clockShift = bit::extract<0, 1>(value) << 1u;
        }

        // 0C1  R/W  8,16     FF        BRR0    Bit rate register 0
        // 0C9  R/W  8,16     FF        BRR1    Bit rate register 1
        //
        //   bits   r/w  code       description
        //    7-0   R/W  -          Bit rate
        uint8 BRR;
        uint64 cyclesPerBit; // derived from BRR

        FORCE_INLINE uint8 ReadBRR() const {
            return BRR;
        }

        FORCE_INLINE void WriteBRR(uint8 value) {
            BRR = value;
            cyclesPerBit = (static_cast<uint64>(BRR) + 1ull) << 2ull;
        }

        // 0C2  R/W  8,16     00        SCR0    Serial control register 0
        // 0CA  R/W  8,16     00        SCR1    Serial control register 1
        //
        //   bits   r/w  code       description
        //      7   R/W  TIE        Transmit Interrupt Enable
        //      6   R/W  RIE        Receive Interrupt Enable
        //      5   R/W  TE         Transmit Enable
        //      4   R/W  RE         Receive Enable
        //      3   R/W  MPIE       Multiprocessor Interrupt Enable
        //      2   R/W  TEIE       Transmit-End Interrupt Enable
        //    1-0   R/W  CKE1-0     Clock Enable
        //                 In asynchronous mode:
        //                   00 (0) = Internal clock, SCK pin used for input or output pin (signal ignored/undefined)
        //                   01 (1) = Internal clock, SCK pin used for clock output (1x bit rate)
        //                   10 (2) = External clock, SCK pin used for clock input (16x bit rate)
        //                   11 (3) = External clock, SCK pin used for clock input (16x bit rate)
        //                 In synchronous mode:
        //                   00 (0) = Internal clock, SCK pin used for serial clock output
        //                   01 (1) = Internal clock, SCK pin used for serial clock output
        //                   10 (2) = External clock, SCK pin used for serial clock input
        //                   11 (3) = External clock, SCK pin used for serial clock input
        //
        // The PFC must be configued to use the SCK pin function.

        FORCE_INLINE uint8 ReadSCR() const {
            uint8 value = 0;
            bit::deposit_into<7>(value, txIntrEnable);
            bit::deposit_into<6>(value, rxIntrEnable);
            bit::deposit_into<5>(value, txEnable);
            bit::deposit_into<4>(value, rxEnable);
            bit::deposit_into<3>(value, multiprocessorIntrEnable);
            bit::deposit_into<2>(value, txEndIntrEnable);
            bit::deposit_into<0, 1>(value, clockEnable);
            return value;
        }

        FORCE_INLINE void WriteSCR(uint8 value) {
            txIntrEnable = bit::test<7>(value);
            rxIntrEnable = bit::test<6>(value);
            txEnable = bit::test<5>(value);
            rxEnable = bit::test<4>(value);
            multiprocessorIntrEnable = bit::test<3>(value);
            txEndIntrEnable = bit::test<2>(value);
            clockEnable = bit::extract<0, 1>(value);
        }

        // 0C3  R/W  8,16     FF        TDR0    Transmit data register 0
        // 0CB  R/W  8,16     FF        TDR1    Transmit data register 1
        //
        //   bits   r/w  code       description
        //    7-0   R/W  -          Transmit data
        uint8 TDR;
        bool TDRvalid; // true if a value was written to TDR

        FORCE_INLINE uint8 ReadTDR() const {
            return TDR;
        }

        FORCE_INLINE void WriteTDR(uint8 value) {
            TDR = value;
            if (TSRbit == 0u) {
                TSR = value;
                TSRbit = 1u;
                txEmpty = true;
            } else {
                TDRvalid = true;
                txEmpty = false;
            }
        }

        // 0C4  R/W* 8,16     84        SSR0    Serial status register 0
        // 0CC  R/W* 8,16     84        SSR1    Serial status register 1
        //
        //   bits   r/w  code       description
        //      7   R/W* TDRE       Transmit Data Register Empty (0=valid data, 1=invalid data)
        //      6   R/W* RDRF       Receive Data Register Full (0=invalid data, 1=valid data)
        //      5   R/W* ORER       Overrun Error (0=no error, 1=overrun error)
        //      4   R/W* FER        Framing Error (0=no error, 1=framing error)
        //      3   R/W* PER        Parity Error (0=no error, 1=parity error)
        //      2   R    TEND       Transmit End (0=in progress, 1=ended)
        //      1   R    MPB        Multiprocessor Bit
        //      0   R/W  MPBT       Multiprocessor Bit Transfer

        template <bool peek>
        FORCE_INLINE uint8 ReadSSR() const {
            uint8 value = 0;
            bit::deposit_into<7>(value, txEmpty);
            bit::deposit_into<6>(value, rxFull);
            bit::deposit_into<5>(value, overrunError);
            bit::deposit_into<4>(value, framingError);
            bit::deposit_into<3>(value, parityError);
            bit::deposit_into<2>(value, txEnd);
            bit::deposit_into<1>(value, multiprocessorRxBit);
            bit::deposit_into<0>(value, multiprocessorTxBit);
            if constexpr (!peek) {
                txEmptyMask = txEmpty;
                rxFullMask = rxFull;
                overrunErrorMask = overrunError;
                framingErrorMask = framingError;
                parityErrorMask = parityError;
            }
            return value;
        }

        template <bool poke>
        FORCE_INLINE void WriteSSR(uint8 value) {
            if constexpr (poke) {
                txEmpty = bit::test<7>(value);
                rxFull = bit::test<6>(value);
                overrunError = bit::test<5>(value);
                framingError = bit::test<4>(value);
                parityError = bit::test<3>(value);
            } else {
                if (txEmpty && txEmptyMask && !bit::test<7>(value)) {
                    txEnd = false;
                }
                txEmpty &= bit::test<7>(value) || !txEmptyMask;
                rxFull &= bit::test<6>(value) || !rxFullMask;
                overrunError &= bit::test<5>(value) || !overrunErrorMask;
                framingError &= bit::test<4>(value) || !framingErrorMask;
                parityError &= bit::test<3>(value) || !parityErrorMask;
            }
        }

        // 0C5  R    8,16     00        RDR0    Receive data register 0
        // 0CD  R    8,16     00        RDR1    Receive data register 1
        //
        //   bits   r/w  code       description
        //    7-0   R    -          Receive data
        uint8 RDR;

        FORCE_INLINE uint8 ReadRDR() const {
            return RDR;
        }

        template <bool poke>
        FORCE_INLINE void WriteRDR(uint8 value) {
            if constexpr (poke) {
                RDR = value;
            }
        }

        // Receive Shift Register.
        // Receives data bit by bit in LSB to MSB order.
        // Once a full byte is received, it is transferred to RDR.
        uint8 RSR;

        // Current bit to be written to RSR.
        uint8 RSRbit;

        // Transmit Shift Register.
        // Used to transmit data bit by bit in LSB to MSB order.
        // Once a full byte is sent, a new byte transferred from TDR.
        uint8 TSR;

        // Current bit to be written to RSR.
        uint8 TSRbit;

        // Whether to use synchronous (true) or asynchronous (false) communication mode.
        // Derived from SMRn.C/!A
        bool sync;

        // Whether to send 8-bit (false) or 7-bit (true) characters.
        // Derived from SMRn.CHR
        bool char7Bit;

        // Whether to send and check for received parity bit.
        // Derived from SMRn.PE
        bool parityEnable;

        // Whether to use even (false) or odd (true) parity if parity check is enabled.
        // Derived from SMRn.O/!E
        bool parityOdd;

        // Whether to use one (false) or two (true) stop bits.
        // Derived from SMRn.STOP
        bool stopBitTwo;

        // Whether to use multiprocessor mode.
        // Derived from SMRn.MP
        bool multiprocessor;

        // Bit shift applied to system clock.
        // Derived from SMRn.CKS1-0
        uint8 clockShift;

        // Whether to raise the TXI interrupt when the transmit-data-empty signal is raised.
        // Derived from SCR.TIE
        bool txIntrEnable;

        // Whether to raise the RXI or ERI interrupts when the receive-data-full or receive-error signals are raised
        // respectively.
        // Derived from SCR.RIE
        bool rxIntrEnable;

        // Whether the transmitter is enabled.
        // Derived from SCR.TE
        bool txEnable;

        // Whether the receiver is enabled.
        // Derived from SCR.RE
        bool rxEnable;

        // Whether to enable multiprocessor interrupts.
        // Derived from SCR.MPIE
        bool multiprocessorIntrEnable;

        // Whether to raise the TEI interrupt when the transmit-end signal is raised.
        // Derived from SCR.MPIE
        bool txEndIntrEnable;

        // Raw value of the clock enable setting. Has different meanings based on the async/sync flag.
        // Derived from SCR.CKE1-0
        uint8 clockEnable;

        // Whether the transmit data register (TDR) is empty.
        // Derived from SSR.TDRE
        bool txEmpty;

        // Whether the receive data register (RDR) is full.
        // Derived from SSR.RDRF
        bool rxFull;

        // Whether there has been a receive overrun error.
        // Derived from SSR.ORER
        bool overrunError;

        // Whether there has been a receive framing error.
        // Derived from SSR.FER
        bool framingError;

        // Whether there has been a receive parity error.
        // Derived from SSR.PER
        bool parityError;

        // Whether the transmission has ended.
        // Derived from SSR.TEND
        bool txEnd;

        // Value of the multiprocessor bit in the receive data.
        // Derived from SSR.MPB
        bool multiprocessorRxBit;

        // Value of the multiprocessor bit in the transmit data.
        // Derived from SSR.MPBT
        bool multiprocessorTxBit;

        mutable bool txEmptyMask;
        mutable bool rxFullMask;
        mutable bool overrunErrorMask;
        mutable bool framingErrorMask;
        mutable bool parityErrorMask;
    };

    std::array<Channel, 2> channels;
};

} // namespace ymir::sh1
