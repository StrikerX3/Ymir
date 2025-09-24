#include <ymir/hw/scu/scu_dsp.hpp>

#include <ymir/hw/scu/scu_defs.hpp>

#include "scu_devlog.hpp"

namespace ymir::scu {

// -----------------------------------------------------------------------------
// Debugger

template <bool debug>
FORCE_INLINE static void TraceDSPDMA(debug::ISCUTracer *tracer, bool toD0, uint32 addrD0, uint8 addrDSP, uint8 count,
                                     uint8 addrInc, bool hold) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->DSPDMA(toD0, addrD0, addrDSP, count, addrInc, hold);
        }
    }
}

// -----------------------------------------------------------------------------
// Implementation

SCUDSP::SCUDSP(sys::SH2Bus &bus)
    : m_bus(bus) {
    Reset(true);
}

void SCUDSP::Reset(bool hard) {
    if (hard) {
        programRAM.fill(DSPInstr{});
        for (auto &bank : dataRAM) {
            bank.fill(0);
        }
    }

    programExecuting = false;
    programPaused = false;
    programEnded = false;
    programStep = false;

    PC = 0;
    dataAddress = 0;

    nextInstr.u32 = 0;

    sign = false;
    zero = false;
    carry = false;
    overflow = false;

    CT.u32 = 0x00000000;
    incCT = 0x00000000;

    ALU.u64 = 0;
    AC.u64 = 0;
    P.u64 = 0;
    RX = 0;
    RY = 0;

    loopTop = 0;
    loopCount = 0;
    looping = false;

    dmaRun = false;
    dmaToD0 = false;
    dmaHold = false;
    dmaCount = 0;
    dmaSrc = 0;
    dmaDst = 0;
    dmaReadAddr = 0;
    dmaWriteAddr = 0;
    dmaAddrInc = 0;

    m_cyclesSpillover = 0u;
}

template <bool debug>
void SCUDSP::Run(uint64 cycles) {
    // SCU DSP runs at half of the main clock rate
    cycles += m_cyclesSpillover;
    m_cyclesSpillover = cycles & 1u;
    cycles >>= 1u;

    // FIXME: WipEout (USA) needs more accurate timings for the DSP to fix exploding geometry

    for (uint64 cy = 0; cy < cycles; cy++) {
        // Bail out if not executing
        if (!programExecuting && !programStep) {
            RunDMA<debug>(0);
            return;
        }

        // Bail out if paused
        if (programPaused) {
            RunDMA<debug>(0);
            return;
        }

        // Execute next command and fetch next instruction
        const DSPInstr instruction = nextInstr;
        nextInstr = programRAM[PC];

        const bool doDMA = dmaRun;
        if (dmaRun) {
            dmaPC = PC;
            // HACK: This fixes Grandia FMVs... what the heck is this DMA doing on real hardware?!?
            if (RunDMA<debug>(0)) {
                return;
            }
        }

        switch (instruction.instructionInfo.instructionClass) {
        case 0b00: Cmd_Operation<debug>(instruction); break;
        case 0b10: Cmd_LoadImm<debug>(instruction); break;
        case 0b11: Cmd_Special<debug>(instruction); break;
        }

        // TODO: is this correct?
        /*if (doDMA) {
            // Run entire DMA if writing to program RAM, otherwise run a single transfer
            if (!dmaToD0 && dmaDst == 4) {
                RunDMA<debug>(0);
            } else {
                // FIXME/HACK: WipEout (USA) doesn't go past menus if we run less DMA steps here
                // RunDMA<debug>(1);
                // RunDMA<debug>(16);
                RunDMA<debug>(0);
            }
        }*/

        // Clear stepping flag to ensure the DSP only runs one command when stepping
        programStep = false;
    }
}

template void SCUDSP::Run<false>(uint64 cycles);
template void SCUDSP::Run<true>(uint64 cycles);

