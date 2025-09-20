#include <ymir/hw/sh1/sh1.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_assert.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/unreachable.hpp>

#include <algorithm>
#include <cassert>
#include <ostream>
#include <string>
#include <string_view>

namespace ymir::sh1 {

// -----------------------------------------------------------------------------
// Dev log groups

namespace grp {

    // Hierarchy:
    //
    // base
    //   exec
    //   intr
    //   mem
    //   reg
    //   dma

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "SH1";
    };

    struct exec : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string_view name = "SH1-Exec";
    };

    struct intr : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string_view name = "SH1-Interrupt";
    };

    struct mem : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string_view name = "SH1-Mem";
    };

    struct reg : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string_view name = "SH1-Reg";
    };

    struct dma : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string_view name = "SH1-DMA";
    };

} // namespace grp

// -----------------------------------------------------------------------------
// Implementation
//
// Notes:
// - Assumes MD2-0 = 0b010, which is how the SH-1 is wired in the Saturn's CD Block. Implies that area 0 of the address
//   space contains a 64 KiB on-chip ROM.
// - Assumes pins are configured according to the Saturn's CD Block ROM setup.
// - Bus implementation uses 28 bits for the sake of performance. Ideally, the Bus would represent the external memory
//   space alone with just 22 address bits and DRAM would be mapped separately, but this is easier and faster.
// - MemRead/MemWrite do not mask bits A23 and A22 on external space accesses for performance and consistency.
// - Only implements the subset of the on-chip modules' features used by the Saturn's CD Block ROM.

SH1::SH1(core::Scheduler &scheduler, sys::SH1Bus &bus)
    : m_scheduler(scheduler)
    , m_bus(bus) {

    Reset(true);
}

void SH1::Reset(bool hard, bool watchdogInitiated) {
    // Initial values:
    // - R0-R14 = undefined
    // - R15 = ReadLong(0x00000004)  [NOTE: ignores VBR]

    // - SR = bits I3-I0 set, reserved bits clear, the rest is undefined
    // - GBR = undefined
    // - VBR = 0x00000000

    // - MACH, MACL = undefined
    // - PR = undefined
    // - PC = ReadLong(0x00000000)  [NOTE: ignores VBR]

    // On-chip peripherals:
    // - BSC, PFC and I/O are not reset on manual/soft reset
    // - all other modules reset always

    R.fill(0);
    PR = 0;

    MAC.u64 = 0;

    SR.u32 = 0;
    SR.ILevel = 0xF;
    GBR = 0;
    VBR = 0x00000000;

    PC = MemReadLong(0x00000000);
    R[15] = MemReadLong(0x00000004);

    m_ram.fill(0);

    if (hard) {
        BSC.Reset();
        PFC.Reset();
    }
    DMAC.Reset();
    ITU.Reset();
    TPC.Reset();
    WDT.Reset(false);
    SCI.Reset();
    AD.Reset();

    SBYCR.u8 = 0x1F;
    m_sleep = false;

    m_nDREQ.fill(true);
    m_PB2 = true;
    m_TIOCB3 = true;

    INTC.Reset();
    m_intrPending = false;

    m_delaySlotTarget = 0;
    m_delaySlot = false;
}

void SH1::LoadROM(std::span<uint8, 64 * 1024> rom) {
    std::copy(rom.begin(), rom.end(), m_rom.begin());
}

uint64 SH1::Advance(uint64 cycles, uint64 spilloverCycles) {
    m_cyclesExecuted = spilloverCycles;

    // TODO: debugging features
    /*if constexpr (debug) {
        if (m_debugSuspend) {
            m_cyclesExecuted = cycles;
            return m_cyclesExecuted;
        }
    }*/

    // Skip interpreting instructions if CPU is in sleep or standby mode.
    // Wake up on interrupts.
    if (m_sleep) [[unlikely]] {
        if (m_intrPending) {
            m_sleep = false;
            PC += 2;
        } else {
            return cycles;
        }
    }

    while (m_cyclesExecuted < cycles) {
        // [[maybe_unused]] const uint32 prevPC = PC; // debug aid

        // TODO: choose between interpreter (cached or uncached) and JIT recompiler
        m_cyclesExecuted += InterpretNext();
        // TODO: lazily update these
        // TODO: AdvanceWDT<false>();
        AdvanceITU();
        AdvanceSCI();
        // TODO: AdvanceDMA<false>();

        // Check for breakpoints and watchpoints in debug tracing mode
        // TODO: debugging features
        /*if constexpr (debug) {
            if (m_debugBreakMgr) {
                if (CheckBreakpoint()) {
                    break;
                }

                const uint16 instr = MemRead<uint16, true, true, enableCache>(PC);
                const auto &mem = DecodeTable::s_instance.mem[instr];
                if (CheckWatchpoints(mem)) {
                    break;
                }
            }
        }*/
    }
    return m_cyclesExecuted;
}

FLATTEN uint64 SH1::Step() {
    m_cyclesExecuted = 0; // so that on-chip modules are synced to the scheduler
    // TODO: AdvanceWDT<false>();
    AdvanceITU();
    AdvanceSCI();
    // TODO: AdvanceDMA<false>();
    const uint64 cycles = InterpretNext();

    return cycles;
}

bool SH1::GetNMI() const {
    return INTC.ICR.NMIL;
}

void SH1::SetNMI() {
    // HACK: should be edge-detected
    INTC.ICR.NMIL = 1;
    INTC.NMI = true;
    RaiseInterrupt(InterruptSource::NMI);
}

void SH1::SetnDREQ0(bool level) {
    m_nDREQ[0] = level;
    if (!level) {
        RunDMAC(0);
    }
}

void SH1::SetnDREQ1(bool level) {
    m_nDREQ[1] = level;
    if (!level) {
        RunDMAC(1);
    }
}

void SH1::AssertIRQ6() {
    SetnIRQ(6, false);
}

void SH1::AssertIRQ7(bool asserted) {
    SetnIRQ(7, !asserted);
}

void SH1::SetTIOCB3(bool level) {
    if (m_TIOCB3 == level) {
        // TIOCB3 is edge-triggered
        return;
    }

    m_TIOCB3 = level;

    auto &timer = ITU.timers[3];

    bool trigger;
    using GRMode = IntegratedTimerPulseUnit::Timer::GRMode;
    switch (timer.GRBMode) {
    case GRMode::InputRising: trigger = level; break;
    case GRMode::InputFalling: trigger = !level; break;
    case GRMode::InputBoth6: [[fallthrough]];
    case GRMode::InputBoth7: trigger = true; break;
    default: trigger = false; break; // the rest are output modes
    }

    if (trigger) {
        timer.IMFB = true;
        if (timer.bufferModeB) {
            timer.BRB = timer.GRB;
        }
        timer.GRB = timer.counter;
        if (timer.clearMode == IntegratedTimerPulseUnit::Timer::ClearMode::GRB) {
            timer.counter = 0;
        }
        if (timer.IMFBIntrEnable) {
            RaiseInterrupt(InterruptSource::ITU3_IMIB3);
        }
    }
}

FORCE_INLINE uint64 SH1::GetCurrentCycleCount() const {
    return m_scheduler.CurrentCount() + m_cyclesExecuted;
}

// -----------------------------------------------------------------------------
// Memory accessors

template <mem_primitive T, bool instrFetch, bool peek>
T SH1::MemRead(uint32 address) {
    static constexpr uint32 kAddressMask = ~(static_cast<uint32>(sizeof(T)) - 1u);

    const uint32 partition = (address >> 24u) & 0xF;
    if (address & ~kAddressMask) {
        if constexpr (!peek) {
            devlog::trace<grp::mem>("WARNING: misaligned %zu-bit read from {:08X}", sizeof(T) * 8, address);
            // TODO: raise CPU address error due to misaligned access
            // - might have to store data in a class member instead of returning
        }
        address &= kAddressMask;
    }

    switch (partition) {
    case 0x0: [[fallthrough]];
    case 0x8: // on-chip ROM
        return util::ReadBE<T>(&m_rom[address & 0xFFFF]);
    case 0x5: // on-chip modules
        return OnChipRegRead<T, peek>(address & 0x1FF);
    case 0xF: // on-chip RAM
        return util::ReadBE<T>(&m_ram[address & 0xFFF]);
    default: // external memory
        if constexpr (peek) {
            return m_bus.Peek<T>(address & 0xFFFFFFF);
        } else {
            return m_bus.Read<T>(address & 0xFFFFFFF);
        }
    }

    util::unreachable();
}

template <mem_primitive T, bool poke>
void SH1::MemWrite(uint32 address, T value) {
    static constexpr uint32 kAddressMask = ~(static_cast<uint32>(sizeof(T)) - 1u);

    const uint32 partition = (address >> 24u) & 0xF;
    if (address & ~kAddressMask) {
        if constexpr (!poke) {
            devlog::trace<grp::mem>("WARNING: misaligned {}-bit write to {:08X} = {:X}", sizeof(T) * 8, address, value);
            // TODO: address error (misaligned access)
        }
        address &= kAddressMask;
    }

    switch (partition) {
    case 0x0: [[fallthrough]];
    case 0x8: // on-chip ROM
        util::WriteBE<T>(&m_rom[address & 0xFFFF], value);
        break;
    case 0x5: // on-chip modules
        OnChipRegWrite<T, poke>(address & 0x1FF, value);
        break;
    case 0xF: // on-chip RAM
        util::WriteBE<T>(&m_ram[address & 0xFFF], value);
        break;
    default: // external memory
        if constexpr (poke) {
            m_bus.Poke<T>(address & 0xFFFFFFF, value);
        } else {
            m_bus.Write<T>(address & 0xFFFFFFF, value);
        }
        break;
    }
}

FLATTEN FORCE_INLINE uint16 SH1::FetchInstruction(uint32 address) {
    return MemRead<uint16, true, false>(address);
}

FLATTEN FORCE_INLINE uint8 SH1::MemReadByte(uint32 address) {
    return MemRead<uint8, false, false>(address);
}

FLATTEN FORCE_INLINE uint16 SH1::MemReadWord(uint32 address) {
    return MemRead<uint16, false, false>(address);
}

FLATTEN FORCE_INLINE uint32 SH1::MemReadLong(uint32 address) {
    return MemRead<uint32, false, false>(address);
}

FLATTEN FORCE_INLINE void SH1::MemWriteByte(uint32 address, uint8 value) {
    MemWrite<uint8, false>(address, value);
}

FLATTEN FORCE_INLINE void SH1::MemWriteWord(uint32 address, uint16 value) {
    MemWrite<uint16, false>(address, value);
}

FLATTEN FORCE_INLINE void SH1::MemWriteLong(uint32 address, uint32 value) {
    MemWrite<uint32, false>(address, value);
}

FORCE_INLINE uint64 SH1::AccessCycles(uint32 address) {
    // const uint32 partition = (address >> 29u) & 0b111;
    // TODO: switch (partition) { ... }
    return 1;

    util::unreachable();
}

// -----------------------------------------------------------------------------
// On-chip modules

template <mem_primitive T, bool peek>
/*FLATTEN_EX FORCE_INLINE_EX*/ T SH1::OnChipRegRead(uint32 address) {
    if constexpr (std::is_same_v<T, uint32>) {
        return OnChipRegReadLong<peek>(address);
    } else if constexpr (std::is_same_v<T, uint16>) {
        return OnChipRegReadWord<peek>(address);
    } else if constexpr (std::is_same_v<T, uint8>) {
        return OnChipRegReadByte<peek>(address);
    }
}

