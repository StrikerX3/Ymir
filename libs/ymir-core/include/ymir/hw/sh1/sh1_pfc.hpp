#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

namespace ymir::sh1 {

struct PinFunctionController {
    PinFunctionController() {
        Reset();
    }

    void Reset() {
        portADir = 0x0000;
        portBDir = 0x0000;

        portAModes = {1, 1, 1, 2, 3, 3, 3, 3, 2, 0, 0, 0, 3, 0, 3, 0};
        portBModes.fill(0);

        cashMode = 1;
        caslMode = 1;
    }

    // 1C4  R/W  8,16,32  0000      PAIOR   Port A I/O register
    //
    //   bits   r/w  code       description
    //   15-0   R/W  PA15-0IOR  Port A Pin 15-0 Input/Output Selection (0=input, 1=output)

    FORCE_INLINE uint16 ReadPAIOR() const {
        return portADir;
    }

    FORCE_INLINE void WritePAIOR(uint16 value) {
        portADir = value;
    }

    FORCE_INLINE void WritePAIORHi(uint8 value) {
        bit::deposit_into<8, 15>(portADir, value);
    }

    FORCE_INLINE void WritePAIORLo(uint8 value) {
        bit::deposit_into<0, 7>(portADir, value);
    }

    // 1C6  R/W  8,16,32  0000      PBIOR   Port B I/O register
    //
    //   bits   r/w  code       description
    //   15-0   R/W  PB15-0IOR  Port B Pin 15-0 Input/Output Selection (0=input, 1=output)

    FORCE_INLINE uint16 ReadPBIOR() const {
        return portBDir;
    }

    FORCE_INLINE void WritePBIOR(uint16 value) {
        portBDir = value;
    }

    FORCE_INLINE void WritePBIORHi(uint8 value) {
        bit::deposit_into<8, 15>(portBDir, value);
    }

    FORCE_INLINE void WritePBIORLo(uint8 value) {
        bit::deposit_into<0, 7>(portBDir, value);
    }

    // 1C8  R/W  8,16,32  3302      PACR1   Port A control register 1
    //
    //   bits   r/w  code       description
    //  15-14   R/W  PA15MD1-0  PA15 Mode
    //                            00 (0) = Input/output (PA15)
    //                            01 (1) = Interrupt request input (nIRQ3)
    //                            10 (2) = Reserved
    //                            11 (3) = DMA transfer request input (nDREQ1)
    //  13-12   R/W  PA14MD1-0  PA14 Mode
    //                            00 (0) = Input/output (PA14)
    //                            01 (1) = Interrupt request input (nIRQ2)
    //                            10 (2) = Reserved
    //                            11 (3) = DMA transfer acknowledge output (DACK1)
    //  11-10   R/W  PA13MD1-0  PA13 Mode
    //                            00 (0) = Input/output (PA13)
    //                            01 (1) = Interrupt request input (nIRQ1)
    //                            10 (2) = ITU timer clock input (TCLKB)
    //                            11 (3) = DMA transfer request input (nDREQ0)
    //    9-8   R/W  PA12MD1-0  PA12 Mode
    //                            00 (0) = Input/output (PA12)
    //                            01 (1) = Interrupt request input (nIRQ0)
    //                            10 (2) = ITU timer clock input (TCLKA)
    //                            11 (3) = DMA transfer acknowledge output (DACK0)
    //    7-6   R/W  PA11MD1-0  PA11 Mode
    //                            00 (0) = Input/output (PA11)
    //                            01 (1) = Upper data bus parity input/output (DPH)
    //                            10 (2) = ITU input capture/output compare (TIOCB1)
    //                            11 (3) = Reserved
    //    5-4   R/W  PA10MD1-0  PA10 Mode
    //                            00 (0) = Input/output (PA10)
    //                            01 (1) = Lower data bus parity input/output (DPL)
    //                            10 (2) = ITU input capture/output compare (TIOCA1)
    //                            11 (3) = Reserved
    //    3-2   R/W  PA9MD1-0   PA9 Mode
    //                            00 (0) = Input/output (PA9)
    //                            01 (1) = Address hold output (nAH)
    //                            10 (2) = A/D conversion trigger input (nADTRG)
    //                            11 (3) = Interrupt request output (nIRQOUT)
    //      1   -    -          Reserved - must be one
    //      0   R/W  PA8MD      PA8 Mode
    //                            0 = Input/output (PA8)
    //                            1 = Bus request input (nBREQ)