template <bool debug>
bool SCUDSP::RunDMA(uint64 cycles) {
    // TODO: proper cycle counting

    // Bail out if DMA is not running
    if (!dmaRun) {
        return false;
    }

    const bool toD0 = dmaToD0;
    const BusID bus = GetBusID(dmaAddrD0);
    if (bus == BusID::None) {
        dmaRun = false;
        return false;
    }

    // Run transfer
    // TODO: should iterate through transfers based on cycle count
    const uint32 ctIndex = toD0 ? dmaSrc : dmaDst;
    const bool useDataRAM = ctIndex <= 3;
    const bool useProgramRAM = !toD0 && ctIndex == 4;
    uint8 programRAMIndex = dmaPC;

    do {
        if (toD0) {
            // Data RAM -> D0
            if (m_bus.IsBusWait(dmaAddrD0, sizeof(uint32), true)) {
                devlog::debug<grp::dsp>("SCU DSP DMA: Read from {:08X} stalled by bus wait signal", dmaAddrD0);
                return true;
            }

            const uint32 value = useDataRAM ? dataRAM[ctIndex][CT.array[ctIndex]] : ~0u;
            if (bus == BusID::ABus) {
                // A-Bus -> one 32-bit write
                m_bus.Write<uint32>(dmaAddrD0, value);
                dmaAddrD0 += dmaAddrInc;
            } else if (bus == BusID::BBus) {
                // B-Bus -> two 16-bit writes
                m_bus.Write<uint16>(dmaAddrD0, value >> 16u);
                dmaAddrD0 += dmaAddrInc;
                m_bus.Write<uint16>(dmaAddrD0, value >> 0u);
                dmaAddrD0 += dmaAddrInc;
            } else if (bus == BusID::WRAM) {
                // WRAM -> one 32-bit write
                m_bus.Write<uint32>(dmaAddrD0 & ~3, value);
                dmaAddrD0 += dmaAddrInc;
            }
        } else {
            // D0 -> Data/Program RAM
            if (m_bus.IsBusWait(dmaAddrD0, sizeof(uint32), false)) {
                devlog::debug<grp::dsp>("SCU DSP DMA: Read from {:08X} stalled by bus wait signal", dmaAddrD0);
                return true;
            }

            uint32 value;
            if (bus == BusID::ABus) {
                // A-Bus -> one 32-bit read
                value = m_bus.Read<uint32>(dmaAddrD0);
                dmaAddrD0 += dmaAddrInc;
            } else if (bus == BusID::BBus) {
                // B-Bus -> two 16-bit reads
                value = m_bus.Read<uint16>(dmaAddrD0 | 0) << 16u;
                value |= m_bus.Read<uint16>(dmaAddrD0 | 2) << 0u;
                dmaAddrD0 += 4;
            } else if (bus == BusID::WRAM) {
                // WRAM -> one 32-bit read
                value = m_bus.Read<uint32>(dmaAddrD0);
                dmaAddrD0 += dmaAddrInc;
            }
            if (useDataRAM) {
                dataRAM[ctIndex][CT.array[ctIndex]] = value;
            } else if (useProgramRAM) {
                programRAM[programRAMIndex++].u32 = value;
            }
        }
        dmaAddrD0 &= 0x7FF'FFFF;
        if (useDataRAM) {
            CT.array[ctIndex]++;
            CT.array[ctIndex] &= 0x3F;
        }
    } while (--dmaCount != 0 && --cycles != 0);

    if (dmaCount == 0) {
        // Update RA0/WA0 if not holding address
        if (!dmaHold) {
            if (dmaAddrInc == 0) {
                if (toD0) {
                    dmaWriteAddr += 4;
                } else {
                    dmaReadAddr += 4;
                }
            } else if (toD0) {
                if (bus == BusID::BBus) {
                    dmaAddrD0 -= dmaAddrInc * 2;
                } else {
                    dmaAddrD0 -= dmaAddrInc;
                }
                dmaWriteAddr = (dmaAddrD0 + 4) & ~3;
            } else {
                dmaReadAddr = dmaAddrD0;
            }
        }

        dmaRun = false;

        // Clear program pipeline if writing to Program RAM
        if (useProgramRAM) {
            nextInstr.u32 = 0;
            PC = loopTop;
        }
    }

    return false;
}

void SCUDSP::SaveState(state::SCUDSPState &state) const {
    for (size_t i = 0; i < programRAM.size(); ++i) {
        state.programRAM[i] = programRAM[i].u32;
    }
    state.dataRAM = dataRAM;
    state.programExecuting = programExecuting;
    state.programPaused = programPaused;
    state.programEnded = programEnded;
    state.programStep = programStep;
    state.PC = PC;
    state.nextInstr = nextInstr.u32;
    state.dataAddress = dataAddress;
    state.sign = sign;
    state.zero = zero;
    state.carry = carry;
    state.overflow = overflow;
    state.CT = CT.array;
    state.ALU = ALU.s64;
    state.AC = AC.s64;
    state.P = P.s64;
    state.RX = RX;
    state.RY = RY;
    state.LOP = loopCount;
    state.TOP = loopTop;
    state.looping = looping;
    state.dmaRun = dmaRun;
    state.dmaToD0 = dmaToD0;
    state.dmaHold = dmaHold;
    state.dmaCount = dmaCount;
    state.dmaSrc = dmaSrc;
    state.dmaDst = dmaDst;
    state.dmaReadAddr = dmaReadAddr;
    state.dmaWriteAddr = dmaWriteAddr;
    state.dmaAddrInc = dmaAddrInc;
    state.dmaAddrD0 = dmaAddrD0;
    state.cyclesSpillover = m_cyclesSpillover;
}