template <bool peek>
FORCE_INLINE uint8 SH1::OnChipRegReadByte(uint32 address) {
    switch (address) {
    case 0x0C0: return SCI.channels[0].ReadSMR();
    case 0x0C1: return SCI.channels[0].ReadBRR();
    case 0x0C2: return SCI.channels[0].ReadSCR();
    case 0x0C3: return SCI.channels[0].ReadTDR();
    case 0x0C4: return SCI.channels[0].ReadSSR<peek>();
    case 0x0C5: return SCI.channels[0].ReadRDR();
    case 0x0C8: return SCI.channels[1].ReadSMR();
    case 0x0C9: return SCI.channels[1].ReadBRR();
    case 0x0CA: return SCI.channels[1].ReadSCR();
    case 0x0CB: return SCI.channels[1].ReadTDR();
    case 0x0CC: return SCI.channels[1].ReadSSR<peek>();
    case 0x0CD: return SCI.channels[1].ReadRDR();

    case 0x0E0: return AD.ReadADDRH<peek>(0);
    case 0x0E1: return AD.ReadADDRL<peek>(0);
    case 0x0E2: return AD.ReadADDRH<peek>(1);
    case 0x0E3: return AD.ReadADDRL<peek>(1);
    case 0x0E4: return AD.ReadADDRH<peek>(2);
    case 0x0E5: return AD.ReadADDRL<peek>(2);
    case 0x0E6: return AD.ReadADDRH<peek>(3);
    case 0x0E7: return AD.ReadADDRL<peek>(3);
    case 0x0E8: return AD.ReadADCSR<peek>();
    case 0x0E9: return AD.ReadADCR();

    case 0x100: return ITU.ReadTSTR();
    case 0x101: return ITU.ReadTSNC();
    case 0x102: return ITU.ReadTMDR();
    case 0x103: return ITU.ReadTFCR();
    case 0x104: return ITU.timers[0].ReadTCR();
    case 0x105: return ITU.timers[0].ReadTIOR();
    case 0x106: return ITU.timers[0].ReadTIER();
    case 0x107: return ITU.timers[0].ReadTSR();
    case 0x108: return ITU.timers[0].ReadTCNT() >> 8u;
    case 0x109: return ITU.timers[0].ReadTCNT();
    case 0x10A: return ITU.timers[0].ReadGRA() >> 8u;
    case 0x10B: return ITU.timers[0].ReadGRA();
    case 0x10C: return ITU.timers[0].ReadGRB() >> 8u;
    case 0x10D: return ITU.timers[0].ReadGRB();
    case 0x10E: return ITU.timers[1].ReadTCR();
    case 0x10F: return ITU.timers[1].ReadTIOR();
    case 0x110: return ITU.timers[1].ReadTIER();
    case 0x111: return ITU.timers[1].ReadTSR();
    case 0x112: return ITU.timers[1].ReadTCNT() >> 8u;
    case 0x113: return ITU.timers[1].ReadTCNT();
    case 0x114: return ITU.timers[1].ReadGRA() >> 8u;
    case 0x115: return ITU.timers[1].ReadGRA();
    case 0x116: return ITU.timers[1].ReadGRB() >> 8u;
    case 0x117: return ITU.timers[1].ReadGRB();
    case 0x118: return ITU.timers[2].ReadTCR();
    case 0x119: return ITU.timers[2].ReadTIOR();
    case 0x11A: return ITU.timers[2].ReadTIER();
    case 0x11B: return ITU.timers[2].ReadTSR();
    case 0x11C: return ITU.timers[2].ReadTCNT() >> 8u;
    case 0x11D: return ITU.timers[2].ReadTCNT();
    case 0x11E: return ITU.timers[2].ReadGRA() >> 8u;
    case 0x11F: return ITU.timers[2].ReadGRA();
    case 0x120: return ITU.timers[2].ReadGRB() >> 8u;
    case 0x121: return ITU.timers[2].ReadGRB();
    case 0x122: return ITU.timers[3].ReadTCR();
    case 0x123: return ITU.timers[3].ReadTIOR();
    case 0x124: return ITU.timers[3].ReadTIER();
    case 0x125: return ITU.timers[3].ReadTSR();
    case 0x126: return ITU.timers[3].ReadTCNT() >> 8u;
    case 0x127: return ITU.timers[3].ReadTCNT();
    case 0x128: return ITU.timers[3].ReadGRA() >> 8u;
    case 0x129: return ITU.timers[3].ReadGRA();
    case 0x12A: return ITU.timers[3].ReadGRB() >> 8u;
    case 0x12B: return ITU.timers[3].ReadGRB();
    case 0x12C: return ITU.timers[3].ReadBRA() >> 8u;
    case 0x12D: return ITU.timers[3].ReadBRA();
    case 0x12E: return ITU.timers[3].ReadBRB() >> 8u;
    case 0x12F: return ITU.timers[3].ReadBRB();
    case 0x131: return ITU.ReadTOCR();
    case 0x132: return ITU.timers[4].ReadTCR();
    case 0x133: return ITU.timers[4].ReadTIOR();
    case 0x134: return ITU.timers[4].ReadTIER();
    case 0x135: return ITU.timers[4].ReadTSR();
    case 0x136: return ITU.timers[4].ReadTCNT() >> 8u;
    case 0x137: return ITU.timers[4].ReadTCNT();
    case 0x138: return ITU.timers[4].ReadGRA() >> 8u;
    case 0x139: return ITU.timers[4].ReadGRA();
    case 0x13A: return ITU.timers[4].ReadGRB() >> 8u;
    case 0x13B: return ITU.timers[4].ReadGRB();
    case 0x13C: return ITU.timers[4].ReadBRA() >> 8u;
    case 0x13D: return ITU.timers[4].ReadBRA();
    case 0x13E: return ITU.timers[4].ReadBRB() >> 8u;
    case 0x13F: return ITU.timers[4].ReadBRB();

    case 0x140: // DMAC SAR0 (bits 24-31)
    case 0x150: // DMAC SAR1 (bits 24-31)
    case 0x160: // DMAC SAR2 (bits 24-31)
    case 0x170: // DMAC SAR3 (bits 24-31)
        return DMAC.channels[(address >> 4) & 3].srcAddress >> 24u;
    case 0x141: // DMAC SAR0 (bits 16-23)
    case 0x151: // DMAC SAR1 (bits 16-23)
    case 0x161: // DMAC SAR2 (bits 16-23)
    case 0x171: // DMAC SAR3 (bits 16-23)
        return DMAC.channels[(address >> 4) & 3].srcAddress >> 16u;
    case 0x142: // DMAC SAR0 (bits 8-15)
    case 0x152: // DMAC SAR1 (bits 8-15)
    case 0x162: // DMAC SAR2 (bits 8-15)
    case 0x172: // DMAC SAR3 (bits 8-15)
        return DMAC.channels[(address >> 4) & 3].srcAddress >> 8u;
    case 0x143: // DMAC SAR0 (bits 0-7)
    case 0x153: // DMAC SAR1 (bits 0-7)
    case 0x163: // DMAC SAR2 (bits 0-7)
    case 0x173: // DMAC SAR3 (bits 0-7)
        return DMAC.channels[(address >> 4) & 3].srcAddress;
    case 0x144: // DMAC DAR0 (bits 24-31)
    case 0x154: // DMAC DAR1 (bits 24-31)
    case 0x164: // DMAC DAR2 (bits 24-31)
    case 0x174: // DMAC DAR3 (bits 24-31)
        return DMAC.channels[(address >> 4) & 3].dstAddress >> 24u;
    case 0x145: // DMAC DAR0 (bits 16-23)
    case 0x155: // DMAC DAR1 (bits 16-23)
    case 0x165: // DMAC DAR2 (bits 16-23)
    case 0x175: // DMAC DAR3 (bits 16-23)
        return DMAC.channels[(address >> 4) & 3].dstAddress >> 16u;
    case 0x146: // DMAC DAR0 (bits 8-15)
    case 0x156: // DMAC DAR1 (bits 8-15)
    case 0x166: // DMAC DAR2 (bits 8-15)
    case 0x176: // DMAC DAR3 (bits 8-15)
        return DMAC.channels[(address >> 4) & 3].dstAddress >> 8u;
    case 0x147: // DMAC DAR0 (bits 0-7)
    case 0x157: // DMAC DAR1 (bits 0-7)
    case 0x167: // DMAC DAR2 (bits 0-7)
    case 0x177: // DMAC DAR3 (bits 0-7)
        return DMAC.channels[(address >> 4) & 3].dstAddress;
    case 0x14A: // DMAC TCR0 (bits 8-15)
    case 0x15A: // DMAC TCR1 (bits 8-15)
    case 0x16A: // DMAC TCR2 (bits 8-15)
    case 0x17A: // DMAC TCR3 (bits 8-15)
        return DMAC.channels[(address >> 4) & 3].xferCount >> 8u;
    case 0x14B: // DMAC TCR0 (bits 0-7)
    case 0x15B: // DMAC TCR1 (bits 0-7)
    case 0x16B: // DMAC TCR2 (bits 0-7)
    case 0x17B: // DMAC TCR3 (bits 0-7)
        return DMAC.channels[(address >> 4) & 3].xferCount;
    case 0x14C: // DMAC unused
    case 0x15C: // DMAC unused
    case 0x16C: // DMAC unused
    case 0x17C: // DMAC unused
        return 0;
    case 0x14D: // DMAC unused
    case 0x15D: // DMAC unused
    case 0x16D: // DMAC unused
    case 0x17D: // DMAC unused
        return 0;
    case 0x14E: // DMAC CHCR0 (bits 8-15)
    case 0x15E: // DMAC CHCR1 (bits 8-15)
    case 0x16E: // DMAC CHCR2 (bits 8-15)
    case 0x17E: // DMAC CHCR3 (bits 8-15)
        return DMAC.channels[(address >> 4) & 3].ReadCHCR() >> 8u;
    case 0x14F: // DMAC CHCR0 (bits 0-7)
    case 0x15F: // DMAC CHCR1 (bits 0-7)
    case 0x16F: // DMAC CHCR2 (bits 0-7)
    case 0x17F: // DMAC CHCR3 (bits 0-7)
        return DMAC.channels[(address >> 4) & 3].ReadCHCR();

    case 0x148: return DMAC.ReadDMAOR() >> 8u;
    case 0x149: return DMAC.ReadDMAOR();
    case 0x158: return 0; // DMAC unused
    case 0x159: return 0; // DMAC unused
    case 0x168: return 0; // DMAC unused
    case 0x169: return 0; // DMAC unused
    case 0x178: return 0; // DMAC unused
    case 0x179: return 0; // DMAC unused

    case 0x184: // INTC IPRA (bits 8-15)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(IRQ0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(IRQ1));
        return value;
    }
    case 0x185: // INTC IPRA (bits 0-7)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(IRQ2));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(IRQ3));
        return value;
    }
    case 0x186: // INTC IPRB (bits 8-15)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(IRQ4));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(IRQ5));
        return value;
    }
    case 0x187: // INTC IPRB (bits 0-7)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(IRQ6));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(IRQ7));
        return value;
    }
    case 0x188: // INTC IPRC (bits 8-15)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(DMAC0_DEI0));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(DMAC1_DEI1));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(DMAC2_DEI2));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(DMAC3_DEI3));
        return value;
    }
    case 0x189: // INTC IPRC (bits 0-7)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU0_IMIA0));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU0_IMIB0));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU0_OVI0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU1_IMIA1));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU1_IMIB1));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU1_OVI1));
        return value;
    }
    case 0x18A: // INTC IPRD (bits 8-15)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU2_IMIA2));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU2_IMIB2));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU2_OVI2));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU3_IMIA3));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU3_IMIB3));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU3_OVI3));
        return value;
    }
    case 0x18B: // INTC IPRD (bits 0-7)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU4_IMIA4));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU4_IMIB4));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU4_OVI4));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(SCI0_ERI0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(SCI0_RxI0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(SCI0_TxI0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(SCI0_TEI0));
        return value;
    }
    case 0x18C: // INTC IPRE (bits 8-15)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(SCI1_ERI1));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(SCI1_RxI1));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(SCI1_TxI1));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(SCI1_TEI1));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(PRT_PEI));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(AD_ITI));
        return value;
    }
    case 0x18D: // INTC IPRE (bits 0-7)
    {
        using enum InterruptSource;
        uint8 value = 0;
        bit::deposit_into<4, 7>(value, INTC.GetLevel(WDT_ITI));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(REF_CMI));
        return value;
    }
    case 0x18E: return INTC.ReadICR() >> 8u;
    case 0x18F: return INTC.ReadICR();

    case 0x190: // UBC BARH (bits 8-15)
    case 0x191: // UBC BARH (bits 0-7)
    case 0x192: // UBC BARL (bits 8-15)
    case 0x193: // UBC BARL (bits 0-7)
    case 0x194: // UBC BAMRH (bits 8-15)
    case 0x195: // UBC BAMRH (bits 0-7)
    case 0x196: // UBC BAMRL (bits 8-15)
    case 0x197: // UBC BAMRL (bits 0-7)
    case 0x198: // UBC BBR (bits 8-15)
    case 0x199: // UBC BBR (bits 0-7)
        // TODO: implement
        return 0;

    case 0x1A0: return BSC.BCR.u16 >> 8u;
    case 0x1A1: return BSC.BCR.u16;
    case 0x1A2: return BSC.WCR1.u16 >> 8u;
    case 0x1A3: return BSC.WCR1.u16;
    case 0x1A4: return BSC.WCR2.u16 >> 8u;
    case 0x1A5: return BSC.WCR2.u16;
    case 0x1A6: return BSC.WCR3.u16 >> 8u;
    case 0x1A7: return BSC.WCR3.u16;
    case 0x1A8: return BSC.DCR.u16 >> 8u;
    case 0x1A9: return BSC.DCR.u16;
    case 0x1AA: return BSC.PCR.u16 >> 8u;
    case 0x1AB: return BSC.PCR.u16;
    case 0x1AC: return BSC.RCR.u16 >> 8u;
    case 0x1AD: return BSC.RCR.u16;
    case 0x1AE: return BSC.RTCSR.u16 >> 8u;
    case 0x1AF: return BSC.RTCSR.u16;
    case 0x1B0: return 0; // top half of RTCNT is always zero
    case 0x1B1: return BSC.RTCNT;
    case 0x1B2: return 0; // top half of RTCOR is always zero
    case 0x1B3: return BSC.RTCOR;

    case 0x1B8:
        if constexpr (peek) {
            return WDT.ReadTCSR<peek>();
        } else {
            // AdvanceWDT<false>();
            return /* m_WDTBusValue = */ WDT.ReadTCSR<peek>();
        }
    case 0x1B9:
        if constexpr (peek) {
            return WDT.ReadTCNT();
        } else {
            // AdvanceWDT<false>();
            return /* m_WDTBusValue = */ WDT.ReadTCNT();
        }
    case 0x1BB:
        if constexpr (peek) {
            return WDT.ReadRSTCSR();
        } else {
            // AdvanceWDT<false>();
            return /* m_WDTBusValue = */ WDT.ReadRSTCSR();
        }

    case 0x1BC: return SBYCR.u8;
    case 0x1BD: return 0u;
    case 0x1BE: return 0u;
    case 0x1BF: return 0u;

    case 0x1C0: return ReadPortA() >> 8u;
    case 0x1C1: return ReadPortA();
    case 0x1C2: return ReadPortB() >> 8u;
    case 0x1C3: return ReadPortB();
    case 0x1C4: return PFC.ReadPAIOR() >> 8u;
    case 0x1C5: return PFC.ReadPAIOR();
    case 0x1C6: return PFC.ReadPBIOR() >> 8u;
    case 0x1C7: return PFC.ReadPBIOR();
    case 0x1C8: return PFC.ReadPACR1() >> 8u;
    case 0x1C9: return PFC.ReadPACR1();
    case 0x1CA: return PFC.ReadPACR2() >> 8u;
    case 0x1CB: return PFC.ReadPACR2();
    case 0x1CC: return PFC.ReadPBCR1() >> 8u;
    case 0x1CD: return PFC.ReadPBCR1();
    case 0x1CE: return PFC.ReadPBCR2() >> 8u;
    case 0x1CF: return PFC.ReadPBCR2();
    case 0x1D0: return 0; // Upper bits of Port C are undefined
    case 0x1D1: return ReadPortC();
    case 0x1EE: return PFC.ReadCASCR() >> 8u;
    case 0x1EF: return PFC.ReadCASCR();

    case 0x1F0: return TPC.ReadTPMR();
    case 0x1F1: return TPC.ReadTPCR();
    case 0x1F2: return TPC.ReadNDERB();
    case 0x1F3: return TPC.ReadNDERA();
    case 0x1F4: return TPC.ReadNDRBGroup3();
    case 0x1F5: return TPC.ReadNDRBGroup2();
    case 0x1F6: return TPC.ReadNDRAGroup1();
    case 0x1F7: return TPC.ReadNDRAGroup0();

    default: //
        if constexpr (!peek) {
            devlog::trace<grp::reg>("[PC = {:08X}] Unhandled 8-bit on-chip register read from {:03X}", PC, address);
        }
        return 0;
    }
}

template <bool peek>
FORCE_INLINE uint16 SH1::OnChipRegReadWord(uint32 address) {
    switch (address) {
    case 0x0E0: return AD.ReadADDR<peek>(0);
    case 0x0E2: return AD.ReadADDR<peek>(1);
    case 0x0E4: return AD.ReadADDR<peek>(2);
    case 0x0E6: return AD.ReadADDR<peek>(3);

    case 0x100: return 0; // ITU TSTR and TSNC only accept 8-bit reads
    case 0x102: return 0; // ITU TMDR and TFCR only accept 8-bit reads
    case 0x104: return 0; // ITU TCR0 and TIOR0 only accept 8-bit reads
    case 0x106: return 0; // ITU TIER0 and TSR0 only accept 8-bit reads
    case 0x108: return ITU.timers[0].ReadTCNT();
    case 0x10A: return ITU.timers[0].ReadGRA();
    case 0x10C: return ITU.timers[0].ReadGRB();
    case 0x10E: return 0; // ITU TCR1 and TIOR1 only accept 8-bit reads
    case 0x110: return 0; // ITU TIER1 and TSR1 only accept 8-bit reads
    case 0x112: return ITU.timers[1].ReadTCNT();
    case 0x114: return ITU.timers[1].ReadGRA();
    case 0x116: return ITU.timers[1].ReadGRB();
    case 0x118: return 0; // ITU TCR2 and TIOR2 only accept 8-bit reads
    case 0x11A: return 0; // ITU TIER2 and TSR2 only accept 8-bit reads
    case 0x11C: return ITU.timers[2].ReadTCNT();
    case 0x11E: return ITU.timers[2].ReadGRA();
    case 0x120: return ITU.timers[2].ReadGRB();
    case 0x122: return 0; // ITU TCR3 and TIOR3 only accept 8-bit reads
    case 0x124: return 0; // ITU TIER3 and TSR3 only accept 8-bit reads
    case 0x126: return ITU.timers[3].ReadTCNT();
    case 0x128: return ITU.timers[3].ReadGRA();
    case 0x12A: return ITU.timers[3].ReadGRB();
    case 0x12C: return ITU.timers[3].ReadBRA();
    case 0x12E: return ITU.timers[3].ReadBRB();
    case 0x130: return 0; // ITU TOCR only accepts 8-bit reads
    case 0x132: return 0; // ITU TCR4 and TIOR4 only accept 8-bit reads
    case 0x134: return 0; // ITU TIER4 and TSR4 only accept 8-bit reads
    case 0x136: return ITU.timers[4].ReadTCNT();
    case 0x138: return ITU.timers[4].ReadGRA();
    case 0x13A: return ITU.timers[4].ReadGRB();
    case 0x13C: return ITU.timers[4].ReadBRA();
    case 0x13E: return ITU.timers[4].ReadBRB();

    case 0x140: // DMAC SAR0 (bits 16-31)
    case 0x150: // DMAC SAR1 (bits 16-31)
    case 0x160: // DMAC SAR2 (bits 16-31)
    case 0x170: // DMAC SAR3 (bits 16-31)
        return DMAC.channels[(address >> 4) & 3].srcAddress >> 16u;
    case 0x142: // DMAC SAR0 (bits 0-15)
    case 0x152: // DMAC SAR1 (bits 0-15)
    case 0x162: // DMAC SAR2 (bits 0-15)
    case 0x172: // DMAC SAR3 (bits 0-15)
        return DMAC.channels[(address >> 4) & 3].srcAddress;
    case 0x144: // DMAC DAR0 (bits 16-31)
    case 0x154: // DMAC DAR1 (bits 16-31)
    case 0x164: // DMAC DAR2 (bits 16-31)
    case 0x174: // DMAC DAR3 (bits 16-31)
        return DMAC.channels[(address >> 4) & 3].dstAddress >> 16u;
    case 0x146: // DMAC DAR0 (bits 0-15)
    case 0x156: // DMAC DAR1 (bits 0-15)
    case 0x166: // DMAC DAR2 (bits 0-15)
    case 0x176: // DMAC DAR3 (bits 0-15)
        return DMAC.channels[(address >> 4) & 3].dstAddress;
    case 0x14A: // DMAC TCR0
    case 0x15A: // DMAC TCR1
    case 0x16A: // DMAC TCR2
    case 0x17A: // DMAC TCR3
        return DMAC.channels[(address >> 4) & 3].xferCount;
    case 0x14C: // DMAC unused
    case 0x15C: // DMAC unused
    case 0x16C: // DMAC unused
    case 0x17C: // DMAC unused
        return 0;
    case 0x14E: // DMAC CHCR0
    case 0x15E: // DMAC CHCR1
    case 0x16E: // DMAC CHCR2
    case 0x17E: // DMAC CHCR3
        return DMAC.channels[(address >> 4) & 3].ReadCHCR();

    case 0x148: return DMAC.ReadDMAOR();
    case 0x158: return 0; // DMAC unused
    case 0x168: return 0; // DMAC unused
    case 0x178: return 0; // DMAC unused

    case 0x184: // INTC IPRA
    {
        using enum InterruptSource;
        uint16 value = 0;
        bit::deposit_into<12, 15>(value, INTC.GetLevel(IRQ0));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(IRQ1));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(IRQ2));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(IRQ3));
        return value;
    }
    case 0x186: // INTC IPRB
    {
        using enum InterruptSource;
        uint16 value = 0;
        bit::deposit_into<12, 15>(value, INTC.GetLevel(IRQ4));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(IRQ5));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(IRQ6));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(IRQ7));
        return value;
    }
    case 0x188: // INTC IPRC
    {
        using enum InterruptSource;
        uint16 value = 0;
        bit::deposit_into<12, 15>(value, INTC.GetLevel(DMAC0_DEI0));
        bit::deposit_into<12, 15>(value, INTC.GetLevel(DMAC1_DEI1));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(DMAC2_DEI2));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(DMAC3_DEI3));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU0_IMIA0));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU0_IMIB0));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU0_OVI0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU1_IMIA1));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU1_IMIB1));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(ITU1_OVI1));
        return value;
    }
    case 0x18A: // INTC IPRD
    {
        using enum InterruptSource;
        uint16 value = 0;
        bit::deposit_into<12, 15>(value, INTC.GetLevel(ITU2_IMIA2));
        bit::deposit_into<12, 15>(value, INTC.GetLevel(ITU2_IMIB2));
        bit::deposit_into<12, 15>(value, INTC.GetLevel(ITU2_OVI2));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(ITU3_IMIA3));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(ITU3_IMIB3));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(ITU3_OVI3));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU4_IMIA4));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU4_IMIB4));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(ITU4_OVI4));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(SCI0_ERI0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(SCI0_RxI0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(SCI0_TxI0));
        bit::deposit_into<0, 3>(value, INTC.GetLevel(SCI0_TEI0));
        return value;
    }
    case 0x18C: // INTC IPRE
    {
        using enum InterruptSource;
        uint16 value = 0;
        bit::deposit_into<12, 15>(value, INTC.GetLevel(SCI1_ERI1));
        bit::deposit_into<12, 15>(value, INTC.GetLevel(SCI1_RxI1));
        bit::deposit_into<12, 15>(value, INTC.GetLevel(SCI1_TxI1));
        bit::deposit_into<12, 15>(value, INTC.GetLevel(SCI1_TEI1));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(PRT_PEI));
        bit::deposit_into<8, 11>(value, INTC.GetLevel(AD_ITI));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(WDT_ITI));
        bit::deposit_into<4, 7>(value, INTC.GetLevel(REF_CMI));
        return value;
    }
    case 0x18E: return INTC.ReadICR();

    case 0x190: // UBC BARH
    case 0x192: // UBC BARL
    case 0x194: // UBC BAMRH
    case 0x196: // UBC BAMRL
    case 0x198: // UBC BBR
        // TODO: implement
        return 0;

    case 0x1A0: return BSC.BCR.u16;
    case 0x1A2: return BSC.WCR1.u16;
    case 0x1A4: return BSC.WCR2.u16;
    case 0x1A6: return BSC.WCR3.u16;
    case 0x1A8: return BSC.DCR.u16;
    case 0x1AA: return BSC.PCR.u16;
    case 0x1AC: return BSC.RCR.u16;
    case 0x1AE: return BSC.RTCSR.u16;
    case 0x1B0: return BSC.RTCNT;
    case 0x1B2: return BSC.RTCOR;

    case 0x1B8: return 0u; // WDT only accepts 8-bit reads
    case 0x1BA: return 0u; // WDT only accepts 8-bit reads

    case 0x1C0: return ReadPortA();
    case 0x1C2: return ReadPortB();
    case 0x1C4: return PFC.ReadPAIOR();
    case 0x1C6: return PFC.ReadPBIOR();
    case 0x1C8: return PFC.ReadPACR1();
    case 0x1CA: return PFC.ReadPACR2();
    case 0x1CC: return PFC.ReadPBCR1();
    case 0x1CE: return PFC.ReadPBCR2();
    case 0x1D0: return ReadPortC();
    case 0x1EE: return PFC.ReadCASCR();

    default: // Break down into two 8-bit reads
    {
        uint16 value = OnChipRegReadByte<peek>(address + 0) << 8u;
        value |= OnChipRegReadByte<peek>(address + 1) << 0u;
        return value;
    }
    }
}