    FORCE_INLINE uint16 ReadPACR1() const {
        uint16 value = 0;
        bit::deposit_into<14, 15>(value, portAModes[15]);
        bit::deposit_into<12, 13>(value, portAModes[14]);
        bit::deposit_into<10, 11>(value, portAModes[13]);
        bit::deposit_into<8, 9>(value, portAModes[12]);
        bit::deposit_into<6, 7>(value, portAModes[11]);
        bit::deposit_into<4, 5>(value, portAModes[10]);
        bit::deposit_into<2, 3>(value, portAModes[9]);
        bit::deposit_into<1>(value, 0b1);
        bit::deposit_into<0>(value, portAModes[8]);
        return value;
    }

    FORCE_INLINE void WritePACR1(uint16 value) {
        portAModes[15] = bit::extract<14, 15>(value);
        portAModes[14] = bit::extract<12, 13>(value);
        portAModes[13] = bit::extract<10, 11>(value);
        portAModes[12] = bit::extract<8, 9>(value);
        portAModes[11] = bit::extract<6, 7>(value);
        portAModes[10] = bit::extract<4, 5>(value);
        portAModes[9] = bit::extract<2, 3>(value);
        portAModes[8] = bit::extract<0>(value);
    }

    FORCE_INLINE void WritePACR1Hi(uint8 value) {
        portAModes[15] = bit::extract<6, 7>(value);
        portAModes[14] = bit::extract<4, 5>(value);
        portAModes[13] = bit::extract<2, 3>(value);
        portAModes[12] = bit::extract<0, 1>(value);
    }

    FORCE_INLINE void WritePACR1Lo(uint8 value) {
        portAModes[11] = bit::extract<6, 7>(value);
        portAModes[10] = bit::extract<4, 5>(value);
        portAModes[9] = bit::extract<2, 3>(value);
        portAModes[8] = bit::extract<0, 1>(value);
    }

    // 1CA  R/W  8,16,32  FF95      PACR2   Port A control register 2
    //
    //   bits   r/w  code       description
    //     15   -    -          Reserved - must be one
    //     14   R/W  PA7MD      PA7 Mode
    //                            0 = Input/output (PA7)
    //                            1 = Bus request acknowledge output (nBACK)
    //     13   -    -          Reserved - must be one
    //     12   R/W  PA6MD      PA6 Mode
    //                            0 = Input/output (PA6)
    //                            1 = Read output (nRD)
    //     11   -    -          Reserved - must be one
    //     10   R/W  PA5MD      PA5 Mode
    //                            0 = Input/output (PA5)
    //                            1 = Upper write output (nWRH) or lower byte strobe output (nLBS)
    //      9   -    -          Reserved - must be one
    //      8   R/W  PA4MD      PA4 Mode
    //                            0 = Input/output (PA4)
    //                            1 = Lower write output (nWRL) or write output (nWR)
    //    7-6   R/W  PA3MD1-0   PA3 Mode
    //                            00 (0) = Input/output (PA3)
    //                            01 (1) = Chip select output (nCS7)
    //                            10 (2) = Wait state input (nWAIT)
    //                            11 (3) = Reserved
    //    5-4   R/W  PA2MD1-0   PA2 Mode
    //                            00 (0) = Input/output (PA2)
    //                            01 (1) = Chip select output (nCS6)
    //                            10 (2) = ITU input capture/output compare (TIOCB0)
    //                            11 (3) = Reserved
    //    3-2   R/W  PA1MD1-0   PA1 Mode
    //                            00 (0) = Input/output (PA1)
    //                            01 (1) = Chip select output (nCS5)
    //                            10 (2) = Row address strobe output (nRAS)
    //                            11 (3) = Reserved
    //    1-0   R/W  PA0MD1-0   PA0 Mode
    //                            00 (0) = Input/output (PA0)
    //                            01 (1) = Chip select output (nCS4)
    //                            10 (2) = ITU input capture/output compare (TIOCA0)
    //                            11 (3) = Reserved