bool SCUDSP::ValidateState(const state::SCUDSPState &state) const {
    if (state.dmaAddrInc != 0 && (!bit::is_power_of_two(state.dmaAddrInc) || state.dmaAddrInc == 1)) {
        return false;
    }
    return true;
}

void SCUDSP::LoadState(const state::SCUDSPState &state) {
    for (size_t i = 0; i < programRAM.size(); ++i) {
        programRAM[i].u32 = state.programRAM[i];
    }
    dataRAM = state.dataRAM;
    programExecuting = state.programExecuting;
    programPaused = state.programPaused;
    programEnded = state.programEnded;
    programStep = state.programStep;
    PC = state.PC;
    nextInstr.u32 = state.nextInstr;
    dataAddress = state.dataAddress;
    sign = state.sign;
    zero = state.zero;
    carry = state.carry;
    overflow = state.overflow;
    CT.array = state.CT;
    ALU.s64 = state.ALU;
    AC.s64 = state.AC;
    P.s64 = state.P;
    RX = state.RX;
    RY = state.RY;
    loopCount = state.LOP & 0xFFF;
    loopTop = state.TOP;
    looping = state.looping;
    dmaRun = state.dmaRun;
    dmaToD0 = state.dmaToD0;
    dmaHold = state.dmaHold;
    dmaCount = state.dmaCount;
    dmaSrc = state.dmaSrc & 3;
    dmaDst = state.dmaDst & 7;
    dmaReadAddr = state.dmaReadAddr & 0x7FFFFFC;
    dmaWriteAddr = state.dmaWriteAddr & 0x7FFFFFC;
    dmaAddrInc = state.dmaAddrInc;
    dmaAddrD0 = state.dmaAddrD0 & 0x7FFFFFF;
    m_cyclesSpillover = state.cyclesSpillover;
}

FORCE_INLINE void SCUDSP::IncrementPC() {
    if (looping) {
        if (loopCount == 0) {
            looping = false;
            ++PC;
        }
        loopCount = (loopCount - 1) & 0xFFF;
    } else {
        ++PC;
    }
}