template <bool peek>
FORCE_INLINE uint32 SH1::OnChipRegReadLong(uint32 address) {
    switch (address) {
    case 0x0C0: return 0; // SCI disallows 32-bit reads
    case 0x0C4: return 0; // SCI disallows 32-bit reads
    case 0x0C8: return 0; // SCI disallows 32-bit reads
    case 0x0CC: return 0; // SCI disallows 32-bit reads

    case 0x0E0: return 0; // A/D disallows 32-bit reads
    case 0x0E4: return 0; // A/D disallows 32-bit reads
    case 0x0E8: return 0; // A/D disallows 32-bit reads

    case 0x100: return 0; // ITU TSTR, TSNC, TMDR and TFCR only accept 8-bit reads
    case 0x104: return 0; // ITU TCR0, TIOR0, TIER0 and TSR0 only accept 8-bit reads
    case 0x10C: return 0; // ITU TCR1 and TIOR1 only accept 8-bit reads
    case 0x112: return 0; // ITU TIER1 and TSR1 only accept 8-bit reads
    case 0x118: return 0; // ITU TCR2, TIOR2, TIER2 and TSR2 only accept 8-bit reads
    case 0x120: return 0; // ITU TCR3 and TIOR3 only accept 8-bit reads
    case 0x124: return 0; // ITU TIER3 and TSR3 only accept 8-bit reads
    case 0x130: return 0; // ITU TOCR, TCR4 and TIOR4 only accept 8-bit reads
    case 0x134: return 0; // ITU TIER4 and TSR4 only accept 8-bit reads

    case 0x140: // DMAC SAR0
    case 0x150: // DMAC SAR1
    case 0x160: // DMAC SAR2
    case 0x170: // DMAC SAR3
        return DMAC.channels[(address >> 4) & 3].srcAddress;
    case 0x144: // DMAC DAR0
    case 0x154: // DMAC DAR1
    case 0x164: // DMAC DAR2
    case 0x174: // DMAC DAR3
        return DMAC.channels[(address >> 4) & 3].dstAddress;

    case 0x1B8: return 0u; // WDT only accepts 8-bit reads

    case 0x1F0: return 0u; // TPC disallows 32-bit reads
    case 0x1F4: return 0u; // TPC disallows 32-bit reads

    default: // Break down into two 16-bit reads
    {
        uint32 value = OnChipRegReadWord<peek>(address + 0) << 16u;
        value |= OnChipRegReadWord<peek>(address + 2) << 0u;
        return value;
    }
    }
}

template <mem_primitive T, bool poke>
/*FLATTEN_EX FORCE_INLINE_EX*/ void SH1::OnChipRegWrite(uint32 address, T value) {
    if constexpr (std::is_same_v<T, uint32>) {
        OnChipRegWriteLong<poke>(address, value);
    } else if constexpr (std::is_same_v<T, uint16>) {
        OnChipRegWriteWord<poke>(address, value);
    } else if constexpr (std::is_same_v<T, uint8>) {
        OnChipRegWriteByte<poke>(address, value);
    }
}

template <bool poke>
FORCE_INLINE void SH1::OnChipRegWriteByte(uint32 address, uint8 value) {
    switch (address) {
    case 0x0C0: SCI.channels[0].WriteSMR(value); break;
    case 0x0C1: SCI.channels[0].WriteBRR(value); break;
    case 0x0C2: SCI.channels[0].WriteSCR(value); break;
    case 0x0C3: SCI.channels[0].WriteTDR(value); break;
    case 0x0C4: SCI.channels[0].WriteSSR<poke>(value); break;
    case 0x0C5: SCI.channels[0].WriteRDR<poke>(value); break; // SCI RDR0 is read-only
    case 0x0C8: SCI.channels[1].WriteSMR(value); break;
    case 0x0C9: SCI.channels[1].WriteBRR(value); break;
    case 0x0CA: SCI.channels[1].WriteSCR(value); break;
    case 0x0CB: SCI.channels[1].WriteTDR(value); break;
    case 0x0CC: SCI.channels[1].WriteSSR<poke>(value); break;
    case 0x0CD: SCI.channels[1].WriteRDR<poke>(value); break; // SCI RDR1 is read-only

    case 0x0E0: AD.WriteADDRH<poke>(0, value); break; // A/D ADDRAH is read-only
    case 0x0E1: AD.WriteADDRL<poke>(0, value); break; // A/D ADDRAL is read-only
    case 0x0E2: AD.WriteADDRH<poke>(1, value); break; // A/D ADDRBH is read-only
    case 0x0E3: AD.WriteADDRL<poke>(1, value); break; // A/D ADDRBL is read-only
    case 0x0E4: AD.WriteADDRH<poke>(2, value); break; // A/D ADDRCH is read-only
    case 0x0E5: AD.WriteADDRL<poke>(2, value); break; // A/D ADDRCL is read-only
    case 0x0E6: AD.WriteADDRH<poke>(3, value); break; // A/D ADDRDH is read-only
    case 0x0E7: AD.WriteADDRL<poke>(3, value); break; // A/D ADDRDL is read-only
    case 0x0E8: AD.WriteADCSR<poke>(value); break;
    case 0x0E9: AD.WriteADCR(value); break;

    case 0x100: ITU.WriteTSTR(value); break;
    case 0x101: ITU.WriteTSNC(value); break;
    case 0x102: ITU.WriteTMDR(value); break;
    case 0x103: ITU.WriteTFCR(value); break;
    case 0x104: ITU.timers[0].WriteTCR(value); break;
    case 0x105: ITU.timers[0].WriteTIOR(value); break;
    case 0x106:
        ITU.timers[0].WriteTIER(value);
        if (INTC.pending.source == InterruptSource::ITU0_IMIA0 || INTC.pending.source == InterruptSource::ITU0_IMIB0 ||
            INTC.pending.source == InterruptSource::ITU0_OVI0) {
            RecalcInterrupts();
        }
        break;
    case 0x107:
        ITU.timers[0].WriteTSR<poke>(value);
        if (INTC.pending.source == InterruptSource::ITU0_IMIA0 || INTC.pending.source == InterruptSource::ITU0_IMIB0 ||
            INTC.pending.source == InterruptSource::ITU0_OVI0) {
            RecalcInterrupts();
        }
        break;
    case 0x108: ITU.timers[0].WriteTCNTHi(value); break;
    case 0x109: ITU.timers[0].WriteTCNTLo(value); break;
    case 0x10A: ITU.timers[0].WriteGRAHi(value); break;
    case 0x10B: ITU.timers[0].WriteGRALo(value); break;
    case 0x10C: ITU.timers[0].WriteGRBHi(value); break;
    case 0x10D: ITU.timers[0].WriteGRBLo(value); break;
    case 0x10E: ITU.timers[1].WriteTCR(value); break;
    case 0x10F: ITU.timers[1].WriteTIOR(value); break;
    case 0x110:
        ITU.timers[1].WriteTIER(value);
        if (INTC.pending.source == InterruptSource::ITU1_IMIA1 || INTC.pending.source == InterruptSource::ITU1_IMIB1 ||
            INTC.pending.source == InterruptSource::ITU1_OVI1) {
            RecalcInterrupts();
        }
        break;
    case 0x111:
        ITU.timers[1].WriteTSR<poke>(value);
        if (INTC.pending.source == InterruptSource::ITU1_IMIA1 || INTC.pending.source == InterruptSource::ITU1_IMIB1 ||
            INTC.pending.source == InterruptSource::ITU1_OVI1) {
            RecalcInterrupts();
        }
        break;
    case 0x112: ITU.timers[1].WriteTCNTHi(value); break;
    case 0x113: ITU.timers[1].WriteTCNTLo(value); break;
    case 0x114: ITU.timers[1].WriteGRAHi(value); break;
    case 0x115: ITU.timers[1].WriteGRALo(value); break;
    case 0x116: ITU.timers[1].WriteGRBHi(value); break;
    case 0x117: ITU.timers[1].WriteGRBLo(value); break;
    case 0x118: ITU.timers[2].WriteTCR(value); break;
    case 0x119: ITU.timers[2].WriteTIOR(value); break;
    case 0x11A:
        ITU.timers[2].WriteTIER(value);
        if (INTC.pending.source == InterruptSource::ITU2_IMIA2 || INTC.pending.source == InterruptSource::ITU2_IMIB2 ||
            INTC.pending.source == InterruptSource::ITU2_OVI2) {
            RecalcInterrupts();
        }
        break;
    case 0x11B:
        ITU.timers[2].WriteTSR<poke>(value);
        if (INTC.pending.source == InterruptSource::ITU2_IMIA2 || INTC.pending.source == InterruptSource::ITU2_IMIB2 ||
            INTC.pending.source == InterruptSource::ITU2_OVI2) {
            RecalcInterrupts();
        }
        break;
    case 0x11C: ITU.timers[2].WriteTCNTHi(value); break;
    case 0x11D: ITU.timers[2].WriteTCNTLo(value); break;
    case 0x11E: ITU.timers[2].WriteGRAHi(value); break;
    case 0x11F: ITU.timers[2].WriteGRALo(value); break;
    case 0x120: ITU.timers[2].WriteGRBHi(value); break;
    case 0x121: ITU.timers[2].WriteGRBLo(value); break;
    case 0x122: ITU.timers[3].WriteTCR(value); break;
    case 0x123: ITU.timers[3].WriteTIOR(value); break;
    case 0x124:
        ITU.timers[3].WriteTIER(value);
        if (INTC.pending.source == InterruptSource::ITU3_IMIA3 || INTC.pending.source == InterruptSource::ITU3_IMIB3 ||
            INTC.pending.source == InterruptSource::ITU3_OVI3) {
            RecalcInterrupts();
        }
        break;
    case 0x125:
        ITU.timers[3].WriteTSR<poke>(value);
        if (INTC.pending.source == InterruptSource::ITU3_IMIA3 || INTC.pending.source == InterruptSource::ITU3_IMIB3 ||
            INTC.pending.source == InterruptSource::ITU3_OVI3) {
            RecalcInterrupts();
        }
        break;
    case 0x126: ITU.timers[3].WriteTCNTHi(value); break;
    case 0x127: ITU.timers[3].WriteTCNTLo(value); break;
    case 0x128: ITU.timers[3].WriteGRAHi(value); break;
    case 0x129: ITU.timers[3].WriteGRALo(value); break;
    case 0x12A: ITU.timers[3].WriteGRBHi(value); break;
    case 0x12B: ITU.timers[3].WriteGRBLo(value); break;
    case 0x12C: ITU.timers[3].WriteBRAHi(value); break;
    case 0x12D: ITU.timers[3].WriteBRALo(value); break;
    case 0x12E: ITU.timers[3].WriteBRBHi(value); break;
    case 0x12F: ITU.timers[3].WriteBRBLo(value); break;
    case 0x131: ITU.WriteTOCR(value); break;
    case 0x132: ITU.timers[4].WriteTCR(value); break;
    case 0x133: ITU.timers[4].WriteTIOR(value); break;
    case 0x134:
        ITU.timers[4].WriteTIER(value);
        if (INTC.pending.source == InterruptSource::ITU4_IMIA4 || INTC.pending.source == InterruptSource::ITU4_IMIB4 ||
            INTC.pending.source == InterruptSource::ITU4_OVI4) {
            RecalcInterrupts();
        }
        break;
    case 0x135:
        ITU.timers[4].WriteTSR<poke>(value);
        if (INTC.pending.source == InterruptSource::ITU4_IMIA4 || INTC.pending.source == InterruptSource::ITU4_IMIB4 ||
            INTC.pending.source == InterruptSource::ITU4_OVI4) {
            RecalcInterrupts();
        }
        break;
    case 0x136: ITU.timers[4].WriteTCNTHi(value); break;
    case 0x137: ITU.timers[4].WriteTCNTLo(value); break;
    case 0x138: ITU.timers[4].WriteGRAHi(value); break;
    case 0x139: ITU.timers[4].WriteGRALo(value); break;
    case 0x13A: ITU.timers[4].WriteGRBHi(value); break;
    case 0x13B: ITU.timers[4].WriteGRBLo(value); break;
    case 0x13C: ITU.timers[4].WriteBRAHi(value); break;
    case 0x13D: ITU.timers[4].WriteBRALo(value); break;
    case 0x13E: ITU.timers[4].WriteBRBHi(value); break;
    case 0x13F: ITU.timers[4].WriteBRBLo(value); break;

    case 0x140: // DMAC SAR0 (bits 24-31)
    case 0x150: // DMAC SAR1 (bits 24-31)
    case 0x160: // DMAC SAR2 (bits 24-31)
    case 0x170: // DMAC SAR3 (bits 24-31)
        bit::deposit_into<24, 31>(DMAC.channels[(address >> 4) & 3].srcAddress, value);
        break;
    case 0x141: // DMAC SAR0 (bits 16-23)
    case 0x151: // DMAC SAR1 (bits 16-23)
    case 0x161: // DMAC SAR2 (bits 16-23)
    case 0x171: // DMAC SAR3 (bits 16-23)
        bit::deposit_into<16, 23>(DMAC.channels[(address >> 4) & 3].srcAddress, value);
        break;
    case 0x142: // DMAC SAR0 (bits 8-15)
    case 0x152: // DMAC SAR1 (bits 8-15)
    case 0x162: // DMAC SAR2 (bits 8-15)
    case 0x172: // DMAC SAR3 (bits 8-15)
        bit::deposit_into<8, 15>(DMAC.channels[(address >> 4) & 3].srcAddress, value);
        break;
    case 0x143: // DMAC SAR0 (bits 0-7)
    case 0x153: // DMAC SAR1 (bits 0-7)
    case 0x163: // DMAC SAR2 (bits 0-7)
    case 0x173: // DMAC SAR3 (bits 0-7)
        bit::deposit_into<0, 7>(DMAC.channels[(address >> 4) & 3].srcAddress, value);
        break;
    case 0x144: // DMAC DAR0 (bits 24-31)
    case 0x154: // DMAC DAR1 (bits 24-31)
    case 0x164: // DMAC DAR2 (bits 24-31)
    case 0x174: // DMAC DAR3 (bits 24-31)
        bit::deposit_into<24, 31>(DMAC.channels[(address >> 4) & 3].dstAddress, value);
        break;
    case 0x145: // DMAC DAR0 (bits 16-23)
    case 0x155: // DMAC DAR1 (bits 16-23)
    case 0x165: // DMAC DAR2 (bits 16-23)
    case 0x175: // DMAC DAR3 (bits 16-23)
        bit::deposit_into<16, 23>(DMAC.channels[(address >> 4) & 3].dstAddress, value);
        break;
    case 0x146: // DMAC DAR0 (bits 8-7)
    case 0x156: // DMAC DAR1 (bits 8-7)
    case 0x166: // DMAC DAR2 (bits 8-7)
    case 0x176: // DMAC DAR3 (bits 8-7)
        bit::deposit_into<8, 15>(DMAC.channels[(address >> 4) & 3].dstAddress, value);
        break;
    case 0x147: // DMAC DAR0 (bits 0-7)
    case 0x157: // DMAC DAR1 (bits 0-7)
    case 0x167: // DMAC DAR2 (bits 0-7)
    case 0x177: // DMAC DAR3 (bits 0-7)
        bit::deposit_into<0, 7>(DMAC.channels[(address >> 4) & 3].dstAddress, value);
        break;
    case 0x14A: // DMAC TCR0 (bits 8-15)
    case 0x15A: // DMAC TCR1 (bits 8-15)
    case 0x16A: // DMAC TCR2 (bits 8-15)
    case 0x17A: // DMAC TCR3 (bits 8-15)
        bit::deposit_into<8, 15>(DMAC.channels[(address >> 4) & 3].xferCount, value);
        break;
    case 0x14B: // DMAC TCR0 (bits 0-7)
    case 0x15B: // DMAC TCR1 (bits 0-7)
    case 0x16B: // DMAC TCR2 (bits 0-7)
    case 0x17B: // DMAC TCR3 (bits 0-7)
        bit::deposit_into<0, 7>(DMAC.channels[(address >> 4) & 3].xferCount, value);
        break;
    case 0x14C: // DMAC unused
    case 0x15C: // DMAC unused
    case 0x16C: // DMAC unused
    case 0x17C: // DMAC unused
        break;
    case 0x14D: // DMAC unused
    case 0x15D: // DMAC unused
    case 0x16D: // DMAC unused
    case 0x17D: // DMAC unused
        break;
    case 0x14E: // DMAC CHCR0 (bits 8-15)
    case 0x15E: // DMAC CHCR1 (bits 8-15)
    case 0x16E: // DMAC CHCR2 (bits 8-15)
    case 0x17E: // DMAC CHCR3 (bits 8-15)
    {
        const uint32 index = (address >> 4) & 3;
        auto &ch = DMAC.channels[index];
        ch.WriteCHCRHi(value);
        if constexpr (!poke) {
            RunDMAC(index); // TODO: should be scheduled
            if (!DMAC.DMAOR.DME || !ch.xferEnded || !ch.irqEnable) {
                LowerInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::DMAC0_DEI0) - index));
            }
        }
        break;
    }
    case 0x14F: // DMAC CHCR0 (bits 0-7)
    case 0x15F: // DMAC CHCR1 (bits 0-7)
    case 0x16F: // DMAC CHCR2 (bits 0-7)
    case 0x17F: // DMAC CHCR3 (bits 0-7)
    {
        const uint32 index = (address >> 4) & 3;
        auto &ch = DMAC.channels[index];
        ch.WriteCHCRLo<poke>(value);
        if constexpr (!poke) {
            RunDMAC(index); // TODO: should be scheduled
            if (!DMAC.DMAOR.DME || !ch.xferEnded || !ch.irqEnable) {
                LowerInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::DMAC0_DEI0) - index));
            }
        }
        break;
    }

    case 0x148: break; // DMAC DMAOR bits 8-15 are unused
    case 0x149:
        DMAC.WriteDMAOR<poke>(value);
        if constexpr (!poke) {
            for (uint32 i = 0; i < 4; ++i) {
                RunDMAC(i); // TODO: should be scheduled
                if (!DMAC.DMAOR.DME) {
                    LowerInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::DMAC0_DEI0) - i));
                }
            }
        }
        break;
    case 0x158: break; // DMAC unused
    case 0x159: break; // DMAC unused
    case 0x168: break; // DMAC unused
    case 0x169: break; // DMAC unused
    case 0x178: break; // DMAC unused
    case 0x179: break; // DMAC unused

    case 0x184: // INTC IPRA (bits 8-15)
        using enum InterruptSource;
        INTC.SetLevel(IRQ0, bit::extract<4, 7>(value));
        INTC.SetLevel(IRQ1, bit::extract<0, 3>(value));
        UpdateInterruptLevels<IRQ0, IRQ1>();
        break;
    case 0x185: // INTC IPRA (bits 0-7)
        using enum InterruptSource;
        INTC.SetLevel(IRQ2, bit::extract<4, 7>(value));
        INTC.SetLevel(IRQ3, bit::extract<0, 3>(value));
        UpdateInterruptLevels<IRQ2, IRQ3>();
        break;
    case 0x186: // INTC IPRB (bits 8-15)
        using enum InterruptSource;
        INTC.SetLevel(IRQ4, bit::extract<4, 7>(value));
        INTC.SetLevel(IRQ5, bit::extract<0, 3>(value));
        UpdateInterruptLevels<IRQ4, IRQ5, IRQ6, IRQ7>();
        break;
    case 0x187: // INTC IPRB (bits 0-7)
        using enum InterruptSource;
        INTC.SetLevel(IRQ6, bit::extract<4, 7>(value));
        INTC.SetLevel(IRQ7, bit::extract<0, 3>(value));
        UpdateInterruptLevels<IRQ4, IRQ5, IRQ6, IRQ7>();
        break;
    case 0x188: // INTC IPRC (bits 8-15)
        using enum InterruptSource;
        INTC.SetLevel(DMAC0_DEI0, bit::extract<4, 7>(value));
        INTC.SetLevel(DMAC1_DEI1, bit::extract<4, 7>(value));
        INTC.SetLevel(DMAC2_DEI2, bit::extract<0, 3>(value));
        INTC.SetLevel(DMAC3_DEI3, bit::extract<0, 3>(value));
        UpdateInterruptLevels<DMAC0_DEI0, DMAC1_DEI1, DMAC2_DEI2, DMAC3_DEI3>();
        break;
    case 0x189: // INTC IPRC (bits 0-7)
        using enum InterruptSource;
        INTC.SetLevel(ITU0_IMIA0, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU0_IMIB0, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU0_OVI0, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU1_IMIA1, bit::extract<0, 3>(value));
        INTC.SetLevel(ITU1_IMIB1, bit::extract<0, 3>(value));
        INTC.SetLevel(ITU1_OVI1, bit::extract<0, 3>(value));
        UpdateInterruptLevels<ITU0_IMIA0, ITU0_IMIB0, ITU0_OVI0, ITU1_IMIA1, ITU1_IMIB1, ITU1_OVI1>();
        break;
    case 0x18A: // INTC IPRD (bits 8-15)
        using enum InterruptSource;
        INTC.SetLevel(ITU2_IMIA2, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU2_IMIB2, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU2_OVI2, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU3_IMIA3, bit::extract<0, 3>(value));
        INTC.SetLevel(ITU3_IMIB3, bit::extract<0, 3>(value));
        INTC.SetLevel(ITU3_OVI3, bit::extract<0, 3>(value));
        UpdateInterruptLevels<ITU2_IMIA2, ITU2_IMIB2, ITU2_OVI2, ITU3_IMIA3, ITU3_IMIB3, ITU3_OVI3>();
        break;
    case 0x18B: // INTC IPRD (bits 0-7)
        using enum InterruptSource;
        INTC.SetLevel(ITU4_IMIA4, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU4_IMIB4, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU4_OVI4, bit::extract<4, 7>(value));
        INTC.SetLevel(SCI0_ERI0, bit::extract<0, 3>(value));
        INTC.SetLevel(SCI0_RxI0, bit::extract<0, 3>(value));
        INTC.SetLevel(SCI0_TxI0, bit::extract<0, 3>(value));
        INTC.SetLevel(SCI0_TEI0, bit::extract<0, 3>(value));
        UpdateInterruptLevels<ITU4_IMIA4, ITU4_IMIB4, ITU4_OVI4, SCI0_ERI0, SCI0_RxI0, SCI0_TxI0, SCI0_TEI0>();
        break;
    case 0x18C: // INTC IPRE (bits 8-15)
        using enum InterruptSource;
        INTC.SetLevel(SCI1_ERI1, bit::extract<4, 7>(value));
        INTC.SetLevel(SCI1_RxI1, bit::extract<4, 7>(value));
        INTC.SetLevel(SCI1_TxI1, bit::extract<4, 7>(value));
        INTC.SetLevel(SCI1_TEI1, bit::extract<4, 7>(value));
        INTC.SetLevel(PRT_PEI, bit::extract<0, 3>(value));
        INTC.SetLevel(AD_ITI, bit::extract<0, 3>(value));
        UpdateInterruptLevels<SCI1_ERI1, SCI1_RxI1, SCI1_TxI1, SCI1_TEI1, PRT_PEI, AD_ITI>();
        break;
    case 0x18D: // INTC IPRE (bits 0-7)
        using enum InterruptSource;
        INTC.SetLevel(WDT_ITI, bit::extract<4, 7>(value));
        INTC.SetLevel(REF_CMI, bit::extract<4, 7>(value));
        UpdateInterruptLevels<WDT_ITI, REF_CMI>();
        break;
    case 0x18E: INTC.WriteICR<false, true, poke>(value << 8u); break;
    case 0x18F: INTC.WriteICR<true, false, poke>(value); break;

    case 0x190: // UBC BARH (bits 8-15)
    case 0x191: // UBC BARH (bits 0-7)
    case 0x192: // UBC BARL (bits 8-15)
    case 0x193: // UBC BARL (bits 0-7)
    case 0x194: // UBC BAMRH (bits 8-15)
    case 0x195: // UBC BAMRH (bits 0-7)
    case 0x196: // UBC BAMRL (bits 8-15)
    case 0x197: // UBC BAMRL (bits 0-7)
    case 0x198: // UBC BBR (bits 8-15)
    case 0x199: // UBC BBR (bits 0-7)
        // TODO: implement
        break;

    case 0x1A0: bit::deposit_into<8, 15>(BSC.BCR.u16, value & 0xF8); break;
    case 0x1A1: break; // BCR bits 7-0 are all reserved
    case 0x1A2: bit::deposit_into<8, 15>(BSC.WCR1.u16, value); break;
    case 0x1A3: bit::deposit_into<0, 7>(BSC.WCR1.u16, (value & 0x02) | 0xFD); break;
    case 0x1A4: bit::deposit_into<8, 15>(BSC.WCR2.u16, value); break;
    case 0x1A5: bit::deposit_into<0, 7>(BSC.WCR2.u16, value); break;
    case 0x1A6: bit::deposit_into<8, 15>(BSC.WCR3.u16, value & 0xF8); break;
    case 0x1A7: break; // WCR3 bits 7-0 are all reserved
    case 0x1A8: bit::deposit_into<8, 15>(BSC.DCR.u16, value); break;
    case 0x1A9: break; // DCR bits 7-0 are all reserved
    case 0x1AA: bit::deposit_into<8, 15>(BSC.PCR.u16, value & 0xF8); break;
    case 0x1AB: break; // PCR bits 7-0 are all reserved
    case 0x1AC: break; // RCR requires 16-bit writes
    case 0x1AD: break; // RCR requires 16-bit writes
    case 0x1AE: break; // RTCSR requires 16-bit writes
    case 0x1AF: break; // RTCSR requires 16-bit writes
    case 0x1B0: break; // RTCNT requires 16-bit writes
    case 0x1B1: break; // RTCNT requires 16-bit writes
    case 0x1B2: break; // RTCOR requires 16-bit writes
    case 0x1B3: break; // RTCOR requires 16-bit writes

    case 0x1B8: break; // WDT requires 16-bit writes
    case 0x1B9: break; // WDT requires 16-bit writes
    case 0x1BA: break; // WDT requires 16-bit writes
    case 0x1BB: break; // WDT requires 16-bit writes

    case 0x1BC: SBYCR.u8 = (value & 0xC0) | 0x1F; break;
    case 0x1BD: break;
    case 0x1BE: break;
    case 0x1BF: break;

    case 0x1C0: WritePortAHi(value); break;
    case 0x1C1: WritePortALo(value); break;
    case 0x1C2: WritePortBHi(value); break;
    case 0x1C3: WritePortBLo(value); break;
    case 0x1C4: PFC.WritePAIORHi(value); break;
    case 0x1C5: PFC.WritePAIORLo(value); break;
    case 0x1C6: PFC.WritePBIORHi(value); break;
    case 0x1C7: PFC.WritePBIORLo(value); break;
    case 0x1C8: PFC.WritePACR1Hi(value); break;
    case 0x1C9: PFC.WritePACR1Lo(value); break;
    case 0x1CA: PFC.WritePACR2Hi(value); break;
    case 0x1CB: PFC.WritePACR2Lo(value); break;
    case 0x1CC: PFC.WritePBCR1Hi(value); break;
    case 0x1CD: PFC.WritePBCR1Lo(value); break;
    case 0x1CE: PFC.WritePBCR2Hi(value); break;
    case 0x1CF: PFC.WritePBCR2Lo(value); break;
    case 0x1D0: break; // Port C is read-only
    case 0x1D1: break; // Port C is read-only
    case 0x1EE: PFC.WriteCASCRHi(value); break;
    case 0x1EF: break; // PFC CASCR bits 7-0 are not writable

    case 0x1F0: TPC.WriteTPMR(value); break;
    case 0x1F1: TPC.WriteTPCR(value); break;
    case 0x1F2: TPC.WriteNDERB(value); break;
    case 0x1F3: TPC.WriteNDERA(value); break;
    case 0x1F4: TPC.WriteNDRBGroup3(value); break;
    case 0x1F5: TPC.WriteNDRBGroup2(value); break;
    case 0x1F6: TPC.WriteNDRAGroup1(value); break;
    case 0x1F7: TPC.WriteNDRAGroup0(value); break;

    default: //
        if constexpr (!poke) {
            devlog::trace<grp::reg>("[PC = {:08X}] Unhandled 8-bit on-chip register write to {:03X} = {:X}", PC,
                                    address, value);
        }
        break;
    }
}