    FORCE_INLINE uint16 ReadPACR2() const {
        uint16 value = 0;
        bit::deposit_into<15>(value, 0b1);
        bit::deposit_into<14>(value, portAModes[7]);
        bit::deposit_into<13>(value, 0b1);
        bit::deposit_into<12>(value, portAModes[6]);
        bit::deposit_into<11>(value, 0b1);
        bit::deposit_into<10>(value, portAModes[5]);
        bit::deposit_into<9>(value, 0b1);
        bit::deposit_into<8>(value, portAModes[4]);
        bit::deposit_into<6, 7>(value, portAModes[3]);
        bit::deposit_into<4, 5>(value, portAModes[2]);
        bit::deposit_into<2, 3>(value, portAModes[1]);
        bit::deposit_into<0, 1>(value, portAModes[0]);
        return value;
    }

    FORCE_INLINE void WritePACR2(uint16 value) {
        portAModes[7] = bit::extract<14>(value);
        portAModes[6] = bit::extract<12>(value);
        portAModes[5] = bit::extract<10>(value);
        portAModes[4] = bit::extract<8>(value);
        portAModes[3] = bit::extract<6, 7>(value);
        portAModes[2] = bit::extract<4, 5>(value);
        portAModes[1] = bit::extract<2, 3>(value);
        portAModes[0] = bit::extract<0, 1>(value);
    }

    FORCE_INLINE void WritePACR2Hi(uint8 value) {
        portAModes[7] = bit::extract<6, 7>(value);
        portAModes[6] = bit::extract<4, 5>(value);
        portAModes[5] = bit::extract<2, 3>(value);
        portAModes[4] = bit::extract<0, 1>(value);
    }

    FORCE_INLINE void WritePACR2Lo(uint8 value) {
        portAModes[3] = bit::extract<6, 7>(value);
        portAModes[2] = bit::extract<4, 5>(value);
        portAModes[1] = bit::extract<2, 3>(value);
        portAModes[0] = bit::extract<0, 1>(value);
    }

    // 1CC  R/W  8,16,32  0000      PBCR1   Port B control register 1
    //
    //   bits   r/w  code       description
    //  15-14   R/W  PB15MD1-0  PB15 Mode
    //                            00 (0) = Input/output (PB15)
    //                            01 (1) = Interrupt request input (nIRQ7)
    //                            10 (2) = Reserved
    //                            11 (3) = Timing pattern output (TP15)
    //  13-12   R/W  PB14MD1-0  PB14 Mode
    //                            00 (0) = Input/output (PB14)
    //                            01 (1) = Interrupt request input (nIRQ6)
    //                            10 (2) = Reserved
    //                            11 (3) = Timing pattern output (TP14)
    //  11-10   R/W  PB13MD1-0  PB13 Mode
    //                            00 (0) = Input/output (PB13)
    //                            01 (1) = Interrupt request input (nIRQ5)
    //                            10 (2) = Serial clock input/output (SCK1)
    //                            11 (3) = Timing pattern output (TP13)
    //    9-8   R/W  PB12MD1-0  PB12 Mode
    //                            00 (0) = Input/output (PB12)
    //                            01 (1) = Interrupt request input (nIRQ4)
    //                            10 (2) = Serial clock input/output (SCK0)
    //                            11 (3) = Timing pattern output (TP12)
    //    7-6   R/W  PB11MD1-0  PB11 Mode
    //                            00 (0) = Input/output (PB11)
    //                            01 (1) = Upper data bus parity input/output (DPH)
    //                            10 (2) = Transmit data output (TxD1)
    //                            11 (3) = Timing pattern output (TP11)
    //    5-4   R/W  PB10MD1-0  PB10 Mode
    //                            00 (0) = Input/output (PB10)
    //                            01 (1) = Lower data bus parity input/output (DPL)
    //                            10 (2) = Receive data input (RxD1)
    //                            11 (3) = Timing pattern output (TP10)
    //    3-2   R/W  PB9MD1-0   PB9 Mode
    //                            00 (0) = Input/output (PB9)
    //                            01 (1) = Address hold output (nAH)
    //                            10 (2) = Transmit data output (TxD0)
    //                            11 (3) = Timing pattern output (TP9)
    //    1-0   R/W  PB8MD1-0   PB8 Mode
    //                            00 (0) = Input/output (PB8)
    //                            01 (1) = Reserved
    //                            10 (2) = Receive data input (RxD0)
    //                            11 (3) = Timing pattern output (TP8)