template <bool debug>
FORCE_INLINE void SCUDSP::Cmd_Operation(DSPInstr instr) {
    IncrementPC();

    // D1-Bus MOVs to MC0-3 using the a bank that was read by any of the three busses prevents writes and CT updates.
    // MOV to M0-3 is unaffected because it writes directly to CT as opposed to M0-3 reads which hit Data RAM.
    //
    // For reference:
    // src 0..3 = M0..M3
    // src 4..7 = MC0..MC3
    // dst 0..3 = MC0..MC3
    uint8 dataRAMReads = 0x0;
    auto markDataRAMRead = [&](uint8 src) {
        if (src < 0x8) {
            dataRAMReads |= 1u << (src & 0x3);
        }
    };

    // ALU
    ALU = AC;
    switch (instr.aluInfo.aluOp) {
    case 0b0000: break;            // NOP
    case 0b0001: ALU_AND(); break; // AND
    case 0b0010: ALU_OR(); break;  // OR
    case 0b0011: ALU_XOR(); break; // XOR
    case 0b0100: ALU_ADD(); break; // ADD
    case 0b0101: ALU_SUB(); break; // SUB
    case 0b0110: ALU_AD2(); break; // AD2
    case 0b1000: ALU_SR(); break;  // SR
    case 0b1001: ALU_RR(); break;  // RR
    case 0b1010: ALU_SL(); break;  // SL
    case 0b1011: ALU_RL(); break;  // RL
    case 0b1111: ALU_RL8(); break; // RL8
    }

    // X-Bus
    //
    // X-Bus writes simultaneously to P and X in some cases:
    // bits
    // 25-23  executed operations
    //  000
    //  001
    //  010   MOV MUL,P
    //  011   MOV [s],P
    //  100               MOV [s],X
    //  101               MOV [s],X
    //  110   MOV MUL,P   MOV [s],X
    //  111   MOV [s],P   MOV [s],X
    if ((instr.aluInfo.xBusOp & 0b11) == 0b10) {
        // MOV MUL,P
        P.s64 = static_cast<sint64>(RX) * static_cast<sint64>(RY);
    }
    if (instr.aluInfo.xBusOp >= 0b011) {
        const sint32 value = ReadSource<debug>(instr.aluInfo.xBusSource);
        markDataRAMRead(instr.aluInfo.xBusSource);
        if ((instr.aluInfo.xBusOp & 0b11) == 0b11) {
            // MOV [s],P
            P.s64 = static_cast<sint64>(value);
        }
        if (bit::test<2>(instr.aluInfo.xBusOp)) {
            // MOV [s],X
            RX = value;
        }
    }

    // Y-Bus
    //
    // Y-Bus writes simultaneously to A and Y in some cases:
    // bits
    // 19-17  executed operations
    // 000
    // 001    CLR A
    // 010    MOV ALU,A
    // 011    MOV [s],A
    // 100                MOV [s],Y
    // 101    CLR A       MOV [s],Y
    // 110    MOV ALU,A   MOV [s],Y
    // 111    MOV [s],A   MOV [s],Y
    if ((instr.aluInfo.yBusOp & 0b11) == 0b01) {
        // CLR A
        AC.s64 = 0;
    } else if ((instr.aluInfo.yBusOp & 0b11) == 0b10) {
        // MOV ALU,A
        AC.s64 = ALU.s64;
    }
    if (instr.aluInfo.yBusOp >= 0b11) {
        const sint32 value = ReadSource<debug>(instr.aluInfo.yBusSource);
        markDataRAMRead(instr.aluInfo.yBusSource);
        if ((instr.aluInfo.yBusOp & 0b11) == 0b11) {
            // MOV [s],A
            AC.s64 = static_cast<sint64>(value);
        }
        if (bit::test<2>(instr.aluInfo.yBusOp)) {
            // MOV [s],Y
            RY = value;
        }
    }

    // D1-Bus
    switch (instr.aluInfo.d1BusOp) {
    case 0b01: // MOV SImm, [d]
    {
        const sint32 imm = instr.aluInfo.d1BusImm;
        const uint8 dst = instr.aluInfo.d1BusDest;
        if (dst < 0x4 && (dataRAMReads & (1u << dst))) {
            CT.u32 &= ~(1 << (dst * 8));
        } else if (dst == 0x4 && bit::test<2>(instr.aluInfo.xBusOp)) {
            // Prevent writes to X if X-Bus has written to it
        } else if (dst == 0x5 && bit::test<1>(instr.aluInfo.xBusOp)) {
            // Prevent writes to P if X-Bus has written to it
        } else {
            WriteD1Bus<debug>(dst, imm);
        }
        break;
    }
    case 0b11: // MOV [s], [d]
    {
        const uint8 src = instr.aluInfo.d1BusImm & 0b1111;
        const uint8 dst = instr.aluInfo.d1BusDest;
        markDataRAMRead(src);

        if (dst >= 0x4 || (dataRAMReads & (1u << dst)) == 0) {
            // Allow writes to Data RAM only if src wasn't read

            if (dst == 0x4 && bit::test<2>(instr.aluInfo.xBusOp)) {
                // Prevent writes to X if X-Bus has written to it
            } else if (dst == 0x5 && bit::test<1>(instr.aluInfo.xBusOp)) {
                // Prevent writes to P if X-Bus has written to it
                void(ReadSource<debug>(src));
            } else {
                const uint32 value = ReadSource<debug>(src);
                WriteD1Bus<debug>(dst, value);
            }
        } else if (dst < 0x4 && src >= 0x4 && src < 0x8 && dst != (src & 3)) {
            // Reads from MC0-3 should still increment CT
            incCT |= 1u << ((src & 3) * 8);
        }
        break;
    }
    }

    // Update CT0-3
    CT.u32 = (CT.u32 + incCT) & 0x3F3F3F3F;
    incCT = 0x00000000;
}

template <bool debug>
FORCE_INLINE void SCUDSP::Cmd_LoadImm(DSPInstr instr) {
    const uint8 dst = instr.loadInfo.loadControl.storageLocation;

    const bool writeToPC = dst == 0b1100;
    if (looping) {
        if (loopCount == 0) {
            looping = false;
            if (!writeToPC) {
                ++PC;
            }
        }
        loopCount = (loopCount - 1) & 0xFFF;
    } else if (!writeToPC) {
        ++PC;
    }

    sint32 imm;
    if (instr.loadInfo.loadControl.conditionalLoad) {
        // Conditional transfer
        // MVI SImm,[d],<cond>
        imm = instr.loadInfo.conditional.imm;

        const uint8 cond = instr.loadInfo.conditional.condition;
        if (!CondCheck(cond)) {
            return;
        }
    } else {
        // Unconditional transfer
        // MVI SImm,[d]
        imm = instr.loadInfo.unconditional.imm;
    }

    WriteImm<debug>(dst, imm);
}