template <bool poke>
FORCE_INLINE void SH1::OnChipRegWriteWord(uint32 address, uint16 value) {
    switch (address) {
    case 0x0E0: AD.WriteADDR<poke>(0, value); break; // A/D ADDRAH/L is read-only
    case 0x0E2: AD.WriteADDR<poke>(1, value); break; // A/D ADDRBH/L is read-only
    case 0x0E4: AD.WriteADDR<poke>(2, value); break; // A/D ADDRCH/L is read-only
    case 0x0E6: AD.WriteADDR<poke>(3, value); break; // A/D ADDRDH/L is read-only

    case 0x100: break; // ITU TSTR and TSNC only accept 8-bit writes
    case 0x102: break; // ITU TMDR and TFCR only accept 8-bit writes
    case 0x104: break; // ITU TCR0 and TIOR0 only accept 8-bit writes
    case 0x106: break; // ITU TIER0 and TSR0 only accept 8-bit writes
    case 0x108: ITU.timers[0].WriteTCNT(value); break;
    case 0x10A: ITU.timers[0].WriteGRA(value); break;
    case 0x10C: ITU.timers[0].WriteGRB(value); break;
    case 0x10E: break; // ITU TCR1 and TIOR1 only accept 8-bit writes
    case 0x110: break; // ITU TIER1 and TSR1 only accept 8-bit writes
    case 0x112: ITU.timers[1].WriteTCNT(value); break;
    case 0x114: ITU.timers[1].WriteGRA(value); break;
    case 0x116: ITU.timers[1].WriteGRB(value); break;
    case 0x118: break; // ITU TCR2 and TIOR2 only accept 8-bit writes
    case 0x11A: break; // ITU TIER2 and TSR2 only accept 8-bit writes
    case 0x11C: ITU.timers[2].WriteTCNT(value); break;
    case 0x11E: ITU.timers[2].WriteGRA(value); break;
    case 0x120: ITU.timers[2].WriteGRB(value); break;
    case 0x122: break; // ITU TCR3 and TIOR3 only accept 8-bit writes
    case 0x124: break; // ITU TIER3 and TSR3 only accept 8-bit writes
    case 0x126: ITU.timers[3].WriteTCNT(value); break;
    case 0x128: ITU.timers[3].WriteGRA(value); break;
    case 0x12A: ITU.timers[3].WriteGRB(value); break;
    case 0x12C: ITU.timers[3].WriteBRA(value); break;
    case 0x12E: ITU.timers[3].WriteBRB(value); break;
    case 0x130: break; // ITU TOCR only accepts 8-bit writes
    case 0x132: break; // ITU TCR4 and TIOR4 only accept 8-bit writes
    case 0x134: break; // ITU TIER4 and TSR4 only accept 8-bit writes
    case 0x136: ITU.timers[4].WriteTCNT(value); break;
    case 0x138: ITU.timers[4].WriteGRA(value); break;
    case 0x13A: ITU.timers[4].WriteGRB(value); break;
    case 0x13C: ITU.timers[4].WriteBRA(value); break;
    case 0x13E: ITU.timers[4].WriteBRB(value); break;

    case 0x140: // DMAC SAR0 (bits 16-31)
    case 0x150: // DMAC SAR1 (bits 16-31)
    case 0x160: // DMAC SAR2 (bits 16-31)
    case 0x170: // DMAC SAR3 (bits 16-31)
        bit::deposit_into<16, 31>(DMAC.channels[(address >> 4) & 3].srcAddress, value);
        break;
    case 0x142: // DMAC SAR0 (bits 0-15)
    case 0x152: // DMAC SAR1 (bits 0-15)
    case 0x162: // DMAC SAR2 (bits 0-15)
    case 0x172: // DMAC SAR3 (bits 0-15)
        bit::deposit_into<0, 15>(DMAC.channels[(address >> 4) & 3].srcAddress, value);
        break;
    case 0x144: // DMAC DAR0 (bits 16-31)
    case 0x154: // DMAC DAR1 (bits 16-31)
    case 0x164: // DMAC DAR2 (bits 16-31)
    case 0x174: // DMAC DAR3 (bits 16-31)
        bit::deposit_into<16, 31>(DMAC.channels[(address >> 4) & 3].dstAddress, value);
        break;
    case 0x146: // DMAC DAR0 (bits 0-15)
    case 0x156: // DMAC DAR1 (bits 0-15)
    case 0x166: // DMAC DAR2 (bits 0-15)
    case 0x176: // DMAC DAR3 (bits 0-15)
        bit::deposit_into<0, 15>(DMAC.channels[(address >> 4) & 3].dstAddress, value);
        break;
    case 0x14A: // DMAC TCR0
    case 0x15A: // DMAC TCR1
    case 0x16A: // DMAC TCR2
    case 0x17A: // DMAC TCR3
        DMAC.channels[(address >> 4) & 3].xferCount = value;
        break;
    case 0x14C: // DMAC unused
    case 0x15C: // DMAC unused
    case 0x16C: // DMAC unused
    case 0x17C: // DMAC unused
        break;
    case 0x14E: // DMAC CHCR0
    case 0x15E: // DMAC CHCR1
    case 0x16E: // DMAC CHCR2
    case 0x17E: // DMAC CHCR3
    {
        const uint32 index = (address >> 4) & 3;
        auto &ch = DMAC.channels[index];
        ch.WriteCHCR<poke>(value);
        if constexpr (!poke) {
            RunDMAC(index); // TODO: should be scheduled
            if (!DMAC.DMAOR.DME || !ch.xferEnded || !ch.irqEnable) {
                LowerInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::DMAC0_DEI0) - index));
            }
        }
        break;
    }

    case 0x148:
        DMAC.WriteDMAOR<poke>(value);
        // TODO: handle channel execution, priorities, etc.
        // TODO: handle interrupts
        break;
    case 0x158: break; // DMAC unused
    case 0x168: break; // DMAC unused
    case 0x178: break; // DMAC unused

    case 0x184: // INTC IPRA
        using enum InterruptSource;
        INTC.SetLevel(IRQ0, bit::extract<12, 15>(value));
        INTC.SetLevel(IRQ1, bit::extract<8, 11>(value));
        INTC.SetLevel(IRQ2, bit::extract<4, 7>(value));
        INTC.SetLevel(IRQ3, bit::extract<0, 3>(value));
        UpdateInterruptLevels<IRQ0, IRQ1, IRQ2, IRQ3>();
        break;
    case 0x186: // INTC IPRB
        using enum InterruptSource;
        INTC.SetLevel(IRQ4, bit::extract<12, 15>(value));
        INTC.SetLevel(IRQ5, bit::extract<8, 11>(value));
        INTC.SetLevel(IRQ6, bit::extract<4, 7>(value));
        INTC.SetLevel(IRQ7, bit::extract<0, 3>(value));
        UpdateInterruptLevels<IRQ4, IRQ5, IRQ6, IRQ7>();
        break;
    case 0x188: // INTC IPRC
        using enum InterruptSource;
        INTC.SetLevel(DMAC0_DEI0, bit::extract<12, 15>(value));
        INTC.SetLevel(DMAC1_DEI1, bit::extract<12, 15>(value));
        INTC.SetLevel(DMAC2_DEI2, bit::extract<8, 11>(value));
        INTC.SetLevel(DMAC3_DEI3, bit::extract<8, 11>(value));
        INTC.SetLevel(ITU0_IMIA0, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU0_IMIB0, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU0_OVI0, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU1_IMIA1, bit::extract<0, 3>(value));
        INTC.SetLevel(ITU1_IMIB1, bit::extract<0, 3>(value));
        INTC.SetLevel(ITU1_OVI1, bit::extract<0, 3>(value));
        UpdateInterruptLevels<DMAC0_DEI0, DMAC1_DEI1, DMAC2_DEI2, DMAC3_DEI3, ITU0_IMIA0, ITU0_IMIB0, ITU0_OVI0,
                              ITU1_IMIA1, ITU1_IMIB1, ITU1_OVI1>();
        break;
    case 0x18A: // INTC IPRD
        using enum InterruptSource;
        INTC.SetLevel(ITU2_IMIA2, bit::extract<12, 15>(value));
        INTC.SetLevel(ITU2_IMIB2, bit::extract<12, 15>(value));
        INTC.SetLevel(ITU2_OVI2, bit::extract<12, 15>(value));
        INTC.SetLevel(ITU3_IMIA3, bit::extract<8, 11>(value));
        INTC.SetLevel(ITU3_IMIB3, bit::extract<8, 11>(value));
        INTC.SetLevel(ITU3_OVI3, bit::extract<8, 11>(value));
        INTC.SetLevel(ITU4_IMIA4, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU4_IMIB4, bit::extract<4, 7>(value));
        INTC.SetLevel(ITU4_OVI4, bit::extract<4, 7>(value));
        INTC.SetLevel(SCI0_ERI0, bit::extract<0, 3>(value));
        INTC.SetLevel(SCI0_RxI0, bit::extract<0, 3>(value));
        INTC.SetLevel(SCI0_TxI0, bit::extract<0, 3>(value));
        INTC.SetLevel(SCI0_TEI0, bit::extract<0, 3>(value));
        UpdateInterruptLevels<ITU2_IMIA2, ITU2_IMIB2, ITU2_OVI2, ITU3_IMIA3, ITU3_IMIB3, ITU3_OVI3, ITU4_IMIA4,
                              ITU4_IMIB4, ITU4_OVI4, SCI0_ERI0, SCI0_RxI0, SCI0_TxI0, SCI0_TEI0>();
        break;
    case 0x18C: // INTC IPRE
        using enum InterruptSource;
        INTC.SetLevel(SCI1_ERI1, bit::extract<12, 15>(value));
        INTC.SetLevel(SCI1_RxI1, bit::extract<12, 15>(value));
        INTC.SetLevel(SCI1_TxI1, bit::extract<12, 15>(value));
        INTC.SetLevel(SCI1_TEI1, bit::extract<12, 15>(value));
        INTC.SetLevel(PRT_PEI, bit::extract<8, 11>(value));
        INTC.SetLevel(AD_ITI, bit::extract<8, 11>(value));
        INTC.SetLevel(WDT_ITI, bit::extract<4, 7>(value));
        INTC.SetLevel(REF_CMI, bit::extract<4, 7>(value));
        UpdateInterruptLevels<SCI1_ERI1, SCI1_RxI1, SCI1_TxI1, SCI1_TEI1, PRT_PEI, AD_ITI, WDT_ITI, REF_CMI>();
        break;
    case 0x18E: INTC.WriteICR<true, true, poke>(value); break;

    case 0x190: // UBC BARH
    case 0x192: // UBC BARL
    case 0x194: // UBC BAMRH
    case 0x196: // UBC BAMRL
    case 0x198: // UBC BBR
        // TODO: implement
        break;

    case 0x1A0: BSC.BCR.u16 = value & 0xF800; break;
    case 0x1A2: BSC.WCR1.u16 = (value & 0xFF02) | 0x00FD; break;
    case 0x1A4: BSC.WCR2.u16 = value; break;
    case 0x1A6: BSC.WCR3.u16 = value & 0xF800; break;
    case 0x1A8: BSC.DCR.u16 = value & 0xFF00; break;
    case 0x1AA: BSC.PCR.u16 = value & 0xF800; break;
    case 0x1AC:
        if (bit::extract<8, 15>(value) == 0x5A) {
            BSC.RCR.u16 = bit::extract<0, 7>(value) & 0xF0;
        }
        break;
    case 0x1AE:
        if (bit::extract<8, 15>(value) == 0xA5) {
            BSC.RTCSR.u16 = bit::extract<0, 7>(value) & 0xF8;
        }
        break;
    case 0x1B0:
        if (bit::extract<8, 15>(value) == 0x69) {
            BSC.RTCNT = bit::extract<0, 7>(value);
        }
        break;
    case 0x1B2:
        if (bit::extract<8, 15>(value) == 0x96) {
            BSC.RTCOR = bit::extract<0, 7>(value);
        }
        break;

    case 0x1B8:
        if ((value >> 8u) == 0x5A) {
            if constexpr (!poke) {
                // AdvanceWDT<true>();
            }
            WDT.WriteTCNT(value);
        } else if ((value >> 8u) == 0xA5) {
            if constexpr (!poke) {
                // AdvanceWDT<true>();
            }
            WDT.WriteTCSR<poke>(value);
            if (!WDT.TCSR.TME || !WDT.TCSR.OVF) {
                LowerInterrupt(InterruptSource::WDT_ITI);
            }
        }
        break;
    case 0x1BA:
        if constexpr (!poke) {
            // m_WDTBusValue = value;
        }
        if ((value >> 8u) == 0x5A) {
            WDT.WriteRSTE_RSTS(value);
        } else if ((value >> 8u) == 0xA5) {
            WDT.WriteWOVF<poke>(value);
        }
        break;

    case 0x1C0: WritePortA(value); break;
    case 0x1C2: WritePortB(value); break;
    case 0x1C4: PFC.WritePAIOR(value); break;
    case 0x1C6: PFC.WritePBIOR(value); break;
    case 0x1C8: PFC.WritePACR1(value); break;
    case 0x1CA: PFC.WritePACR2(value); break;
    case 0x1CC: PFC.WritePBCR1(value); break;
    case 0x1CE: PFC.WritePBCR2(value); break;
    case 0x1D0: break; // Port C is read-only
    case 0x1EE: PFC.WriteCASCR(value); break;

    default: //
        OnChipRegWriteByte<poke>(address + 0, value >> 8u);
        OnChipRegWriteByte<poke>(address + 1, value >> 0u);
        break;
    }
}