    FORCE_INLINE uint16 ReadPBCR1() const {
        uint16 value = 0;
        bit::deposit_into<14, 15>(value, portBModes[15]);
        bit::deposit_into<12, 13>(value, portBModes[14]);
        bit::deposit_into<10, 11>(value, portBModes[13]);
        bit::deposit_into<8, 9>(value, portBModes[12]);
        bit::deposit_into<6, 7>(value, portBModes[11]);
        bit::deposit_into<4, 5>(value, portBModes[10]);
        bit::deposit_into<2, 3>(value, portBModes[9]);
        bit::deposit_into<0, 1>(value, portBModes[8]);
        return value;
    }

    FORCE_INLINE void WritePBCR1(uint16 value) {
        portBModes[15] = bit::extract<14, 15>(value);
        portBModes[14] = bit::extract<12, 13>(value);
        portBModes[13] = bit::extract<10, 11>(value);
        portBModes[12] = bit::extract<8, 9>(value);
        portBModes[11] = bit::extract<6, 7>(value);
        portBModes[10] = bit::extract<4, 5>(value);
        portBModes[9] = bit::extract<2, 3>(value);
        portBModes[8] = bit::extract<0, 1>(value);
    }

    FORCE_INLINE void WritePBCR1Hi(uint8 value) {
        portBModes[15] = bit::extract<6, 7>(value);
        portBModes[14] = bit::extract<4, 5>(value);
        portBModes[13] = bit::extract<2, 3>(value);
        portBModes[12] = bit::extract<0, 1>(value);
    }

    FORCE_INLINE void WritePBCR1Lo(uint8 value) {
        portBModes[11] = bit::extract<6, 7>(value);
        portBModes[10] = bit::extract<4, 5>(value);
        portBModes[9] = bit::extract<2, 3>(value);
        portBModes[8] = bit::extract<0, 1>(value);
    }

    // 1CE  R/W  8,16,32  0000      PBCR2   Port B control register 2
    //
    //   bits   r/w  code       description
    //  15-14   R/W  PB7MD1-0   PB7 Mode
    //                            00 (0) = Input/output (PB7)
    //                            01 (1) = ITU timer clock input (TCLKD)
    //                            10 (2) = ITU output compare (TOCXB4)
    //                            11 (3) = Timing pattern output (TP7)
    //  13-12   R/W  PB6MD1-0   PB6 Mode
    //                            00 (0) = Input/output (PB6)
    //                            01 (1) = ITU timer clock input (TCLKC)
    //                            10 (2) = ITU output compare (TOCXA4)
    //                            11 (3) = Timing pattern output (TP6)
    //  11-10   R/W  PB5MD1-0   PB5 Mode
    //                            00 (0) = Input/output (PB5)
    //                            01 (1) = Reserved
    //                            10 (2) = ITU input capture/output compare (TIOCB4)
    //                            11 (3) = Timing pattern output (TP5)
    //    9-8   R/W  PB4MD1-0   PB4 Mode
    //                            00 (0) = Input/output (PB4)
    //                            01 (1) = Reserved
    //                            10 (2) = ITU input capture/output compare (TIOCA4)
    //                            11 (3) = Timing pattern output (TP4)
    //    7-6   R/W  PB3MD1-0   PB3 Mode
    //                            00 (0) = Input/output (PB3)
    //                            01 (1) = Reserved
    //                            10 (2) = ITU input capture/output compare (TIOCB3)
    //                            11 (3) = Timing pattern output (TP3)
    //    5-4   R/W  PB2MD1-0   PB2 Mode
    //                            00 (0) = Input/output (PB2)
    //                            01 (1) = Reserved
    //                            10 (2) = ITU input capture/output compare (TIOCA3)
    //                            11 (3) = Timing pattern output (TP2)
    //    3-2   R/W  PB1MD1-0   PB1 Mode
    //                            00 (0) = Input/output (PB1)
    //                            01 (1) = Reserved
    //                            10 (2) = ITU input capture/output compare (TIOCB2)
    //                            11 (3) = Timing pattern output (TP1)
    //    1-0   R/W  PB0MD1-0   PB0 Mode
    //                            00 (0) = Input/output (PB0)
    //                            01 (1) = Reserved
    //                            10 (2) = ITU input capture/output compare (TIOCA2)
    //                            11 (3) = Timing pattern output (TP0)