template <bool debug>
FORCE_INLINE void SCUDSP::Cmd_Special(DSPInstr instr) {
    const uint32 cmdSubcategory = instr.specialInfo.specialControl.specialClass;
    switch (cmdSubcategory) {
    case 0b00: Cmd_Special_DMA<debug>(instr); break;
    case 0b01: Cmd_Special_Jump(instr); break;
    case 0b10: Cmd_Special_Loop(instr); break;
    case 0b11: Cmd_Special_End(instr); break;
    }
}

template <bool debug>
FORCE_INLINE void SCUDSP::Cmd_Special_DMA(DSPInstr command) {
    IncrementPC();

    // Finish previous DMA transfer
    if (dmaRun) {
        RunDMA<debug>(0);
    }

    dmaRun = true;
    dmaToD0 = command.specialInfo.dmaInfo.direction;
    dmaHold = command.specialInfo.dmaInfo.hold;

    // Get DMA transfer length
    if (command.specialInfo.dmaInfo.sizeSource) {
        const uint8 ctIndex = command.specialInfo.dmaInfo.imm & 0b11;
        const bool inc = bit::test<2>(command.specialInfo.dmaInfo.imm);
        const uint32 ctAddr = CT.array[ctIndex];
        dmaCount = dataRAM[ctIndex][ctAddr];
        if (inc) {
            CT.array[ctIndex]++;
            CT.array[ctIndex] &= 0x3F;
        }
    } else {
        dmaCount = command.specialInfo.dmaInfo.imm;
    }

    // Get [RAM] source/destination register (CT) index and address increment
    const uint8 addrInc = command.specialInfo.dmaInfo.stride;
    if (dmaToD0) {
        // DMA [RAM],D0,SImm
        // DMA [RAM],D0,[s]
        // DMAH [RAM],D0,SImm
        // DMAH [RAM],D0,[s]
        dmaSrc = command.specialInfo.dmaInfo.address;
        dmaAddrInc = (1u << addrInc) & ~1u;
        dmaAddrD0 = dmaWriteAddr;
        devlog::trace<grp::dsp>("Running DSP DMA transfer: DSP -> {:08X} (+{:X}), {} longwords", dmaAddrD0, dmaAddrInc,
                                dmaCount);
        TraceDSPDMA<debug>(m_tracer, dmaToD0, dmaAddrD0, dmaSrc, dmaCount, dmaAddrInc, dmaHold);
    } else {
        // DMA D0,[RAM],SImm
        // DMA D0,[RAM],[s]
        // DMAH D0,[RAM],SImm
        // DMAH D0,[RAM],[s]
        dmaDst = command.specialInfo.dmaInfo.address;
        dmaAddrInc = (1u << (addrInc & 0x2u)) & ~1u;
        dmaAddrD0 = dmaReadAddr;
        devlog::trace<grp::dsp>("Running DSP DMA transfer: {:08X} -> DSP (+{:X}), {} longwords", dmaAddrD0, dmaAddrInc,
                                dmaCount);
        TraceDSPDMA<debug>(m_tracer, dmaToD0, dmaAddrD0, dmaDst, dmaCount, dmaAddrInc, dmaHold);
    }

    devlog::trace<grp::dsp>("DSP DMA command: {:04X} @ {:02X}", command.u32, PC);
}

FORCE_INLINE void SCUDSP::Cmd_Special_Jump(DSPInstr command) {
    // JMP <cond>,SImm
    // JMP SImm
    IncrementPC();

    if (command.specialInfo.jumpInfo.conditional) {
        const uint32 cond = command.specialInfo.jumpInfo.condition;
        if (cond != 0 && !CondCheck(cond)) {
            return;
        }
    }

    PC = command.specialInfo.jumpInfo.target;
}

FORCE_INLINE void SCUDSP::Cmd_Special_Loop(DSPInstr command) {
    if (command.specialInfo.loopInfo.repeat) {
        // LPS
        looping = true;
        IncrementPC();
    } else {
        // BTM
        if (loopCount != 0) {
            PC = loopTop;
        } else {
            IncrementPC();
        }
        loopCount = (loopCount - 1) & 0xFFF;
    }
}

FORCE_INLINE void SCUDSP::Cmd_Special_End(DSPInstr command) {
    // END
    // ENDI
    IncrementPC();

    programExecuting = false;
    if (command.specialInfo.endInfo.interrupt && !programEnded) {
        programEnded = true;
        m_cbTriggerDSPEnd();
    }
}

} // namespace ymir::scu