template <bool poke>
FORCE_INLINE void SH1::OnChipRegWriteLong(uint32 address, uint32 value) {
    switch (address) {
    case 0x0C0: break; // SCI disallows 32-bit writes
    case 0x0C4: break; // SCI disallows 32-bit writes
    case 0x0C8: break; // SCI disallows 32-bit writes
    case 0x0CC: break; // SCI disallows 32-bit writes

    case 0x0E0: break; // A/D disallows 32-bit writes
    case 0x0E4: break; // A/D disallows 32-bit writes
    case 0x0E8: break; // A/D disallows 32-bit writes

    case 0x100: break; // ITU TSTR, TSNC, TMDR and TFCR only accept 8-bit reads
    case 0x104: break; // ITU TCR0, TIOR0, TIER0 and TSR0 only accept 8-bit reads
    case 0x10C: break; // ITU TCR1 and TIOR1 only accept 8-bit reads
    case 0x112: break; // ITU TIER1 and TSR1 only accept 8-bit reads
    case 0x118: break; // ITU TCR2, TIOR2, TIER2 and TSR2 only accept 8-bit reads
    case 0x120: break; // ITU TCR3 and TIOR3 only accept 8-bit reads
    case 0x124: break; // ITU TIER3 and TSR3 only accept 8-bit reads
    case 0x130: break; // ITU TOCR, TCR4 and TIOR4 only accept 8-bit reads
    case 0x134: break; // ITU TIER4 and TSR4 only accept 8-bit reads

    case 0x140: // DMAC SAR0
    case 0x150: // DMAC SAR1
    case 0x160: // DMAC SAR2
    case 0x170: // DMAC SAR3
        DMAC.channels[(address >> 4) & 3].srcAddress = value;
        break;
    case 0x144: // DMAC DAR0 (bits 16-31)
    case 0x154: // DMAC DAR1 (bits 16-31)
    case 0x164: // DMAC DAR2 (bits 16-31)
    case 0x174: // DMAC DAR3 (bits 16-31)
        DMAC.channels[(address >> 4) & 3].dstAddress = value;
        break;

    case 0x1B8: break; // WDT disallows 32-bit writes

    case 0x1F0: break; // TPC disallows 32-bit writes
    case 0x1F4: break; // TPC disallows 32-bit writes

    default:
        OnChipRegWriteWord<poke>(address + 0, value >> 16u);
        OnChipRegWriteWord<poke>(address + 2, value >> 0u);
        break;
    }
}

void SH1::RunDMAC(uint32 channel) {
    assert(channel < DMAC.channels.size());
    auto &ch = DMAC.channels[channel];

    // TODO: prioritize channels based on DMAOR.PRn
    // TODO: proper timings, cycle-stealing, etc. (suspend instructions if not cached)

    if (!IsDMATransferActive(ch)) {
        return;
    }

    switch (ch.xferResSelect) {
    case DMAResourceSelect::nDREQDual: [[fallthrough]];
    case DMAResourceSelect::nDREQSingleDACKDst: [[fallthrough]];
    case DMAResourceSelect::nDREQSingleDACKSrc:
        if (channel >= 2) {
            // No DREQ# signals for these channels
            return;
        }
        if (m_nDREQ[channel]) {
            // DREQ# not asserted
            return;
        }
        break;
    case DMAResourceSelect::SCI0_RXI0: /*TODO*/ return;
    case DMAResourceSelect::SCI0_TXI0: /*TODO*/ return;
    case DMAResourceSelect::SCI1_RXI1: /*TODO*/ return;
    case DMAResourceSelect::SCI1_TXI1: /*TODO*/ return;
    case DMAResourceSelect::ITU0_IMIA0: /*TODO*/ return;
    case DMAResourceSelect::ITU1_IMIA1: /*TODO*/ return;
    case DMAResourceSelect::ITU2_IMIA2: /*TODO*/ return;
    case DMAResourceSelect::ITU3_IMIA3: /*TODO*/ return;
    case DMAResourceSelect::AutoRequest: break;
    case DMAResourceSelect::AD_ADI: /*TODO*/ return;
    case DMAResourceSelect::Reserved1: [[fallthrough]];
    case DMAResourceSelect::ReservedE: [[fallthrough]];
    case DMAResourceSelect::ReservedF: return;
    }

    devlog::trace<grp::dma>("DMAC{} transfer started: {:08X} to {:08X}, {:04X} {} units", channel, ch.srcAddress,
                            ch.dstAddress, ch.xferCount, (ch.xferSize == DMATransferSize::Byte ? "byte" : "word"));

    static constexpr uint32 kXferSize[] = {1, 2};
    const uint32 xferSize = kXferSize[static_cast<uint32>(ch.xferSize)];
    auto getAddressInc = [&](DMATransferIncrementMode mode) -> sint32 {
        using enum DMATransferIncrementMode;
        switch (mode) {
        default: [[fallthrough]];
        case Fixed: return 0;
        case Increment: return +xferSize;
        case Decrement: return -xferSize;
        case Reserved: return 0;
        }
    };

    const sint32 srcInc = getAddressInc(ch.srcMode);
    const sint32 dstInc = getAddressInc(ch.dstMode);

    do {
        // Perform one unit of transfer
        switch (ch.xferSize) {
        case DMATransferSize::Byte: {
            const uint8 value = MemReadByte(ch.srcAddress);
            MemWriteByte(ch.dstAddress, value);
            break;
        }
        case DMATransferSize::Word: {
            const uint16 value = MemReadWord(ch.srcAddress);
            MemWriteWord(ch.dstAddress, value);
            break;
        }
        }

        // Update address and remaining count
        ch.srcAddress += srcInc;
        ch.dstAddress += dstInc;
        --ch.xferCount;
    } while (ch.xferCount != 0);

    ch.xferEnded = true;
    devlog::trace<grp::dma>("DMAC{} transfer finished", channel);
    if (ch.irqEnable) {
        devlog::trace<grp::dma>("DMAC{} DEI{} raised", channel, channel);
        switch (channel) {
        case 0: RaiseInterrupt(InterruptSource::DMAC0_DEI0); break;
        case 1: RaiseInterrupt(InterruptSource::DMAC1_DEI1); break;
        case 2: RaiseInterrupt(InterruptSource::DMAC2_DEI2); break;
        case 3: RaiseInterrupt(InterruptSource::DMAC3_DEI3); break;
        }
    }
}

FLATTEN FORCE_INLINE bool SH1::IsDMATransferActive(const DMAController::DMAChannel &ch) const {
    return ch.IsEnabled() && DMAC.DMAOR.DME && !DMAC.DMAOR.NMIF && !DMAC.DMAOR.AE;
}

FORCE_INLINE void SH1::SCI0RxBit(bool bit) {
    auto &ch = SCI.channels[0];
    ch.ReceiveBit(bit);
    if (ch.RSRbit == 0u && ch.rxIntrEnable && ch.rxFull) {
        RaiseInterrupt(InterruptSource::SCI0_RxI0);
    }
}

FORCE_INLINE bool SH1::SCI0TxBit() {
    auto &ch = SCI.channels[0];
    const bool value = ch.TransmitBit();
    // TODO: handle Tx-related interrupts
    return value;
}

// TODO: Wire up pins:
// A0  I TIOCA0    <- MPEG card - MPEG A data transfer (DMA3) request input (edge detected)
// A1  O RAS#      -> DRAM - CASL output
// A2  O CS6#      -> YGR - [CS6#] CS6 output (area 6 access -- MPEG ROM)
// A3  I PA3       <- Vss (=0)
// A4  O WRL#/WR#  -> DRAM/external space - WRL output / test point TP6
// A5  O WRH#/LBS# -> DRAM/external space - WRH output
// A6  O RD#       -> DRAM/external space - RD output
// A7  O PA7       -> YGR - [RESET] GA reset output (0=reset, 1=release) / test point TP5 (GND)
// A8  O PA8
// A9  O PA9       -> [IRQOUT] Spare to output 0 during interrupt process
// A10 I TIOCA1    <- MPEG card - MPEG V data transfer (DMA2) request input (edge detected)
// A11 O PA11
// A12 O DACK0     -> YGR - [DACK0]  DMA0 (for CD-ROM sector data input) ACK output (to GA)
// A13 I DREQ0#    <- YGR - [DREQ0#] DMA0 (for CD-ROM sector data input) REQ input (from GA)
// A14 O DACK1     -> YGR - [DACK1]  DMA1 (for host data input/output) ACK output (to GA)
// A15 I DREQ1#    <- YGR - [DREQ1#] DMA1 (for host data input/output) REQ output (from GA)
//
// B0  I TIOCA2    <- MPEG card - [MPEGA_IRQ#] MPEG A interrupt input (edge detected)
// B1  I TIOCB2    <- MPEG card - [MPEGV_IRQ#] MPEG V interrupt input (edge detected)
// B2  I PB2       <- CD drive - [COMSYNC#] CD comms start signal input: 0=start, 1=middle
// B3  I TIOCB3    <- CD drive - [COMREQ#] CD comms sync interrupt input (edge detected): 0=on clock xfer, 1=on interval
// B4  O PB4
// B5  O PB5
// B6  O PB6       -> CD drive - [DEMP] Deemphasis output: 0=off, 1=on
// B7  O PB7
// B8  I RxD0      <- CD drive - [CDATA] CD comms data input (SCK0 used)
// B9  O TxD0      -> CD drive - [HDATA] CD comms data output (SCK0 used)
// B10 I PB10      <- test point TP3 - [TEST#] Test control input (0=test mode, 1=normal mode) (=1)
// B11 O TxD1      -> test point TP2 - [TDATA] Test data output
// B12 O SCK0      -> CD drive - [COMCLK] CD comms clock output (SCK0 used)
// B13 O SCK1      -> test point TP1 - [TCLK] Clock output for test data
// B14 I IRQ6#     <- YGR - [IRQ_L#] Host command interrupt input (level detected)
// B15 I IRQ7#     <- YGR - [IRQ_H#] CD-ROM sync detection, subcode sync detection interrupt input (level detected)
//
// C0  I PC0/AN0
// C1  I PC1/AN1
// C2  I PC2/AN2
// C3  I PC3/AN3
// C4  I PC4/AN4
// C5  I PC5/AN5   <- MPEG card - [VCD] CD/VCD detection (0=CD drive connected, 1=VCD connected)
// C6  I PC6/AN6
// C7  I PC7/AN7

uint16 SH1::ReadPortA() const {
    return (m_nDREQ[1] << 15u) | (m_nDREQ[0] << 13u);
}

void SH1::WritePortA(uint16 value) const {}

void SH1::WritePortAHi(uint8 value) const {}

void SH1::WritePortALo(uint8 value) const {}

uint16 SH1::ReadPortB() const {
    // Pin B10 is test point 3 (TP3) aka TEST# - Test control input
    //   0 = test mode
    //   1 = normal mode
    const uint16 B10 = 1u;
    return (INTC.IsIRQAsserted(7) << 15u) | (INTC.IsIRQAsserted(6) << 14u) | (B10 << 10u) | (m_TIOCB3 << 3u) |
           (m_PB2 << 2u);
}

void SH1::WritePortB(uint16 value) const {}

void SH1::WritePortBHi(uint8 value) const {}

void SH1::WritePortBLo(uint8 value) const {}

uint8 SH1::ReadPortC() const {
    return 0u;
}