    FORCE_INLINE uint16 ReadPBCR2() const {
        uint16 value = 0;
        bit::deposit_into<14, 15>(value, portBModes[7]);
        bit::deposit_into<12, 13>(value, portBModes[6]);
        bit::deposit_into<10, 11>(value, portBModes[5]);
        bit::deposit_into<8, 9>(value, portBModes[4]);
        bit::deposit_into<6, 7>(value, portBModes[3]);
        bit::deposit_into<4, 5>(value, portBModes[2]);
        bit::deposit_into<2, 3>(value, portBModes[1]);
        bit::deposit_into<0, 1>(value, portBModes[0]);
        return value;
    }

    FORCE_INLINE void WritePBCR2(uint16 value) {
        portBModes[7] = bit::extract<14, 15>(value);
        portBModes[6] = bit::extract<12, 13>(value);
        portBModes[5] = bit::extract<10, 11>(value);
        portBModes[4] = bit::extract<8, 9>(value);
        portBModes[3] = bit::extract<6, 7>(value);
        portBModes[2] = bit::extract<4, 5>(value);
        portBModes[1] = bit::extract<2, 3>(value);
        portBModes[0] = bit::extract<0, 1>(value);
    }

    FORCE_INLINE void WritePBCR2Hi(uint8 value) {
        portBModes[7] = bit::extract<6, 7>(value);
        portBModes[6] = bit::extract<4, 5>(value);
        portBModes[5] = bit::extract<2, 3>(value);
        portBModes[4] = bit::extract<0, 1>(value);
    }

    FORCE_INLINE void WritePBCR2Lo(uint8 value) {
        portBModes[3] = bit::extract<6, 7>(value);
        portBModes[2] = bit::extract<4, 5>(value);
        portBModes[1] = bit::extract<2, 3>(value);
        portBModes[0] = bit::extract<0, 1>(value);
    }

    // 1EE  R/W  8,16,32  5FFF      CASCR   Column address strobe pin control register
    //
    //   bits   r/w  code       description
    //  15-14   R/W  CASHMD1-0  CASH Mode
    //                            00 (0) = Reserved
    //                            01 (1) = Chip select output (nCS1)
    //                            10 (2) = Column address strobe output (nCASH)
    //                            11 (3) = Reserved
    //  13-12   R/W  CASLMD1-0  CASL Mode
    //                            00 (0) = Reserved
    //                            01 (1) = Chip select output (nCS3)
    //                            10 (2) = Column address strobe output (nCASL)
    //                            11 (3) = Reserved
    //   11-0   -    -          Reserved - must be one

    FORCE_INLINE uint16 ReadCASCR() const {
        uint16 value = 0;
        bit::deposit_into<14, 15>(value, cashMode);
        bit::deposit_into<12, 13>(value, caslMode);
        bit::deposit_into<0, 11>(value, 0b111111111111);
        return value;
    }

    FORCE_INLINE void WriteCASCR(uint16 value) {
        cashMode = bit::extract<14, 15>(value);
        caslMode = bit::extract<12, 13>(value);
    }

    FORCE_INLINE void WriteCASCRHi(uint8 value) {
        cashMode = bit::extract<6, 7>(value);
        caslMode = bit::extract<4, 5>(value);
    }

    // Port A pin directions (one per bit; 0=input, 1=output).
    // Derived from PAIOR
    uint16 portADir;

    // Port B pin directions (one per bit; 0=input, 1=output).
    // Derived from PBIOR
    uint16 portBDir;

    // Modes for each port A pin.
    // Derived from PACR1 and PACR2
    std::array<uint8, 16> portAModes;

    // Modes for each port B pin.
    // Derived from PBCR1 and PBCR2
    std::array<uint8, 16> portBModes;

    // CASH mode.
    // Derived from CASCR.CASHMD1-0
    uint8 cashMode;

    // CASL mode.
    // Derived from CASCR.CASLMD1-0
    uint8 caslMode;
};

} // namespace ymir::sh1
