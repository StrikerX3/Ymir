#pragma once

#include <ymir/core/types.hpp>

namespace ymir::sh1 {

// Standard SH-1 exception vectors
inline constexpr uint8 xvHardResetPC = 0x00;      // 00  00000000  Power-on reset PC value
inline constexpr uint8 xvHardResetSP = 0x01;      // 01  00000004  Power-on reset SP value
inline constexpr uint8 xvSoftResetPC = 0x02;      // 02  00000008  Manual reset PC value
inline constexpr uint8 xvSoftResetSP = 0x03;      // 03  0000000C  Manual reset SP value
inline constexpr uint8 xvGenIllegalInstr = 0x04;  // 04  00000010  General illegal instruction
inline constexpr uint8 xvSlotIllegalInstr = 0x06; // 06  00000018  Slot illegal instruction
inline constexpr uint8 xvCPUAddressError = 0x09;  // 09  00000024  CPU address error
inline constexpr uint8 xvDMAAddressError = 0x0A;  // 0A  00000028  DMA address error
inline constexpr uint8 xvIntrNMI = 0x0B;          // 0B  0000002C  NMI interrupt
inline constexpr uint8 xvIntrUserBreak = 0x0C;    // 0C  00000030  User break interrupt
inline constexpr uint8 xvIRQ0 = 0x40;             // 40  00000100  IRQ0
inline constexpr uint8 xvIRQ1 = 0x41;             // 41  00000104  IRQ1
inline constexpr uint8 xvIRQ2 = 0x42;             // 42  00000108  IRQ2
inline constexpr uint8 xvIRQ3 = 0x43;             // 43  0000010C  IRQ3
inline constexpr uint8 xvIRQ4 = 0x44;             // 44  00000110  IRQ4
inline constexpr uint8 xvIRQ5 = 0x45;             // 45  00000114  IRQ5
inline constexpr uint8 xvIRQ6 = 0x46;             // 46  00000118  IRQ6
inline constexpr uint8 xvIRQ7 = 0x47;             // 47  0000011C  IRQ7
                                                  //
                                                  // vectors 05, 07, 08, 0D through 1F are reserved
                                                  // vectors 20 through 3F are reserved for TRAPA
                                                  // vectors 48 to FF can be used by on-chip modules

} // namespace ymir::sh1