void SH1::AdvanceITU() {
    const uint64 cycles = GetCurrentCycleCount();

    for (uint32 i = 0; i < 5; ++i) {
        auto &timer = ITU.timers[i];
        if (!timer.started) {
            continue;
        }

        // Must be monotonically increasing
        assert(cycles >= timer.currCycles);

        const uint64 timerCycles = cycles - timer.currCycles;

        uint64 steps = 0;
        using Prescaler = IntegratedTimerPulseUnit::Timer::Prescaler;
        switch (timer.prescaler) {
        case Prescaler::Phi:
            steps = timerCycles;
            timer.currCycles = cycles;
            break;
        case Prescaler::Phi2:
            steps = timerCycles >> 1ull;
            timer.currCycles = cycles & ~1ull;
            break;
        case Prescaler::Phi4:
            steps = timerCycles >> 2ull;
            timer.currCycles = cycles & ~3ull;
            break;
        case Prescaler::Phi8:
            steps = timerCycles >> 3ull;
            timer.currCycles = cycles & ~7ull;
            break;
        default: timer.currCycles = cycles; break; // TCLKA to TCLKD, not implemented
        }

        uint64 nextCount = timer.counter + steps;
        if (nextCount >= 0x10000) {
            timer.OVF = true;
            if (timer.OVFIntrEnable) {
                RaiseInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::ITU0_OVI0) - i * 3));
            }
        }

        using GRMode = IntegratedTimerPulseUnit::Timer::GRMode;
        using ClearMode = IntegratedTimerPulseUnit::Timer::ClearMode;
        if (static_cast<uint16>(timer.GRA - timer.counter) < steps) {
            // TODO: update TIOCAn here
            switch (timer.GRAMode) {
            case GRMode::OutputNone: break;
            case GRMode::Output0: break;
            case GRMode::Output1: break;
            case GRMode::OutputToggle:
                if (i == 2) {
                    // Behaves like Output1 instead
                } else {
                }
                break;
            default: break; // the rest are inputs
            }

            timer.IMFA = true;
            if (timer.clearMode == ClearMode::GRA) {
                nextCount = 0;
            }
            if (timer.IMFAIntrEnable) {
                RaiseInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::ITU0_IMIA0) - i * 3));
            }
        }

        if (static_cast<uint16>(timer.GRB - timer.counter) < steps) {
            // TODO: update TIOCBn here
            switch (timer.GRBMode) {
            case GRMode::OutputNone: break;
            case GRMode::Output0: break;
            case GRMode::Output1: break;
            case GRMode::OutputToggle:
                if (i == 2) {
                    // Behaves like Output1 instead
                } else {
                }
                break;
            default: break; // the rest are inputs
            }

            timer.IMFB = true;
            if (timer.clearMode == ClearMode::GRB) {
                nextCount = 0;
            }
            if (timer.IMFBIntrEnable) {
                RaiseInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::ITU0_IMIB0) - i * 3));
            }
        }
        timer.counter = nextCount;
    }
}

void SH1::AdvanceSCI() {
    const uint64 cycles = GetCurrentCycleCount();

    for (uint32 i = 0; i < 2; ++i) {
        auto &ch = SCI.channels[i];
        if (ch.clockEnable >= 2) {
            // External clock signals are not implemented
            continue;
        }
        if (!ch.sync) {
            // Async mode not implemented
            continue;
        }

        // Must be monotonically increasing
        assert(cycles >= ch.currCycles);

        uint64 chCycles = cycles - ch.currCycles;
        while (chCycles >= ch.cyclesPerBit) {
            chCycles -= ch.cyclesPerBit;
            if (ch.txEnd) {
                break;
            }
            if (ch.rxEnable) {
                ch.ReceiveBit(m_cbSerialRx[i]());
            }
            if (ch.txEnable) {
                m_cbSerialTx[i](ch.TransmitBit());
            }
        }
        ch.currCycles = cycles - chCycles;
    }
}

// -----------------------------------------------------------------------------
// Interrupts

void SH1::SetnIRQ(uint8 irqnum, bool level) {
    assert(irqnum < 8);

    const auto source = static_cast<InterruptSource>(static_cast<uint8>(InterruptSource::IRQ0) - irqnum);
    const bool trigger = INTC.UpdateIRQ(irqnum, level);
    if (trigger) {
        switch (irqnum) {
        case 0: UpdateInterruptLevels<InterruptSource::IRQ0>(); break;
        case 1: UpdateInterruptLevels<InterruptSource::IRQ1>(); break;
        case 2: UpdateInterruptLevels<InterruptSource::IRQ2>(); break;
        case 3: UpdateInterruptLevels<InterruptSource::IRQ3>(); break;
        case 4: UpdateInterruptLevels<InterruptSource::IRQ4>(); break;
        case 5: UpdateInterruptLevels<InterruptSource::IRQ5>(); break;
        case 6: UpdateInterruptLevels<InterruptSource::IRQ6>(); break;
        case 7: UpdateInterruptLevels<InterruptSource::IRQ7>(); break;
        }

        RaiseInterrupt(source);
        devlog::trace<grp::intr>("IRQ{} raised; pending level {:X}", irqnum, INTC.pending.level);
    } else {
        LowerInterrupt(source);
    }
}

template <InterruptSource source, InterruptSource... sources>
FLATTEN FORCE_INLINE void SH1::UpdateInterruptLevels() {
    if (INTC.pending.source == source) {
        const uint8 newLevel = INTC.GetLevel(source);
        if (newLevel < INTC.pending.level) {
            // Interrupt may no longer have the highest priority; recalculate
            RecalcInterrupts();
        } else {
            // Interrupt still has the highest priority; update level
            INTC.pending.level = newLevel;
        }
    }
    if constexpr (sizeof...(sources) > 1) {
        UpdateInterruptLevels<sources...>();
    }
}

void SH1::RecalcInterrupts() {
    // Check interrupts and use the vector number of the exception with highest priority
    // See documentation for InterruptSource for related registers and default/tie-breaker priority order

    INTC.pending.level = 0;
    INTC.pending.source = InterruptSource::None;
    m_intrPending = false;

    // HACK: should be edge-detected
    if (INTC.NMI) {
        RaiseInterrupt(InterruptSource::NMI);
        return; // highest priority
    }

    // TODO: user break
    /*if (...) {
        RaiseInterrupt(InterruptSource::UserBreak);
        return; // next highest priority; no other interrupt can have a priority higher than this
    }*/

    // From this point on, all interrupt sources have dynamic priorities, so we need to check them all
    // TODO: should probably optimize this further somehow, maybe by sorting them by priority so we can bail out early

    if (INTC.IsIRQAsserted(1)) {
        RaiseInterrupt(InterruptSource::IRQ0);
    }
    if (INTC.IsIRQAsserted(1)) {
        RaiseInterrupt(InterruptSource::IRQ1);
    }
    if (INTC.IsIRQAsserted(2)) {
        RaiseInterrupt(InterruptSource::IRQ2);
    }
    if (INTC.IsIRQAsserted(3)) {
        RaiseInterrupt(InterruptSource::IRQ3);
    }
    if (INTC.IsIRQAsserted(4)) {
        RaiseInterrupt(InterruptSource::IRQ4);
    }
    if (INTC.IsIRQAsserted(5)) {
        RaiseInterrupt(InterruptSource::IRQ5);
    }
    if (INTC.IsIRQAsserted(6)) {
        RaiseInterrupt(InterruptSource::IRQ6);
    }
    if (INTC.IsIRQAsserted(7)) {
        RaiseInterrupt(InterruptSource::IRQ7);
    }

    if (DMAC.DMAOR.DME) {
        for (uint32 i = 0; i < 4; ++i) {
            const auto &ch = DMAC.channels[i];
            if (ch.irqEnable && ch.xferEnded) {
                RaiseInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::DMAC0_DEI0) - i));
            }
        }
    }

    for (uint32 i = 0; i < 5; ++i) {
        const auto &timer = ITU.timers[i];
        if (timer.IMFAIntrEnable && timer.IMFA) {
            RaiseInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::ITU0_IMIA0) - i * 3));
        } else if (timer.IMFBIntrEnable && timer.IMFB) {
            RaiseInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::ITU0_IMIB0) - i * 3));
        } else if (timer.OVFIntrEnable && timer.OVF) {
            RaiseInterrupt(static_cast<InterruptSource>(static_cast<uint32>(InterruptSource::ITU0_OVI0) - i * 3));
        }
    }

    // TODO: SCI0 ERI0
    if (SCI.channels[0].rxIntrEnable && SCI.channels[0].rxFull) {
        RaiseInterrupt(InterruptSource::SCI0_RxI0);
    }
    // TODO: SCI0 TxI0, TEI0

    // TODO: SCI1 ERI1
    if (SCI.channels[1].rxIntrEnable && SCI.channels[1].rxFull) {
        RaiseInterrupt(InterruptSource::SCI1_RxI1);
    }
    // TODO: SCI1 TxI1, TEI1

    // TODO: BSC REF CMI
    // TODO: WDT ITI
    // TODO: A/D ITI
    // TODO: BSC PRT PEI
}

// -------------------------------------------------------------------------
// Helper functions

FORCE_INLINE void SH1::SetupDelaySlot(uint32 targetAddress) {
    m_delaySlot = true;
    m_delaySlotTarget = targetAddress;
    m_intrPending = false;
}

template <bool delaySlot>
FORCE_INLINE void SH1::AdvancePC() {
    if constexpr (delaySlot) {
        PC = m_delaySlotTarget;
        m_delaySlot = false;
        m_intrPending = INTC.pending.level > SR.ILevel;
    } else {
        PC += 2;
    }
}

FORCE_INLINE uint64 SH1::EnterException(uint8 vectorNumber) {
    const uint32 address1 = R[15] - 4;
    const uint32 address2 = R[15] - 8;
    const uint32 address3 = VBR + (static_cast<uint32>(vectorNumber) << 2u);
    MemWriteLong(address1, SR.u32);
    MemWriteLong(address2, PC);
    PC = MemReadLong(address3);
    R[15] -= 8;
    return AccessCycles(address1) + AccessCycles(address2) + AccessCycles(address3) + 5;
}

// -----------------------------------------------------------------------------
// Instruction interpreters

FORCE_INLINE uint64 SH1::InterpretNext() {
    if (m_intrPending) [[unlikely]] {
        // Service interrupt
        const uint8 vecNum = INTC.GetVector(INTC.pending.source);
        devlog::trace<grp::intr>("[PC = {:08X}] Handling interrupt level {:02X}, vector number {:02X}", PC,
                                 INTC.pending.level, vecNum);
        const uint64 cycles = EnterException(vecNum);
        devlog::trace<grp::intr>("[PC = {:08X}] Entering interrupt handler", PC);
        SR.ILevel = std::min<uint8>(INTC.pending.level, 0xF);
        m_intrPending = false;

        // Acknowledge interrupt
        // HACK: Deassert IRQs as soon as they're handled
        switch (INTC.pending.source) {
        case InterruptSource::IRQ0: SetnIRQ(0, true); break;
        case InterruptSource::IRQ1: SetnIRQ(1, true); break;
        case InterruptSource::IRQ2: SetnIRQ(2, true); break;
        case InterruptSource::IRQ3: SetnIRQ(3, true); break;
        case InterruptSource::IRQ4: SetnIRQ(4, true); break;
        case InterruptSource::IRQ5: SetnIRQ(5, true); break;
        case InterruptSource::IRQ6: SetnIRQ(6, true); break;
        case InterruptSource::IRQ7: SetnIRQ(7, true); break;

        case InterruptSource::NMI:
            INTC.NMI = false;
            LowerInterrupt(InterruptSource::NMI);
            break;
        default: break;
        }
        return cycles + 1;
    }

    // TODO: emulate or approximate fetch - decode - execute - memory access - writeback pipeline

    const uint16 instr = FetchInstruction(PC);

    const OpcodeType opcode = DecodeTable::s_instance.opcodes[m_delaySlot][instr];
    const DecodedArgs &args = DecodeTable::s_instance.args[instr];

    // TODO: check program execution
    switch (opcode) {
    case OpcodeType::NOP: return NOP<false>();

    case OpcodeType::SLEEP: return SLEEP();

    case OpcodeType::MOV_R: return MOV<false>(args);
    case OpcodeType::MOVB_L: return MOVBL<false>(args);
    case OpcodeType::MOVW_L: return MOVWL<false>(args);
    case OpcodeType::MOVL_L: return MOVLL<false>(args);
    case OpcodeType::MOVB_L0: return MOVBL0<false>(args);
    case OpcodeType::MOVW_L0: return MOVWL0<false>(args);
    case OpcodeType::MOVL_L0: return MOVLL0<false>(args);
    case OpcodeType::MOVB_L4: return MOVBL4<false>(args);
    case OpcodeType::MOVW_L4: return MOVWL4<false>(args);
    case OpcodeType::MOVL_L4: return MOVLL4<false>(args);
    case OpcodeType::MOVB_LG: return MOVBLG<false>(args);
    case OpcodeType::MOVW_LG: return MOVWLG<false>(args);
    case OpcodeType::MOVL_LG: return MOVLLG<false>(args);
    case OpcodeType::MOVB_M: return MOVBM<false>(args);
    case OpcodeType::MOVW_M: return MOVWM<false>(args);
    case OpcodeType::MOVL_M: return MOVLM<false>(args);
    case OpcodeType::MOVB_P: return MOVBP<false>(args);
    case OpcodeType::MOVW_P: return MOVWP<false>(args);
    case OpcodeType::MOVL_P: return MOVLP<false>(args);
    case OpcodeType::MOVB_S: return MOVBS<false>(args);
    case OpcodeType::MOVW_S: return MOVWS<false>(args);
    case OpcodeType::MOVL_S: return MOVLS<false>(args);
    case OpcodeType::MOVB_S0: return MOVBS0<false>(args);
    case OpcodeType::MOVW_S0: return MOVWS0<false>(args);
    case OpcodeType::MOVL_S0: return MOVLS0<false>(args);
    case OpcodeType::MOVB_S4: return MOVBS4<false>(args);
    case OpcodeType::MOVW_S4: return MOVWS4<false>(args);
    case OpcodeType::MOVL_S4: return MOVLS4<false>(args);
    case OpcodeType::MOVB_SG: return MOVBSG<false>(args);
    case OpcodeType::MOVW_SG: return MOVWSG<false>(args);
    case OpcodeType::MOVL_SG: return MOVLSG<false>(args);
    case OpcodeType::MOV_I: return MOVI<false>(args);
    case OpcodeType::MOVW_I: return MOVWI<false>(args);
    case OpcodeType::MOVL_I: return MOVLI<false>(args);
    case OpcodeType::MOVA: return MOVA<false>(args);
    case OpcodeType::MOVT: return MOVT<false>(args);
    case OpcodeType::CLRT: return CLRT<false>();
    case OpcodeType::SETT: return SETT<false>();

    case OpcodeType::EXTUB: return EXTUB<false>(args);
    case OpcodeType::EXTUW: return EXTUW<false>(args);
    case OpcodeType::EXTSB: return EXTSB<false>(args);
    case OpcodeType::EXTSW: return EXTSW<false>(args);
    case OpcodeType::SWAPB: return SWAPB<false>(args);
    case OpcodeType::SWAPW: return SWAPW<false>(args);
    case OpcodeType::XTRCT: return XTRCT<false>(args);

    case OpcodeType::LDC_GBR_R: return LDCGBR<false>(args);
    case OpcodeType::LDC_SR_R: return LDCSR<false>(args);
    case OpcodeType::LDC_VBR_R: return LDCVBR<false>(args);
    case OpcodeType::LDS_MACH_R: return LDSMACH<false>(args);
    case OpcodeType::LDS_MACL_R: return LDSMACL<false>(args);
    case OpcodeType::LDS_PR_R: return LDSPR<false>(args);
    case OpcodeType::STC_GBR_R: return STCGBR<false>(args);
    case OpcodeType::STC_SR_R: return STCSR<false>(args);
    case OpcodeType::STC_VBR_R: return STCVBR<false>(args);
    case OpcodeType::STS_MACH_R: return STSMACH<false>(args);
    case OpcodeType::STS_MACL_R: return STSMACL<false>(args);
    case OpcodeType::STS_PR_R: return STSPR<false>(args);
    case OpcodeType::LDC_GBR_M: return LDCMGBR<false>(args);
    case OpcodeType::LDC_SR_M: return LDCMSR<false>(args);
    case OpcodeType::LDC_VBR_M: return LDCMVBR<false>(args);
    case OpcodeType::LDS_MACH_M: return LDSMMACH<false>(args);
    case OpcodeType::LDS_MACL_M: return LDSMMACL<false>(args);
    case OpcodeType::LDS_PR_M: return LDSMPR<false>(args);
    case OpcodeType::STC_GBR_M: return STCMGBR<false>(args);
    case OpcodeType::STC_SR_M: return STCMSR<false>(args);
    case OpcodeType::STC_VBR_M: return STCMVBR<false>(args);
    case OpcodeType::STS_MACH_M: return STSMMACH<false>(args);
    case OpcodeType::STS_MACL_M: return STSMMACL<false>(args);
    case OpcodeType::STS_PR_M: return STSMPR<false>(args);

    case OpcodeType::ADD: return ADD<false>(args);
    case OpcodeType::ADD_I: return ADDI<false>(args);
    case OpcodeType::ADDC: return ADDC<false>(args);
    case OpcodeType::ADDV: return ADDV<false>(args);
    case OpcodeType::AND_R: return AND<false>(args);
    case OpcodeType::AND_I: return ANDI<false>(args);
    case OpcodeType::AND_M: return ANDM<false>(args);
    case OpcodeType::NEG: return NEG<false>(args);
    case OpcodeType::NEGC: return NEGC<false>(args);
    case OpcodeType::NOT: return NOT<false>(args);
    case OpcodeType::OR_R: return OR<false>(args);
    case OpcodeType::OR_I: return ORI<false>(args);
    case OpcodeType::OR_M: return ORM<false>(args);
    case OpcodeType::ROTCL: return ROTCL<false>(args);
    case OpcodeType::ROTCR: return ROTCR<false>(args);
    case OpcodeType::ROTL: return ROTL<false>(args);
    case OpcodeType::ROTR: return ROTR<false>(args);
    case OpcodeType::SHAL: return SHAL<false>(args);
    case OpcodeType::SHAR: return SHAR<false>(args);
    case OpcodeType::SHLL: return SHLL<false>(args);
    case OpcodeType::SHLL2: return SHLL2<false>(args);
    case OpcodeType::SHLL8: return SHLL8<false>(args);
    case OpcodeType::SHLL16: return SHLL16<false>(args);
    case OpcodeType::SHLR: return SHLR<false>(args);
    case OpcodeType::SHLR2: return SHLR2<false>(args);
    case OpcodeType::SHLR8: return SHLR8<false>(args);
    case OpcodeType::SHLR16: return SHLR16<false>(args);
    case OpcodeType::SUB: return SUB<false>(args);
    case OpcodeType::SUBC: return SUBC<false>(args);
    case OpcodeType::SUBV: return SUBV<false>(args);
    case OpcodeType::XOR_R: return XOR<false>(args);
    case OpcodeType::XOR_I: return XORI<false>(args);
    case OpcodeType::XOR_M: return XORM<false>(args);

    case OpcodeType::CLRMAC: return CLRMAC<false>();
    case OpcodeType::MACW: return MACW<false>(args);
    case OpcodeType::MACL: return MACL<false>(args);
    case OpcodeType::MULS: return MULS<false>(args);
    case OpcodeType::MULU: return MULU<false>(args);

    case OpcodeType::DIV0S: return DIV0S<false>(args);
    case OpcodeType::DIV0U: return DIV0U<false>();
    case OpcodeType::DIV1: return DIV1<false>(args);

    case OpcodeType::CMP_EQ_I: return CMPIM<false>(args);
    case OpcodeType::CMP_EQ_R: return CMPEQ<false>(args);
    case OpcodeType::CMP_GE: return CMPGE<false>(args);
    case OpcodeType::CMP_GT: return CMPGT<false>(args);
    case OpcodeType::CMP_HI: return CMPHI<false>(args);
    case OpcodeType::CMP_HS: return CMPHS<false>(args);
    case OpcodeType::CMP_PL: return CMPPL<false>(args);
    case OpcodeType::CMP_PZ: return CMPPZ<false>(args);
    case OpcodeType::CMP_STR: return CMPSTR<false>(args);
    case OpcodeType::TAS: return TAS<false>(args);
    case OpcodeType::TST_R: return TST<false>(args);
    case OpcodeType::TST_I: return TSTI<false>(args);
    case OpcodeType::TST_M: return TSTM<false>(args);

    case OpcodeType::BF: return BF(args);
    case OpcodeType::BFS: return BFS(args);
    case OpcodeType::BT: return BT(args);
    case OpcodeType::BTS: return BTS(args);
    case OpcodeType::BRA: return BRA(args);
    case OpcodeType::BSR: return BSR(args);
    case OpcodeType::JMP: return JMP(args);
    case OpcodeType::JSR: return JSR(args);
    case OpcodeType::TRAPA: return TRAPA(args);

    case OpcodeType::RTE: return RTE();
    case OpcodeType::RTS: return RTS();

    case OpcodeType::Illegal: return EnterException(xvGenIllegalInstr);

    case OpcodeType::Delay_NOP: return NOP<true>();

    case OpcodeType::Delay_SLEEP: return SLEEP();

    case OpcodeType::Delay_MOV_R: return MOV<true>(args);
    case OpcodeType::Delay_MOVB_L: return MOVBL<true>(args);
    case OpcodeType::Delay_MOVW_L: return MOVWL<true>(args);
    case OpcodeType::Delay_MOVL_L: return MOVLL<true>(args);
    case OpcodeType::Delay_MOVB_L0: return MOVBL0<true>(args);
    case OpcodeType::Delay_MOVW_L0: return MOVWL0<true>(args);
    case OpcodeType::Delay_MOVL_L0: return MOVLL0<true>(args);
    case OpcodeType::Delay_MOVB_L4: return MOVBL4<true>(args);
    case OpcodeType::Delay_MOVW_L4: return MOVWL4<true>(args);
    case OpcodeType::Delay_MOVL_L4: return MOVLL4<true>(args);
    case OpcodeType::Delay_MOVB_LG: return MOVBLG<true>(args);
    case OpcodeType::Delay_MOVW_LG: return MOVWLG<true>(args);
    case OpcodeType::Delay_MOVL_LG: return MOVLLG<true>(args);
    case OpcodeType::Delay_MOVB_M: return MOVBM<true>(args);
    case OpcodeType::Delay_MOVW_M: return MOVWM<true>(args);
    case OpcodeType::Delay_MOVL_M: return MOVLM<true>(args);
    case OpcodeType::Delay_MOVB_P: return MOVBP<true>(args);
    case OpcodeType::Delay_MOVW_P: return MOVWP<true>(args);
    case OpcodeType::Delay_MOVL_P: return MOVLP<true>(args);
    case OpcodeType::Delay_MOVB_S: return MOVBS<true>(args);
    case OpcodeType::Delay_MOVW_S: return MOVWS<true>(args);
    case OpcodeType::Delay_MOVL_S: return MOVLS<true>(args);
    case OpcodeType::Delay_MOVB_S0: return MOVBS0<true>(args);
    case OpcodeType::Delay_MOVW_S0: return MOVWS0<true>(args);
    case OpcodeType::Delay_MOVL_S0: return MOVLS0<true>(args);
    case OpcodeType::Delay_MOVB_S4: return MOVBS4<true>(args);
    case OpcodeType::Delay_MOVW_S4: return MOVWS4<true>(args);
    case OpcodeType::Delay_MOVL_S4: return MOVLS4<true>(args);
    case OpcodeType::Delay_MOVB_SG: return MOVBSG<true>(args);
    case OpcodeType::Delay_MOVW_SG: return MOVWSG<true>(args);
    case OpcodeType::Delay_MOVL_SG: return MOVLSG<true>(args);
    case OpcodeType::Delay_MOV_I: return MOVI<true>(args);
    case OpcodeType::Delay_MOVW_I: return MOVWI<true>(args);
    case OpcodeType::Delay_MOVL_I: return MOVLI<true>(args);
    case OpcodeType::Delay_MOVA: return MOVA<true>(args);
    case OpcodeType::Delay_MOVT: return MOVT<true>(args);
    case OpcodeType::Delay_CLRT: return CLRT<true>();
    case OpcodeType::Delay_SETT: return SETT<true>();

    case OpcodeType::Delay_EXTUB: return EXTUB<true>(args);
    case OpcodeType::Delay_EXTUW: return EXTUW<true>(args);
    case OpcodeType::Delay_EXTSB: return EXTSB<true>(args);
    case OpcodeType::Delay_EXTSW: return EXTSW<true>(args);
    case OpcodeType::Delay_SWAPB: return SWAPB<true>(args);
    case OpcodeType::Delay_SWAPW: return SWAPW<true>(args);
    case OpcodeType::Delay_XTRCT: return XTRCT<true>(args);

    case OpcodeType::Delay_LDC_GBR_R: return LDCGBR<true>(args);
    case OpcodeType::Delay_LDC_SR_R: return LDCSR<true>(args);
    case OpcodeType::Delay_LDC_VBR_R: return LDCVBR<true>(args);
    case OpcodeType::Delay_LDS_MACH_R: return LDSMACH<true>(args);
    case OpcodeType::Delay_LDS_MACL_R: return LDSMACL<true>(args);
    case OpcodeType::Delay_LDS_PR_R: return LDSPR<true>(args);
    case OpcodeType::Delay_STC_GBR_R: return STCGBR<true>(args);
    case OpcodeType::Delay_STC_SR_R: return STCSR<true>(args);
    case OpcodeType::Delay_STC_VBR_R: return STCVBR<true>(args);
    case OpcodeType::Delay_STS_MACH_R: return STSMACH<true>(args);
    case OpcodeType::Delay_STS_MACL_R: return STSMACL<true>(args);
    case OpcodeType::Delay_STS_PR_R: return STSPR<true>(args);
    case OpcodeType::Delay_LDC_GBR_M: return LDCMGBR<true>(args);
    case OpcodeType::Delay_LDC_SR_M: return LDCMSR<true>(args);
    case OpcodeType::Delay_LDC_VBR_M: return LDCMVBR<true>(args);
    case OpcodeType::Delay_LDS_MACH_M: return LDSMMACH<true>(args);
    case OpcodeType::Delay_LDS_MACL_M: return LDSMMACL<true>(args);
    case OpcodeType::Delay_LDS_PR_M: return LDSMPR<true>(args);
    case OpcodeType::Delay_STC_GBR_M: return STCMGBR<true>(args);
    case OpcodeType::Delay_STC_SR_M: return STCMSR<true>(args);
    case OpcodeType::Delay_STC_VBR_M: return STCMVBR<true>(args);
    case OpcodeType::Delay_STS_MACH_M: return STSMMACH<true>(args);
    case OpcodeType::Delay_STS_MACL_M: return STSMMACL<true>(args);
    case OpcodeType::Delay_STS_PR_M: return STSMPR<true>(args);

    case OpcodeType::Delay_ADD: return ADD<true>(args);
    case OpcodeType::Delay_ADD_I: return ADDI<true>(args);
    case OpcodeType::Delay_ADDC: return ADDC<true>(args);
    case OpcodeType::Delay_ADDV: return ADDV<true>(args);
    case OpcodeType::Delay_AND_R: return AND<true>(args);
    case OpcodeType::Delay_AND_I: return ANDI<true>(args);
    case OpcodeType::Delay_AND_M: return ANDM<true>(args);
    case OpcodeType::Delay_NEG: return NEG<true>(args);
    case OpcodeType::Delay_NEGC: return NEGC<true>(args);
    case OpcodeType::Delay_NOT: return NOT<true>(args);
    case OpcodeType::Delay_OR_R: return OR<true>(args);
    case OpcodeType::Delay_OR_I: return ORI<true>(args);
    case OpcodeType::Delay_OR_M: return ORM<true>(args);
    case OpcodeType::Delay_ROTCL: return ROTCL<true>(args);
    case OpcodeType::Delay_ROTCR: return ROTCR<true>(args);
    case OpcodeType::Delay_ROTL: return ROTL<true>(args);
    case OpcodeType::Delay_ROTR: return ROTR<true>(args);
    case OpcodeType::Delay_SHAL: return SHAL<true>(args);
    case OpcodeType::Delay_SHAR: return SHAR<true>(args);
    case OpcodeType::Delay_SHLL: return SHLL<true>(args);
    case OpcodeType::Delay_SHLL2: return SHLL2<true>(args);
    case OpcodeType::Delay_SHLL8: return SHLL8<true>(args);
    case OpcodeType::Delay_SHLL16: return SHLL16<true>(args);
    case OpcodeType::Delay_SHLR: return SHLR<true>(args);
    case OpcodeType::Delay_SHLR2: return SHLR2<true>(args);
    case OpcodeType::Delay_SHLR8: return SHLR8<true>(args);
    case OpcodeType::Delay_SHLR16: return SHLR16<true>(args);
    case OpcodeType::Delay_SUB: return SUB<true>(args);
    case OpcodeType::Delay_SUBC: return SUBC<true>(args);
    case OpcodeType::Delay_SUBV: return SUBV<true>(args);
    case OpcodeType::Delay_XOR_R: return XOR<true>(args);
    case OpcodeType::Delay_XOR_I: return XORI<true>(args);
    case OpcodeType::Delay_XOR_M: return XORM<true>(args);

    case OpcodeType::Delay_CLRMAC: return CLRMAC<true>();
    case OpcodeType::Delay_MACW: return MACW<true>(args);
    case OpcodeType::Delay_MACL: return MACL<true>(args);
    case OpcodeType::Delay_MULS: return MULS<true>(args);
    case OpcodeType::Delay_MULU: return MULU<true>(args);

    case OpcodeType::Delay_DIV0S: return DIV0S<true>(args);
    case OpcodeType::Delay_DIV0U: return DIV0U<true>();
    case OpcodeType::Delay_DIV1: return DIV1<true>(args);

    case OpcodeType::Delay_CMP_EQ_I: return CMPIM<true>(args);
    case OpcodeType::Delay_CMP_EQ_R: return CMPEQ<true>(args);
    case OpcodeType::Delay_CMP_GE: return CMPGE<true>(args);
    case OpcodeType::Delay_CMP_GT: return CMPGT<true>(args);
    case OpcodeType::Delay_CMP_HI: return CMPHI<true>(args);
    case OpcodeType::Delay_CMP_HS: return CMPHS<true>(args);
    case OpcodeType::Delay_CMP_PL: return CMPPL<true>(args);
    case OpcodeType::Delay_CMP_PZ: return CMPPZ<true>(args);
    case OpcodeType::Delay_CMP_STR: return CMPSTR<true>(args);
    case OpcodeType::Delay_TAS: return TAS<true>(args);
    case OpcodeType::Delay_TST_R: return TST<true>(args);
    case OpcodeType::Delay_TST_I: return TSTI<true>(args);
    case OpcodeType::Delay_TST_M: return TSTM<true>(args);

    case OpcodeType::IllegalSlot: return EnterException(xvSlotIllegalInstr);
    }

    util::unreachable();
}

// nop
template <bool delaySlot>
FORCE_INLINE uint64 SH1::NOP() {
    AdvancePC<delaySlot>();
    return 1;
}

// sleep
FORCE_INLINE uint64 SH1::SLEEP() {
    if (!m_sleep) {
        if (SBYCR.SBY) {
            devlog::trace<grp::exec>("Entering standby");

            // Initialize on-chip registers
            DMAC.Reset();
            WDT.StandbyReset();
            ITU.Reset();
            SCI.Reset();
            AD.Reset();

            // TODO: enter standby state
        } else {
            devlog::trace<grp::exec>("Entering sleep");
            // TODO: enter sleep state
        }
        m_sleep = true;
    }

    return 3;
}

// mov Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOV(const DecodedArgs &args) {
    R[args.rn] = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// mov.b @Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBL(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = bit::sign_extend<8>(MemReadByte(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w @Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWL(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = bit::sign_extend<16>(MemReadWord(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l @Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLL(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = MemReadLong(address);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b @(R0,Rm), Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBL0(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + R[0];
    R[args.rn] = bit::sign_extend<8>(MemReadByte(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w @(R0,Rm), Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWL0(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + R[0];
    R[args.rn] = bit::sign_extend<16>(MemReadWord(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l @(R0,Rm), Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLL0(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + R[0];
    R[args.rn] = MemReadLong(address);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b @(disp,Rm), R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBL4(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + args.dispImm;
    R[0] = bit::sign_extend<8>(MemReadByte(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w @(disp,Rm), R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWL4(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + args.dispImm;
    R[0] = bit::sign_extend<16>(MemReadWord(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l @(disp,Rm), Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLL4(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + args.dispImm;
    R[args.rn] = MemReadLong(address);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b @(disp,GBR), R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBLG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    R[0] = bit::sign_extend<8>(MemReadByte(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w @(disp,GBR), R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWLG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    R[0] = bit::sign_extend<16>(MemReadWord(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l @(disp,GBR), R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLLG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    R[0] = MemReadLong(address);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b Rm, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBM(const DecodedArgs &args) {
    const uint32 address = R[args.rn] - 1;
    MemWriteByte(address, R[args.rm]);
    R[args.rn] -= 1;
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w Rm, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWM(const DecodedArgs &args) {
    const uint32 address = R[args.rn] - 2;
    MemWriteWord(address, R[args.rm]);
    R[args.rn] -= 2;
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l Rm, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLM(const DecodedArgs &args) {
    const uint32 address = R[args.rn] - 4;
    MemWriteLong(address, R[args.rm]);
    R[args.rn] -= 4;
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b @Rm+, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBP(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = bit::sign_extend<8>(MemReadByte(address));
    if (args.rn != args.rm) {
        R[args.rm] += 1;
    }
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w @Rm+, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWP(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = bit::sign_extend<16>(MemReadWord(address));
    if (args.rn != args.rm) {
        R[args.rm] += 2;
    }
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l @Rm+, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLP(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = MemReadLong(address);
    if (args.rn != args.rm) {
        R[args.rm] += 4;
    }
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b Rm, @Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBS(const DecodedArgs &args) {
    const uint32 address = R[args.rn];
    MemWriteByte(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w Rm, @Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWS(const DecodedArgs &args) {
    const uint32 address = R[args.rn];
    MemWriteWord(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l Rm, @Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLS(const DecodedArgs &args) {
    const uint32 address = R[args.rn];
    MemWriteLong(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b Rm, @(R0,Rn)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBS0(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + R[0];
    MemWriteByte(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w Rm, @(R0,Rn)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWS0(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + R[0];
    MemWriteWord(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l Rm, @(R0,Rn)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLS0(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + R[0];
    MemWriteLong(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b R0, @(disp,Rn)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBS4(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + args.dispImm;
    MemWriteByte(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w R0, @(disp,Rn)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWS4(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + args.dispImm;
    MemWriteWord(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l Rm, @(disp,Rn)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLS4(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + args.dispImm;
    MemWriteLong(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.b R0, @(disp,GBR)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVBSG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    MemWriteByte(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.w R0, @(disp,GBR)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWSG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    MemWriteWord(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l R0, @(disp,GBR)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLSG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    MemWriteLong(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov #imm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVI(const DecodedArgs &args) {
    R[args.rn] = args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// mov.w @(disp,PC), Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVWI(const DecodedArgs &args) {
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = pc + args.dispImm;
    R[args.rn] = bit::sign_extend<16>(MemReadWord(address));
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mov.l @(disp,PC), Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVLI(const DecodedArgs &args) {
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = (pc & ~3u) + args.dispImm;
    R[args.rn] = MemReadLong(address);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// mova @(disp,PC), R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVA(const DecodedArgs &args) {
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = (pc & ~3u) + args.dispImm;
    R[0] = address;
    AdvancePC<delaySlot>();
    return 1;
}

// movt Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MOVT(const DecodedArgs &args) {
    R[args.rn] = SR.T;
    AdvancePC<delaySlot>();
    return 1;
}

// clrt
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CLRT() {
    SR.T = 0;
    AdvancePC<delaySlot>();
    return 1;
}

// sett
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SETT() {
    SR.T = 1;
    AdvancePC<delaySlot>();
    return 1;
}

// exts.b Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::EXTSB(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<8>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// exts.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::EXTSW(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<16>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// extu.b Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::EXTUB(const DecodedArgs &args) {
    R[args.rn] = R[args.rm] & 0xFF;
    AdvancePC<delaySlot>();
    return 1;
}

// extu.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::EXTUW(const DecodedArgs &args) {
    R[args.rn] = R[args.rm] & 0xFFFF;
    AdvancePC<delaySlot>();
    return 1;
}

// swap.b Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SWAPB(const DecodedArgs &args) {
    const uint32 tmp0 = R[args.rm] & 0xFFFF0000;
    const uint32 tmp1 = (R[args.rm] & 0xFF) << 8u;
    R[args.rn] = ((R[args.rm] >> 8u) & 0xFF) | tmp1 | tmp0;
    AdvancePC<delaySlot>();
    return 1;
}

// swap.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SWAPW(const DecodedArgs &args) {
    const uint32 tmp = R[args.rm] >> 16u;
    R[args.rn] = (R[args.rm] << 16u) | tmp;
    AdvancePC<delaySlot>();
    return 1;
}

// xtrct Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::XTRCT(const DecodedArgs &args) {
    R[args.rn] = (R[args.rn] >> 16u) | (R[args.rm] << 16u);
    AdvancePC<delaySlot>();
    return 1;
}

// ldc Rm, GBR
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDCGBR(const DecodedArgs &args) {
    GBR = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// ldc Rm, SR
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDCSR(const DecodedArgs &args) {
    SR.u32 = R[args.rm] & 0x000003F3;
    m_intrPending = !delaySlot && INTC.pending.level > SR.ILevel;
    AdvancePC<delaySlot>();
    return 1;
}

// ldc Rm, VBR
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDCVBR(const DecodedArgs &args) {
    VBR = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// lds Rm, MACH
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDSMACH(const DecodedArgs &args) {
    MAC.H = bit::sign_extend<10>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// lds Rm, MACL
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDSMACL(const DecodedArgs &args) {
    MAC.L = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// lds Rm, PR
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDSPR(const DecodedArgs &args) {
    PR = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// stc GBR, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STCGBR(const DecodedArgs &args) {
    R[args.rn] = GBR;
    AdvancePC<delaySlot>();
    return 1;
}

// stc SR, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STCSR(const DecodedArgs &args) {
    R[args.rn] = SR.u32;
    AdvancePC<delaySlot>();
    return 1;
}

// stc VBR, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STCVBR(const DecodedArgs &args) {
    R[args.rn] = VBR;
    AdvancePC<delaySlot>();
    return 1;
}

// sts MACH, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STSMACH(const DecodedArgs &args) {
    R[args.rn] = MAC.H;
    AdvancePC<delaySlot>();
    return 1;
}

// sts MACL, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STSMACL(const DecodedArgs &args) {
    R[args.rn] = MAC.L;
    AdvancePC<delaySlot>();
    return 1;
}

// sts PR, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STSPR(const DecodedArgs &args) {
    R[args.rn] = PR;
    AdvancePC<delaySlot>();
    return 1;
}

// ldc.l @Rm+, GBR
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDCMGBR(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    GBR = MemReadLong(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles(address) + 2;
}

// ldc.l @Rm+, SR
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDCMSR(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    SR.u32 = MemReadLong(address) & 0x000003F3;
    m_intrPending = !delaySlot && INTC.pending.level > SR.ILevel;
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles(address) + 2;
}

// ldc.l @Rm+, VBR
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDCMVBR(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    VBR = MemReadLong(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles(address) + 2;
}

// lds.l @Rm+, MACH
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDSMMACH(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    MAC.H = bit::sign_extend<10>(MemReadLong(address));
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// lds.l @Rm+, MACL
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDSMMACL(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    MAC.L = MemReadLong(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// lds.l @Rm+, PR
template <bool delaySlot>
FORCE_INLINE uint64 SH1::LDSMPR(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    PR = MemReadLong(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// stc.l GBR, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STCMGBR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong(address, GBR);
    AdvancePC<delaySlot>();
    return AccessCycles(address) + 1;
}

// stc.l SR, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STCMSR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong(address, SR.u32);
    AdvancePC<delaySlot>();
    return AccessCycles(address) + 1;
}

// stc.l VBR, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STCMVBR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong(address, VBR);
    AdvancePC<delaySlot>();
    return AccessCycles(address) + 1;
}

// sts.l MACH, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STSMMACH(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong(address, MAC.H);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// sts.l MACL, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STSMMACL(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong(address, MAC.L);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// sts.l PR, @-Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::STSMPR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong(address, PR);
    AdvancePC<delaySlot>();
    return AccessCycles(address);
}

// add Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ADD(const DecodedArgs &args) {
    R[args.rn] += R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// add #imm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ADDI(const DecodedArgs &args) {
    R[args.rn] += args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// addc Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ADDC(const DecodedArgs &args) {
    const uint32 tmp1 = R[args.rn] + R[args.rm];
    const uint32 tmp0 = R[args.rn];
    R[args.rn] = tmp1 + SR.T;
    SR.T = (tmp0 > tmp1) || (tmp1 > R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// addv Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ADDV(const DecodedArgs &args) {
    const bool dst = static_cast<sint32>(R[args.rn]) < 0;
    const bool src = static_cast<sint32>(R[args.rm]) < 0;

    R[args.rn] += R[args.rm];

    bool ans = static_cast<sint32>(R[args.rn]) < 0;
    ans ^= dst;
    SR.T = (src == dst) & ans;

    AdvancePC<delaySlot>();
    return 1;
}

// and Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::AND(const DecodedArgs &args) {
    R[args.rn] &= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// and #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ANDI(const DecodedArgs &args) {
    R[0] &= args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// and.b #imm, @(R0,GBR)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ANDM(const DecodedArgs &args) {
    const uint32 address = GBR + R[0];
    uint8 tmp = MemReadByte(address);
    tmp &= args.dispImm;
    MemWriteByte(address, tmp);
    AdvancePC<delaySlot>();
    return AccessCycles(address) * 2 + 1;
}

// neg Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::NEG(const DecodedArgs &args) {
    R[args.rn] = 0u - R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// negc Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::NEGC(const DecodedArgs &args) {
    const uint32 tmp = 0u - R[args.rm];
    R[args.rn] = tmp - SR.T;
    SR.T = (0 < tmp) || (tmp < R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// not Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::NOT(const DecodedArgs &args) {
    R[args.rn] = ~R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// or Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::OR(const DecodedArgs &args) {
    R[args.rn] |= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// or #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ORI(const DecodedArgs &args) {
    R[0] |= args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// or.b #imm, @(R0,GBR)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ORM(const DecodedArgs &args) {
    const uint32 address = GBR + R[0];
    uint8 tmp = MemReadByte(address);
    tmp |= args.dispImm;
    MemWriteByte(address, tmp);
    AdvancePC<delaySlot>();
    return AccessCycles(address) * 2 + 1;
}

// rotcl Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ROTCL(const DecodedArgs &args) {
    const bool tmp = R[args.rn] >> 31u;
    R[args.rn] = (R[args.rn] << 1u) | SR.T;
    SR.T = tmp;
    AdvancePC<delaySlot>();
    return 1;
}

// rotcr Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ROTCR(const DecodedArgs &args) {
    const bool tmp = R[args.rn] & 1u;
    R[args.rn] = (R[args.rn] >> 1u) | (SR.T << 31u);
    SR.T = tmp;
    AdvancePC<delaySlot>();
    return 1;
}

// rotl Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ROTL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] = (R[args.rn] << 1u) | SR.T;
    AdvancePC<delaySlot>();
    return 1;
}

// rotr Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::ROTR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] = (R[args.rn] >> 1u) | (SR.T << 31u);
    AdvancePC<delaySlot>();
    return 1;
}

// shal Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHAL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] <<= 1u;
    AdvancePC<delaySlot>();
    return 1;
}

// shar Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHAR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] = static_cast<sint32>(R[args.rn]) >> 1;
    AdvancePC<delaySlot>();
    return 1;
}

// shll Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHLL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] <<= 1u;
    AdvancePC<delaySlot>();
    return 1;
}

// shll2 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHLL2(const DecodedArgs &args) {
    R[args.rn] <<= 2u;
    AdvancePC<delaySlot>();
    return 1;
}

// shll8 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHLL8(const DecodedArgs &args) {
    R[args.rn] <<= 8u;
    AdvancePC<delaySlot>();
    return 1;
}

// shll16 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHLL16(const DecodedArgs &args) {
    R[args.rn] <<= 16u;
    AdvancePC<delaySlot>();
    return 1;
}

// shlr Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHLR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] >>= 1u;
    AdvancePC<delaySlot>();
    return 1;
}

// shlr2 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHLR2(const DecodedArgs &args) {
    R[args.rn] >>= 2u;
    AdvancePC<delaySlot>();
    return 1;
}

// shlr8 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHLR8(const DecodedArgs &args) {
    R[args.rn] >>= 8u;
    AdvancePC<delaySlot>();
    return 1;
}

// shlr16 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SHLR16(const DecodedArgs &args) {
    R[args.rn] >>= 16u;
    AdvancePC<delaySlot>();
    return 1;
}

// sub Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SUB(const DecodedArgs &args) {
    R[args.rn] -= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// subc Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SUBC(const DecodedArgs &args) {
    const uint32 tmp1 = R[args.rn] - R[args.rm];
    const uint32 tmp0 = R[args.rn];
    R[args.rn] = tmp1 - SR.T;
    SR.T = (tmp0 < tmp1) || (tmp1 < R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// subv Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::SUBV(const DecodedArgs &args) {

    const bool dst = static_cast<sint32>(R[args.rn]) < 0;
    const bool src = static_cast<sint32>(R[args.rm]) < 0;

    R[args.rn] -= R[args.rm];

    bool ans = static_cast<sint32>(R[args.rn]) < 0;
    ans ^= dst;
    SR.T = (src != dst) & ans;

    AdvancePC<delaySlot>();
    return 1;
}

// xor Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::XOR(const DecodedArgs &args) {
    R[args.rn] ^= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// xor #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::XORI(const DecodedArgs &args) {
    R[0] ^= args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// xor.b #imm, @(R0,GBR)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::XORM(const DecodedArgs &args) {
    const uint32 address = GBR + R[0];
    uint8 tmp = MemReadByte(address);
    tmp ^= args.dispImm;
    MemWriteByte(address, tmp);
    AdvancePC<delaySlot>();
    return AccessCycles(address) * 2 + 1;
}

// clrmac
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CLRMAC() {
    MAC.u64 = 0;
    AdvancePC<delaySlot>();
    return 1;
}

// mac.w @Rm+, @Rn+
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MACW(const DecodedArgs &args) {
    const uint32 address2 = R[args.rn];
    const sint32 op2 = static_cast<sint16>(MemReadWord(address2));
    R[args.rn] += 2;
    const uint32 address1 = R[args.rm];
    const sint32 op1 = static_cast<sint16>(MemReadWord(address1));
    R[args.rm] += 2;

    const sint32 mul = op1 * op2;
    if (SR.S) {
        const sint64 result = static_cast<sint64>(static_cast<sint32>(MAC.L)) + mul;
        const sint32 saturatedResult = std::clamp<sint64>(result, -0x80000000LL, 0x7FFFFFFFLL);
        if (result == saturatedResult) {
            MAC.L = result;
        } else {
            MAC.L = saturatedResult;
            MAC.H |= 1;
        }
    } else {
        MAC.u64 = bit::sign_extend<42>(MAC.u64 + mul);
    }

    AdvancePC<delaySlot>();
    return AccessCycles(address1) + AccessCycles(address2);
}

// mac.l @Rm+, @Rn+
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MACL(const DecodedArgs &args) {
    const uint32 address2 = R[args.rn];
    const sint64 op2 = static_cast<sint64>(static_cast<sint32>(MemReadLong(address2)));
    R[args.rn] += 4;
    const uint32 address1 = R[args.rm];
    const sint64 op1 = static_cast<sint64>(static_cast<sint32>(MemReadLong(address1)));
    R[args.rm] += 4;

    const sint64 mul = op1 * op2;
    sint64 result = mul + MAC.u64;
    if (SR.S && result > 0x000003FFFFFFFFFFull && result < 0xFFFFFC0000000000ull) {
        if (static_cast<sint32>(op1 ^ op2) < 0) {
            result = 0xFFFFFC0000000000ull;
        } else {
            result = 0x000003FFFFFFFFFFull;
        }
    }
    MAC.u64 = bit::sign_extend<42>(result);

    AdvancePC<delaySlot>();
    return AccessCycles(address1) + AccessCycles(address2);
}

// muls.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MULS(const DecodedArgs &args) {
    MAC.L = bit::sign_extend<16>(R[args.rm]) * bit::sign_extend<16>(R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// mulu.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::MULU(const DecodedArgs &args) {
    auto cast = [](uint32 val) { return static_cast<uint32>(static_cast<uint16>(val)); };
    MAC.L = cast(R[args.rm]) * cast(R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// div0s r{}, Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::DIV0S(const DecodedArgs &args) {
    SR.M = static_cast<sint32>(R[args.rm]) < 0;
    SR.Q = static_cast<sint32>(R[args.rn]) < 0;
    SR.T = SR.M != SR.Q;
    AdvancePC<delaySlot>();
    return 1;
}

// div0u
template <bool delaySlot>
FORCE_INLINE uint64 SH1::DIV0U() {
    SR.M = 0;
    SR.Q = 0;
    SR.T = 0;
    AdvancePC<delaySlot>();
    return 1;
}

// div1 Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::DIV1(const DecodedArgs &args) {
    const bool oldQ = SR.Q;
    SR.Q = static_cast<sint32>(R[args.rn]) < 0;
    R[args.rn] = (R[args.rn] << 1u) | SR.T;

    const uint32 prevVal = R[args.rn];
    if (oldQ == SR.M) {
        R[args.rn] -= R[args.rm];
    } else {
        R[args.rn] += R[args.rm];
    }

    if (oldQ) {
        if (SR.M) {
            SR.Q ^= R[args.rn] <= prevVal;
        } else {
            SR.Q ^= R[args.rn] < prevVal;
        }
    } else {
        if (SR.M) {
            SR.Q ^= R[args.rn] >= prevVal;
        } else {
            SR.Q ^= R[args.rn] > prevVal;
        }
    }

    SR.T = SR.Q == SR.M;

    AdvancePC<delaySlot>();
    return 1;
}

// cmp/eq #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPIM(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[0]) == args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/eq Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPEQ(const DecodedArgs &args) {
    SR.T = R[args.rn] == R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/ge Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPGE(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) >= static_cast<sint32>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/gt Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPGT(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) > static_cast<sint32>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/hi Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPHI(const DecodedArgs &args) {
    SR.T = R[args.rn] > R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/hs Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPHS(const DecodedArgs &args) {
    SR.T = R[args.rn] >= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/pl Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPPL(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) > 0;
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/pz Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPPZ(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) >= 0;
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/str Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::CMPSTR(const DecodedArgs &args) {
    const uint32 tmp = R[args.rm] ^ R[args.rn];
    const uint8 hh = tmp >> 24u;
    const uint8 hl = tmp >> 16u;
    const uint8 lh = tmp >> 8u;
    const uint8 ll = tmp >> 0u;
    SR.T = !(hh && hl && lh && ll);
    AdvancePC<delaySlot>();
    return 1;
}

// tas.b @Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::TAS(const DecodedArgs &args) {
    const uint32 address = R[args.rn];
    // TODO: enable bus lock on this read
    const uint8 tmp = MemReadByte(address);
    SR.T = tmp == 0;
    // TODO: disable bus lock on this write
    MemWriteByte(address, tmp | 0x80);

    AdvancePC<delaySlot>();
    return AccessCycles(address) + AccessCycles(address) + 2;
}

// tst Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH1::TST(const DecodedArgs &args) {
    SR.T = (R[args.rn] & R[args.rm]) == 0;
    AdvancePC<delaySlot>();
    return 1;
}

// tst #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH1::TSTI(const DecodedArgs &args) {
    SR.T = (R[0] & args.dispImm) == 0;
    AdvancePC<delaySlot>();
    return 1;
}

// tst.b #imm, @(R0,GBR)
template <bool delaySlot>
FORCE_INLINE uint64 SH1::TSTM(const DecodedArgs &args) {
    const uint32 address = GBR + R[0];
    const uint8 tmp = MemReadByte(address);
    SR.T = (tmp & args.dispImm) == 0;
    AdvancePC<delaySlot>();
    return AccessCycles(address) + 2;
}

// bf <label>
FORCE_INLINE uint64 SH1::BF(const DecodedArgs &args) {
    if (!SR.T) {
        PC += args.dispImm;
        return 3;
    } else {
        PC += 2;
        return 1;
    }
}

// bf/s <label>
FORCE_INLINE uint64 SH1::BFS(const DecodedArgs &args) {
    if (!SR.T) {
        SetupDelaySlot(PC + args.dispImm);
    }
    PC += 2;
    return !SR.T ? 2 : 1;
}

// bt <label>
FORCE_INLINE uint64 SH1::BT(const DecodedArgs &args) {
    if (SR.T) {
        PC += args.dispImm;
        return 3;
    } else {
        PC += 2;
        return 1;
    }
}

// bt/s <label>
FORCE_INLINE uint64 SH1::BTS(const DecodedArgs &args) {
    if (SR.T) {
        SetupDelaySlot(PC + args.dispImm);
    }
    PC += 2;
    return SR.T ? 2 : 1;
}

// bra <label>
FORCE_INLINE uint64 SH1::BRA(const DecodedArgs &args) {
    SetupDelaySlot(PC + args.dispImm);
    PC += 2;
    return 2;
}

// bsr <label>
FORCE_INLINE uint64 SH1::BSR(const DecodedArgs &args) {
    PR = PC + 4;
    SetupDelaySlot(PC + args.dispImm);
    PC += 2;
    return 2;
}

// jmp @Rm
FORCE_INLINE uint64 SH1::JMP(const DecodedArgs &args) {
    SetupDelaySlot(R[args.rm]);
    PC += 2;
    return 2;
}

// jsr @Rm
FORCE_INLINE uint64 SH1::JSR(const DecodedArgs &args) {
    PR = PC + 4;
    SetupDelaySlot(R[args.rm]);
    PC += 2;
    return 2;
}

// trapa #imm
FORCE_INLINE uint64 SH1::TRAPA(const DecodedArgs &args) {
    devlog::trace<grp::intr>("[PC = {:08X}] Handling TRAPA, vector number {:02X}", PC, args.dispImm >> 2u);
    const uint32 address1 = R[15] - 4;
    const uint32 address2 = R[15] - 8;
    const uint32 address3 = VBR + args.dispImm;
    MemWriteLong(address1, SR.u32);
    MemWriteLong(address2, PC + 2);
    PC = MemReadLong(address3);
    R[15] -= 8;
    devlog::trace<grp::intr>("[PC = {:08X}] Entering TRAPA handler", PC);
    return AccessCycles(address1) + AccessCycles(address2) + AccessCycles(address3) + 5;
}

FORCE_INLINE uint64 SH1::RTE() {
    const uint32 address1 = R[15];
    const uint32 address2 = R[15] + 4;
    // rte
    SetupDelaySlot(MemReadLong(address1));
    SR.u32 = MemReadLong(address2) & 0x000003F3;
    PC += 2;
    R[15] += 8;
    devlog::trace<grp::intr>("[PC = {:08X}] Returning from exception handler, PC -> {:08X}", PC, m_delaySlotTarget);
    return AccessCycles(address1) + AccessCycles(address2) + 2;
}

// rts
FORCE_INLINE uint64 SH1::RTS() {
    SetupDelaySlot(PR);
    PC += 2;
    return 2;
}

} // namespace ymir::sh1
