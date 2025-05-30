#include <ymir/hw/sh2/sh2.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/unreachable.hpp>

#include <algorithm>
#include <cassert>
#include <string>
#include <string_view>

namespace ymir::sh2 {

// -----------------------------------------------------------------------------
// Dev log groups

namespace grp {

    // Hierarchy:
    //
    // base
    //   exec
    //     exec_dump
    //   mem
    //   reg
    //   code_fetch
    //   cache
    //   dma
    //     dma_xfer

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix);
        }
    };

    struct exec : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Exec";
        }
    };

    struct exec_dump : public exec {
        static constexpr bool enabled = false;
    };

    struct mem : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Mem";
        }
    };

    struct reg : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Reg";
        }
    };

    struct code_fetch : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-CodeFetch";
        }
    };

    struct cache : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Cache";
        }
    };

    struct dma : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-DMA";
        }
    };

    struct dma_xfer : public dma {
        // static constexpr bool enabled = true;
    };

} // namespace grp

// -----------------------------------------------------------------------------
// Configuration

namespace config {
    // Address of SYS_EXECDMP function.
    // 0x186C is valid in most BIOS images.
    // 0x197C on JP (v1.003).
    inline constexpr uint32 sysExecDumpAddress = 0x186C;
} // namespace config

// -----------------------------------------------------------------------------
// Debugger

template <bool debug>
FORCE_INLINE static void TraceExecuteInstruction(debug::ISH2Tracer *tracer, uint32 pc, uint16 opcode, bool delaySlot) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->ExecuteInstruction(pc, opcode, delaySlot);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceInterrupt(debug::ISH2Tracer *tracer, uint8 vecNum, uint8 level,
                                        sh2::InterruptSource source, uint32 pc) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Interrupt(vecNum, level, source, pc);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceException(debug::ISH2Tracer *tracer, uint8 vecNum, uint32 pc, uint32 sr) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Exception(vecNum, pc, sr);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceBegin32x32Division(debug::ISH2Tracer *tracer, sint32 dividend, sint32 divisor,
                                                 bool overflowIntrEnable) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Begin32x32Division(dividend, divisor, overflowIntrEnable);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceBegin64x32Division(debug::ISH2Tracer *tracer, sint64 dividend, sint32 divisor,
                                                 bool overflowIntrEnable) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->Begin64x32Division(dividend, divisor, overflowIntrEnable);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceEndDivision(debug::ISH2Tracer *tracer, sint32 quotient, sint32 remainder, bool overflow) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->EndDivision(quotient, remainder, overflow);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceDMAXferBegin(debug::ISH2Tracer *tracer, uint32 channel, uint32 srcAddress,
                                           uint32 dstAddress, uint32 count, uint32 unitSize, sint32 srcInc,
                                           sint32 dstInc) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->DMAXferBegin(channel, srcAddress, dstAddress, count, unitSize, srcInc, dstInc);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceDMAXferData(debug::ISH2Tracer *tracer, uint32 channel, uint32 srcAddress,
                                          uint32 dstAddress, uint32 data, uint32 unitSize) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->DMAXferData(channel, srcAddress, dstAddress, data, unitSize);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceDMAXferEnd(debug::ISH2Tracer *tracer, uint32 channel, bool irqRaised) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->DMAXferEnd(channel, irqRaised);
        }
    }
}

// -----------------------------------------------------------------------------
// Implementation

SH2::SH2(sys::Bus &bus, bool master, const sys::SystemFeatures &systemFeatures)
    : m_bus(bus)
    , m_systemFeatures(systemFeatures)
    , m_logPrefix(master ? "SH2-M" : "SH2-S") {

    BCR1.MASTER = !master;
    Reset(true);
}

void SH2::Reset(bool hard, bool watchdogInitiated) {
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
    // - BSC, USB and FMR are not reset on power-on/hard reset
    // - all other modules reset always

    R.fill(0);
    PR = 0;

    MAC.u64 = 0;

    SR.u32 = 0;
    SR.ILevel = 0xF;
    GBR = 0;
    VBR = 0x00000000;

    // NOTE: Cache is disabled on reset
    PC = MemReadLong<false>(0x00000000);
    R[15] = MemReadLong<false>(0x00000004);
    FillPipeline<false>();

    // On-chip registers
    BCR1.u15 = 0x03F0;
    BCR2.u16 = 0x00FC;
    WCR.u16 = 0xAAFF;
    MCR.u16 = 0x0000;
    RTCSR.u16 = 0x0000;
    RTCNT = 0x0000;
    RTCOR = 0x0000;

    DMAOR.Reset();
    for (auto &ch : m_dmaChannels) {
        ch.Reset();
    }

    WDT.Reset(watchdogInitiated);

    SBYCR.u8 = 0x00;

    DIVU.Reset();
    FRT.Reset();
    INTC.Reset();

    m_delaySlot = false;

    m_cache.Reset();
}

void SH2::MapMemory(sys::Bus &bus) {
    const uint32 addressOffset = !BCR1.MASTER * 0x80'0000;
    bus.MapNormal(
        0x100'0000 + addressOffset, 0x17F'FFFF + addressOffset, this,
        [](uint32 address, uint8, void *ctx) {
            if (address & 1) {
                static_cast<SH2 *>(ctx)->TriggerFRTInputCapture();
            }
        },
        [](uint32 address, uint16, void *ctx) { static_cast<SH2 *>(ctx)->TriggerFRTInputCapture(); },
        [](uint32 address, uint32, void *ctx) { static_cast<SH2 *>(ctx)->TriggerFRTInputCapture(); });
}

void SH2::DumpCacheData(std::ostream &out) const {
    for (uint32 addr = 0; addr < 4096; addr += 4) {
        const uint32 value = m_cache.ReadDataArray<uint32>(addr);
        out.write((const char *)&value, sizeof(value));
    }
}

void SH2::DumpCacheAddressTag(std::ostream &out) const {
    for (uint32 addr = 0; addr < 1024; addr += 4) {
        const uint32 value = m_cache.ReadAddressArray<true>(addr);
        out.write((const char *)&value, sizeof(value));
    }
}

template <bool debug, bool enableCache>
FLATTEN uint64 SH2::Advance(uint64 cycles) {
    uint64 cyclesExecuted = 0;
    while (cyclesExecuted < cycles) {
        uint64 deadline = cycles;

        if (WDT.WTCSR.TME) {
            deadline = std::min(deadline, cyclesExecuted + WDT.CyclesUntilNextTick());
        }
        // TODO: skip FRT updates if interrupt disabled
        // - update on reads
        // - needs to keep track of global cycle count to update properly
        deadline = std::min(deadline, cyclesExecuted + FRT.CyclesUntilNextTick());

        const uint64 prevCyclesExecuted = cyclesExecuted;
        while (cyclesExecuted <= deadline) {
            // TODO: choose between interpreter (cached or uncached) and JIT recompiler
            cyclesExecuted += InterpretNext<debug, enableCache>();

            // If PC is in any of these places, something went horribly wrong
            /*if ((PC & 1) || ((PC >> 29u) != 0b000 && (PC >> 29u) != 0b001 && (PC >> 29u) != 0b101)) {
                __debugbreak();
            }*/

            if constexpr (devlog::debug_enabled<grp::exec_dump>) {
                // Dump stack trace on SYS_EXECDMP
                if ((PC & 0x7FFFFFF) == config::sysExecDumpAddress) {
                    devlog::debug<grp::exec_dump>(m_logPrefix, "SYS_EXECDMP triggered");
                    // TODO: trace event
                }
            }
        }
        const uint64 cyclesExecutedNow = cyclesExecuted - prevCyclesExecuted;
        AdvanceWDT(cyclesExecutedNow);
        AdvanceFRT(cyclesExecutedNow);
    }
    return cyclesExecuted;
}

template uint64 SH2::Advance<false, false>(uint64 cycles);
template uint64 SH2::Advance<false, true>(uint64 cycles);
template uint64 SH2::Advance<true, false>(uint64 cycles);
template uint64 SH2::Advance<true, true>(uint64 cycles);

template <bool debug, bool enableCache>
FLATTEN uint64 SH2::Step() {
    uint64 cyclesExecuted = InterpretNext<debug, enableCache>();
    AdvanceWDT(cyclesExecuted);
    AdvanceFRT(cyclesExecuted);
    return cyclesExecuted;
}

template uint64 SH2::Step<false, false>();
template uint64 SH2::Step<false, true>();
template uint64 SH2::Step<true, false>();
template uint64 SH2::Step<true, true>();

bool SH2::GetNMI() const {
    return INTC.ICR.NMIL;
}

void SH2::SetNMI() {
    // HACK: should be edge-detected
    INTC.ICR.NMIL = 1;
    INTC.NMI = true;
    RaiseInterrupt(InterruptSource::NMI);
}

void SH2::PurgeCache() {
    m_cache.Purge();
}

// -----------------------------------------------------------------------------
// Save states

void SH2::SaveState(state::SH2State &state) const {
    state.R = R;
    state.PC = PC;
    state.PR = PR;
    state.MACL = MAC.L;
    state.MACH = MAC.H;
    state.SR = SR.u32;
    state.GBR = GBR;
    state.VBR = VBR;
    state.delaySlot = m_delaySlot;
    state.fetchQueue = m_fetchQueue;

    state.bsc.BCR1 = BCR1.u16;
    state.bsc.BCR2 = BCR2.u16;
    state.bsc.WCR = WCR.u16;
    state.bsc.MCR = MCR.u16;
    state.bsc.RTCSR = RTCSR.u16;
    state.bsc.RTCNT = RTCNT;
    state.bsc.RTCOR = RTCOR;

    state.dmac.DMAOR = DMAOR.Read();
    m_dmaChannels[0].SaveState(state.dmac.channels[0]);
    m_dmaChannels[1].SaveState(state.dmac.channels[1]);
    WDT.SaveState(state.wdt);
    DIVU.SaveState(state.divu);
    FRT.SaveState(state.frt);
    INTC.SaveState(state.intc);
    m_cache.SaveState(state.cache);
    state.SBYCR = SBYCR.u8;
}

bool SH2::ValidateState(const state::SH2State &state) const {
    return true;
}

void SH2::LoadState(const state::SH2State &state) {
    R = state.R;
    PC = state.PC;
    PR = state.PR;
    MAC.L = state.MACL;
    MAC.H = state.MACH;
    SR.u32 = state.SR;
    GBR = state.GBR;
    VBR = state.VBR;
    m_delaySlot = state.delaySlot;
    m_fetchQueue = state.fetchQueue;

    BCR1.u15 = state.bsc.BCR1; // Do not change the MASTER bit
    BCR2.u16 = state.bsc.BCR2;
    WCR.u16 = state.bsc.WCR;
    MCR.u16 = state.bsc.MCR;
    RTCSR.u16 = state.bsc.RTCSR;
    RTCNT = state.bsc.RTCNT;
    RTCOR = state.bsc.RTCOR;

    DMAOR.Write<true>(state.dmac.DMAOR);
    m_dmaChannels[0].LoadState(state.dmac.channels[0]);
    m_dmaChannels[1].LoadState(state.dmac.channels[1]);
    WDT.LoadState(state.wdt);
    DIVU.LoadState(state.divu);
    FRT.LoadState(state.frt);
    INTC.LoadState(state.intc);
    m_cache.LoadState(state.cache);
    SBYCR.u8 = state.SBYCR;
}

// -----------------------------------------------------------------------------
// Memory accessors

template <mem_primitive T, bool instrFetch, bool peek, bool enableCache>
T SH2::MemRead(uint32 address) {
    const uint32 partition = (address >> 29u) & 0b111;
    if (address & static_cast<uint32>(sizeof(T) - 1)) {
        if constexpr (!peek) {
            devlog::trace<grp::mem>(m_logPrefix, "WARNING: misaligned {}-bit read from {:08X}", sizeof(T) * 8, address);
            // TODO: raise CPU address error due to misaligned access
            // - might have to store data in a class member instead of returning
        }
        address &= ~(sizeof(T) - 1);
    }

    switch (partition) {
    case 0b000: // cache
        if constexpr (enableCache) {
            if (m_cache.CCR.CE) {
                CacheEntry &entry = m_cache.GetEntry(address);
                uint32 way = entry.FindWay(address);

                if constexpr (!peek) {
                    if (!IsValidCacheWay(way)) {
                        // Cache miss
                        way = m_cache.SelectWay<instrFetch>(address);
                        if (IsValidCacheWay(way)) {
                            // Fill line
                            const uint32 baseAddress = address & ~0xF;
                            for (uint32 offset = 0; offset < 16; offset += 4) {
                                const uint32 addressInc = (address + 4 + offset) & 0xC;
                                const uint32 memValue = m_bus.Read<uint32>((baseAddress + addressInc) & 0x7FFFFFF);
                                util::WriteBE<uint32>(&entry.line[way][addressInc], memValue);
                            }
                        }
                    }
                }

                // If way is valid, fetch from cache
                if (IsValidCacheWay(way)) {
                    const uint32 byte = bit::extract<0, 3>(address);
                    const T value = util::ReadBE<T>(&entry.line[way][byte]);
                    if constexpr (!peek) {
                        m_cache.UpdateLRU(address, way);
                        devlog::trace<grp::cache>(m_logPrefix, "{}-bit SH-2 cached area read from {:08X} = {:X} (hit)",
                                                  sizeof(T) * 8, address, value);
                    }
                    return value;
                }
                if constexpr (!peek) {
                    devlog::trace<grp::cache>(m_logPrefix, "{}-bit SH-2 cached area read from {:08X} (miss)",
                                              sizeof(T) * 8, address);
                }
            }
        }
        [[fallthrough]];
    case 0b001:
    case 0b101: // cache-through
        if constexpr (peek) {
            return m_bus.Peek<T>(address & 0x7FFFFFF);
        } else {
            return m_bus.Read<T>(address & 0x7FFFFFF);
        }
    case 0b010: // associative purge
        if constexpr (!peek && std::is_same_v<T, uint32>) {
            m_cache.AssociativePurge(address);
            devlog::trace<grp::cache>(m_logPrefix, "{}-bit SH-2 associative purge read from {:08X}", sizeof(T) * 8,
                                      address);
        }
        return (address & 1) ? static_cast<T>(0x12231223) : static_cast<T>(0x23122312);
    case 0b011: // cache address array
        if constexpr (peek || std::is_same_v<T, uint32>) {
            const uint32 value = m_cache.ReadAddressArray<peek>(address);
            if constexpr (!peek) {
                devlog::trace<grp::cache>(m_logPrefix, "{}-bit SH-2 cache address array read from {:08X} = {:X}",
                                          sizeof(T) * 8, address, value);
            }
            if constexpr (std::is_same_v<T, uint32>) {
                return value;
            } else {
                return value >> ((~address & 3u) * 8u);
            }
        } else {
            return 0;
        }
    case 0b100:
    case 0b110: // cache data array
    {
        const T value = m_cache.ReadDataArray<T>(address);
        if constexpr (!peek) {
            devlog::trace<grp::cache>(m_logPrefix, "{}-bit SH-2 cache data array read from {:08X} = {:X}",
                                      sizeof(T) * 8, address, value);
        }
        return value;
    }
    case 0b111: // I/O area
        if constexpr (instrFetch) {
            if constexpr (!peek) {
                // TODO: raise CPU address error due to attempt to fetch instruction from I/O area
                devlog::trace<grp::code_fetch>(m_logPrefix, "Attempted to fetch instruction from I/O area at {:08X}",
                                               address);
            }
            return 0;
        } else if ((address & 0xE0004000) == 0xE0004000) {
            // bits 31-29 and 14 must be set
            // bits 8-0 index the register
            // bits 28 and 12 must be both set to access the lower half of the registers
            if ((address & 0x100) || (address & 0x10001000) == 0x10001000) {
                return OnChipRegRead<T, peek>(address & 0x1FF);
            } else {
                return OpenBusSeqRead<T>(address);
            }
        } else {
            // TODO: implement
            if constexpr (!peek) {
                devlog::trace<grp::mem>(m_logPrefix, "Unhandled {}-bit SH-2 I/O area read from {:08X}", sizeof(T) * 8,
                                        address);
            }
            return 0;
        }
    }

    util::unreachable();
}

template <mem_primitive T, bool poke, bool debug, bool enableCache>
void SH2::MemWrite(uint32 address, T value) {
    const uint32 partition = address >> 29u;
    if (address & static_cast<uint32>(sizeof(T) - 1)) {
        if constexpr (!poke) {
            devlog::trace<grp::mem>(m_logPrefix, "WARNING: misaligned {}-bit write to {:08X} = {:X}", sizeof(T) * 8,
                                    address, value);
            // TODO: address error (misaligned access)
        }
        address &= ~(sizeof(T) - 1);
    }

    switch (partition) {
    case 0b000: // cache
        if constexpr (enableCache) {
            if (m_cache.CCR.CE) {
                auto &entry = m_cache.GetEntry(address);
                const uint8 way = entry.FindWay(address);
                if (IsValidCacheWay(way)) {
                    const uint32 byte = bit::extract<0, 3>(address);
                    util::WriteBE<T>(&entry.line[way][byte], value);
                    if constexpr (!poke) {
                        m_cache.UpdateLRU(address, way);
                    }
                }
            }
        }
        [[fallthrough]];
    case 0b001:
    case 0b101: // cache-through
        if constexpr (poke) {
            m_bus.Poke<T>(address & 0x7FFFFFF, value);
        } else {
            m_bus.Write<T>(address & 0x7FFFFFF, value);
        }
        break;
    case 0b010: // associative purge
        if constexpr (poke || std::is_same_v<T, uint32>) {
            m_cache.AssociativePurge(address);
            if constexpr (!poke) {
                devlog::trace<grp::cache>(m_logPrefix, "{}-bit SH-2 associative purge write to {:08X} = {:X}",
                                          sizeof(T) * 8, address, value);
            }
        }
        break;
    case 0b011: // cache address array
        if constexpr (poke || std::is_same_v<T, uint32>) {
            m_cache.WriteAddressArray<T, poke>(address, value);
            if constexpr (!poke) {
                devlog::trace<grp::cache>(m_logPrefix, "{}-bit SH-2 cache address array write to {:08X} = {:X}",
                                          sizeof(T) * 8, address, value);
            }
        }
        break;
    case 0b100:
    case 0b110: // cache data array
    {
        m_cache.WriteDataArray<T>(address, value);
        if constexpr (!poke) {
            devlog::trace<grp::cache>(m_logPrefix, "{}-bit SH-2 cache data array write to {:08X} = {:X}", sizeof(T) * 8,
                                      address, value);
        }
        break;
    }
    case 0b111: // I/O area
        if ((address & 0xE0004000) == 0xE0004000) {
            // bits 31-29 and 14 must be set
            // bits 8-0 index the register
            // bits 28 and 12 must be both set to access the lower half of the registers
            if ((address & 0x100) || (address & 0x10001000) == 0x10001000) {
                OnChipRegWrite<T, poke, debug, enableCache>(address & 0x1FF, value);
            }
        } else if ((address >> 12u) == 0xFFFF8) {
            // DRAM setup stuff
            if constexpr (!poke) {
                switch (address) {
                case 0xFFFF8426: devlog::trace<grp::reg>(m_logPrefix, "16-bit CAS latency 1"); break;
                case 0xFFFF8446: devlog::trace<grp::reg>(m_logPrefix, "16-bit CAS latency 2"); break;
                case 0xFFFF8466: devlog::trace<grp::reg>(m_logPrefix, "16-bit CAS latency 3"); break;
                case 0xFFFF8848: devlog::trace<grp::reg>(m_logPrefix, "32-bit CAS latency 1"); break;
                case 0xFFFF8888: devlog::trace<grp::reg>(m_logPrefix, "32-bit CAS latency 2"); break;
                case 0xFFFF88C8: devlog::trace<grp::reg>(m_logPrefix, "32-bit CAS latency 3"); break;
                default:
                    devlog::debug<grp::reg>(m_logPrefix, "Unhandled {}-bit SH-2 I/O area write to {:08X} = {:X}",
                                            sizeof(T) * 8, address, value);
                    break;
                }
            }
        } else {
            // TODO: implement
            if constexpr (!poke) {
                devlog::trace<grp::reg>(m_logPrefix, "Unhandled {}-bit SH-2 I/O area write to {:08X} = {:X}",
                                        sizeof(T) * 8, address, value);
            }
        }
        break;
    }
}

template <bool enableCache>
FLATTEN FORCE_INLINE void SH2::FillPipeline() {
    m_fetchQueue[0] = MemRead<uint16, true, false, enableCache>(PC + 0);
    m_fetchQueue[1] = MemRead<uint16, true, false, enableCache>(PC + 2);
    PC += 4;
}

template <bool enableCache>
FLATTEN FORCE_INLINE uint16 SH2::FetchInstruction(uint32 address) {
    const uint16 opcode = m_fetchQueue[0];
    m_fetchQueue[0] = m_fetchQueue[1];
    m_fetchQueue[1] = MemRead<uint16, true, false, enableCache>(address);
    return opcode;
}

template <bool enableCache>
FLATTEN FORCE_INLINE uint8 SH2::MemReadByte(uint32 address) {
    return MemRead<uint8, false, false, enableCache>(address);
}

template <bool enableCache>
FLATTEN FORCE_INLINE uint16 SH2::MemReadWord(uint32 address) {
    return MemRead<uint16, false, false, enableCache>(address);
}

template <bool enableCache>
FLATTEN FORCE_INLINE uint32 SH2::MemReadLong(uint32 address) {
    return MemRead<uint32, false, false, enableCache>(address);
}

template <bool debug, bool enableCache>
FLATTEN FORCE_INLINE void SH2::MemWriteByte(uint32 address, uint8 value) {
    MemWrite<uint8, false, debug, enableCache>(address, value);
}

template <bool debug, bool enableCache>
FLATTEN FORCE_INLINE void SH2::MemWriteWord(uint32 address, uint16 value) {
    MemWrite<uint16, false, debug, enableCache>(address, value);
}

template <bool debug, bool enableCache>
FLATTEN FORCE_INLINE void SH2::MemWriteLong(uint32 address, uint32 value) {
    MemWrite<uint32, false, debug, enableCache>(address, value);
}

template <bool enableCache>
FLATTEN FORCE_INLINE uint16 SH2::PeekInstruction(uint32 address) {
    return MemRead<uint16, true, true, enableCache>(address);
}

template <bool enableCache>
FLATTEN FORCE_INLINE uint8 SH2::MemPeekByte(uint32 address) {
    return MemRead<uint8, false, true, enableCache>(address);
}

template <bool enableCache>
FLATTEN FORCE_INLINE uint16 SH2::MemPeekWord(uint32 address) {
    return MemRead<uint16, false, true, enableCache>(address);
}

template <bool enableCache>
FLATTEN FORCE_INLINE uint32 SH2::MemPeekLong(uint32 address) {
    return MemRead<uint32, false, true, enableCache>(address);
}

template <bool enableCache>
FLATTEN FORCE_INLINE void SH2::MemPokeByte(uint32 address, uint8 value) {
    MemWrite<uint8, true, false, enableCache>(address, value);
}

template <bool enableCache>
FLATTEN FORCE_INLINE void SH2::MemPokeWord(uint32 address, uint16 value) {
    MemWrite<uint16, true, false, enableCache>(address, value);
}

template <bool enableCache>
FLATTEN FORCE_INLINE void SH2::MemPokeLong(uint32 address, uint32 value) {
    MemWrite<uint32, true, false, enableCache>(address, value);
}

template <mem_primitive T>
T SH2::OpenBusSeqRead(uint32 address) {
    if constexpr (std::is_same_v<T, uint8>) {
        return (address & 1u) * ((address >> 1u) & 0x7);
        // return OpenBusSeqRead<uint16>(address) >> (((address & 1) ^ 1) * 8);
    } else if constexpr (std::is_same_v<T, uint16>) {
        return (address >> 1u) & 0x7;
    } else if constexpr (std::is_same_v<T, uint32>) {
        return (OpenBusSeqRead<uint16>(address + 1) << 16u) | OpenBusSeqRead<uint16>(address);
    }
}

// -----------------------------------------------------------------------------
// On-chip peripherals

template <mem_primitive T, bool peek>
T SH2::OnChipRegRead(uint32 address) {
    // Misaligned memory accesses raise an address error, therefore:
    //   (address & 3) == 2 is only valid for 16-bit accesses
    //   (address & 1) == 1 is only valid for 8-bit accesses
    // Additionally:
    //   (address & 1) == 0 has special cases for registers 0-255:
    //     8-bit read from a 16-bit register:  r >> 8u
    //     16-bit read from a 8-bit register: (r << 8u) | r
    //     Every other access returns just r

    if constexpr (std::is_same_v<T, uint32>) {
        return OnChipRegReadLong<peek>(address);
    } else if constexpr (std::is_same_v<T, uint16>) {
        return OnChipRegReadWord<peek>(address);
    } else if constexpr (std::is_same_v<T, uint8>) {
        return OnChipRegReadByte<peek>(address);
    }
}

template <bool peek>
FORCE_INLINE uint8 SH2::OnChipRegReadByte(uint32 address) {
    if (address >= 0x100) {
        if constexpr (peek) {
            const uint16 value = OnChipRegReadWord<true>(address & ~1);
            return value >> ((~address & 1) * 8u);
        } else {
            // Registers 0x100-0x1FF do not accept 8-bit accesses
            // TODO: raise CPU address error
            devlog::debug<grp::reg>(m_logPrefix, "Illegal 8-bit on-chip register read from {:03X}", address);
            return 0;
        }
    }

    switch (address) {
    case 0x04: return 0; // TODO: SCI SSR
    case 0x10: return FRT.ReadTIER();
    case 0x11: return FRT.ReadFTCSR();
    case 0x12: return FRT.ReadFRCH<peek>();
    case 0x13: return FRT.ReadFRCL<peek>();
    case 0x14: return FRT.ReadOCRH();
    case 0x15: return FRT.ReadOCRL();
    case 0x16: return FRT.ReadTCR();
    case 0x17: return FRT.ReadTOCR();
    case 0x18: return FRT.ReadICRH<peek>();
    case 0x19: return FRT.ReadICRL<peek>();

    case 0x60: return (INTC.GetLevel(InterruptSource::SCI_ERI) << 4u) | INTC.GetLevel(InterruptSource::FRT_ICI);
    case 0x61: return 0;
    case 0x62: return INTC.GetVector(InterruptSource::SCI_ERI);
    case 0x63: return INTC.GetVector(InterruptSource::SCI_RXI);
    case 0x64: return INTC.GetVector(InterruptSource::SCI_TXI);
    case 0x65: return INTC.GetVector(InterruptSource::SCI_TEI);
    case 0x66: return INTC.GetVector(InterruptSource::FRT_ICI);
    case 0x67: return INTC.GetVector(InterruptSource::FRT_OCI);
    case 0x68: return INTC.GetVector(InterruptSource::FRT_OVI);
    case 0x69: return 0;

    case 0x71: return m_dmaChannels[0].ReadDRCR();
    case 0x72: return m_dmaChannels[1].ReadDRCR();

    case 0x80: return WDT.ReadWTCSR();
    case 0x81: return WDT.ReadWTCNT();
    case 0x83: return WDT.ReadRSTCSR();

    case 0x91: return SBYCR.u8;
    case 0x92: return m_cache.ReadCCR();
    case 0x93: return m_cache.ReadCCR();
    case 0x94: return m_cache.ReadCCR();
    case 0x95: return m_cache.ReadCCR();
    case 0x96: return m_cache.ReadCCR();
    case 0x97: return m_cache.ReadCCR();
    case 0x98: return m_cache.ReadCCR();
    case 0x99: return m_cache.ReadCCR();
    case 0x9A: return m_cache.ReadCCR();
    case 0x9B: return m_cache.ReadCCR();
    case 0x9C: return m_cache.ReadCCR();
    case 0x9D: return m_cache.ReadCCR();
    case 0x9E: return m_cache.ReadCCR();
    case 0x9F: return m_cache.ReadCCR();

    case 0xE0: return OnChipRegReadWord<peek>(address) >> 8u;
    case 0xE1: return OnChipRegReadWord<peek>(address & ~1) >> 0u;
    case 0xE2: return (INTC.GetLevel(InterruptSource::DIVU_OVFI) << 4u) | INTC.GetLevel(InterruptSource::DMAC0_XferEnd);
    case 0xE3: return INTC.GetLevel(InterruptSource::WDT_ITI) << 4u;
    case 0xE4: return INTC.GetVector(InterruptSource::WDT_ITI);
    case 0xE5: return INTC.GetVector(InterruptSource::BSC_REF_CMI);

    default: //
        if constexpr (!peek) {
            devlog::debug<grp::reg>(m_logPrefix, "Unhandled 8-bit on-chip register read from {:03X}", address);
        }
        return 0;
    }
}

template <bool peek>
FORCE_INLINE uint16 SH2::OnChipRegReadWord(uint32 address) {
    if (address < 0x100) {
        if (address == 0xE0) {
            return INTC.ReadICR();
        }
        if constexpr (peek) {
            uint16 value = OnChipRegReadByte<peek>(address + 0) << 8u;
            value |= OnChipRegReadByte<peek>(address + 1) << 0u;
            return value;
        } else {
            const uint16 value = OnChipRegReadByte<peek>(address);
            return (value << 8u) | value;
        }
    } else {
        return OnChipRegReadLong<peek>(address & ~3);
    }
}

template <bool peek>
FORCE_INLINE uint32 SH2::OnChipRegReadLong(uint32 address) {
    if (address < 0x100) {
        if constexpr (peek) {
            uint32 value = OnChipRegReadWord<true>(address & ~3) << 16u;
            value |= OnChipRegReadWord<true>((address & ~3) | 2) << 0u;
            return value;
        } else {
            // Registers 0x000-0x0FF do not accept 32-bit accesses
            // TODO: raise CPU address error
            devlog::debug<grp::reg>(m_logPrefix, "Illegal 32-bit on-chip register read from {:03X}", address);
            return 0;
        }
    }

    switch (address) {
    case 0x100:
    case 0x120: return DIVU.DVSR;

    case 0x104:
    case 0x124: return DIVU.DVDNT;

    case 0x108:
    case 0x128: return DIVU.DVCR.Read();

    case 0x10C:
    case 0x12C: return INTC.GetVector(InterruptSource::DIVU_OVFI);

    case 0x110:
    case 0x130: return DIVU.DVDNTH;

    case 0x114:
    case 0x134: return DIVU.DVDNTL;

    case 0x118:
    case 0x138: return DIVU.DVDNTUH;

    case 0x11C:
    case 0x13C: return DIVU.DVDNTUL;

    case 0x180: return m_dmaChannels[0].srcAddress;
    case 0x184: return m_dmaChannels[0].dstAddress;
    case 0x188: return m_dmaChannels[0].xferCount;
    case 0x18C: return m_dmaChannels[0].ReadCHCR();

    case 0x190: return m_dmaChannels[1].srcAddress;
    case 0x194: return m_dmaChannels[1].dstAddress;
    case 0x198: return m_dmaChannels[1].xferCount;
    case 0x19C: return m_dmaChannels[1].ReadCHCR();

    case 0x1A0: return INTC.GetVector(InterruptSource::DMAC0_XferEnd);
    case 0x1A8: return INTC.GetVector(InterruptSource::DMAC1_XferEnd);

    case 0x1B0: return DMAOR.Read();

    case 0x1E0: return BCR1.u16;
    case 0x1E4: return BCR2.u16;
    case 0x1E8: return WCR.u16;
    case 0x1EC: return MCR.u16;
    case 0x1F0: return RTCSR.u16;
    case 0x1F4: return RTCNT;
    case 0x1F8: return RTCOR;

    default: //
        if constexpr (!peek) {
            devlog::debug<grp::reg>(m_logPrefix, "Unhandled 32-bit on-chip register read from {:03X}", address);
        }
        return 0;
    }
}

template <mem_primitive T, bool poke, bool debug, bool enableCache>
void SH2::OnChipRegWrite(uint32 address, T value) {
    // Misaligned memory accesses raise an address error, therefore:
    //   (address & 3) == 2 is only valid for 16-bit accesses
    //   (address & 1) == 1 is only valid for 8-bit accesses
    if constexpr (std::is_same_v<T, uint32>) {
        OnChipRegWriteLong<poke, debug, enableCache>(address, value);
    } else if constexpr (std::is_same_v<T, uint16>) {
        OnChipRegWriteWord<poke, debug, enableCache>(address, value);
    } else if constexpr (std::is_same_v<T, uint8>) {
        OnChipRegWriteByte<poke, debug, enableCache>(address, value);
    }
}

template <bool poke, bool debug, bool enableCache>
FORCE_INLINE void SH2::OnChipRegWriteByte(uint32 address, uint8 value) {
    if (address >= 0x100) {
        if constexpr (poke) {
            uint16 currValue = OnChipRegReadWord<true>(address & ~1);
            const uint16 shift = (~address & 1) & 8u;
            const uint16 mask = ~(0xFF << shift);
            currValue = (currValue & mask) | (value << shift);
            OnChipRegWriteWord<true, debug, enableCache>(address & ~1, currValue);
        } else {
            // Registers 0x100-0x1FF do not accept 8-bit accesses
            // TODO: raise CPU address error
            devlog::debug<grp::reg>(m_logPrefix, "Illegal 8-bit on-chip register write to {:03X} = {:X}", address,
                                    value);
        }
        return;
    }

    if constexpr (poke) {
        switch (address) {
        case 0x80: WDT.WriteWTCSR<poke>(value); break;
        case 0x81: WDT.WriteWTCNT(value); break;
        case 0x83: WDT.WriteRSTCSR<poke>(value); break;
        case 0x88: WDT.WriteWTCSR<poke>(value); break;
        case 0x89: WDT.WriteWTCNT(value); break;
        case 0x8B: WDT.WriteRSTCSR<poke>(value); break;

        case 0x93: m_cache.WriteCCR<poke>(value); break;
        case 0x94: m_cache.WriteCCR<poke>(value); break;
        case 0x95: m_cache.WriteCCR<poke>(value); break;
        case 0x96: m_cache.WriteCCR<poke>(value); break;
        case 0x97: m_cache.WriteCCR<poke>(value); break;
        case 0x98: m_cache.WriteCCR<poke>(value); break;
        case 0x99: m_cache.WriteCCR<poke>(value); break;
        case 0x9A: m_cache.WriteCCR<poke>(value); break;
        case 0x9B: m_cache.WriteCCR<poke>(value); break;
        case 0x9C: m_cache.WriteCCR<poke>(value); break;
        case 0x9D: m_cache.WriteCCR<poke>(value); break;
        case 0x9E: m_cache.WriteCCR<poke>(value); break;
        case 0x9F: m_cache.WriteCCR<poke>(value); break;
        }
    }

    switch (address) {
    case 0x10:
        FRT.WriteTIER(value);
        if (INTC.pending.source == InterruptSource::FRT_OVI || INTC.pending.source == InterruptSource::FRT_OCI ||
            INTC.pending.source == InterruptSource::FRT_ICI) {
            RecalcInterrupts();
        }
        break;
    case 0x11:
        FRT.WriteFTCSR<poke>(value);
        if (INTC.pending.source == InterruptSource::FRT_OVI || INTC.pending.source == InterruptSource::FRT_OCI ||
            INTC.pending.source == InterruptSource::FRT_ICI) {
            RecalcInterrupts();
        }
        break;
    case 0x12: FRT.WriteFRCH<poke>(value); break;
    case 0x13: FRT.WriteFRCL<poke>(value); break;
    case 0x14: FRT.WriteOCRH<poke>(value); break;
    case 0x15: FRT.WriteOCRL<poke>(value); break;
    case 0x16: FRT.WriteTCR(value); break;
    case 0x17: FRT.WriteTOCR(value); break;
    case 0x18: FRT.WriteICRH<poke>(value); break; // ICRH is read-only
    case 0x19: FRT.WriteICRL<poke>(value); break; // ICRL is read-only

    case 0x60: //
    {
        const uint8 frtIntrLevel = bit::extract<0, 3>(value);
        const uint8 sciIntrLevel = bit::extract<4, 7>(value);

        using enum InterruptSource;
        INTC.SetLevel(FRT_ICI, frtIntrLevel);
        INTC.SetLevel(FRT_OCI, frtIntrLevel);
        INTC.SetLevel(FRT_OVI, frtIntrLevel);
        INTC.SetLevel(SCI_ERI, sciIntrLevel);
        INTC.SetLevel(SCI_RXI, sciIntrLevel);
        INTC.SetLevel(SCI_TXI, sciIntrLevel);
        INTC.SetLevel(SCI_TEI, sciIntrLevel);
        UpdateInterruptLevels<FRT_ICI, FRT_OCI, FRT_OVI, SCI_ERI, SCI_RXI, SCI_TXI, SCI_TEI>();
        break;
    }
    case 0x61: /* IPRB bits 7-0 are all reserved */ break;
    case 0x62: INTC.SetVector(InterruptSource::SCI_ERI, bit::extract<0, 6>(value)); break;
    case 0x63: INTC.SetVector(InterruptSource::SCI_RXI, bit::extract<0, 6>(value)); break;
    case 0x64: INTC.SetVector(InterruptSource::SCI_TXI, bit::extract<0, 6>(value)); break;
    case 0x65: INTC.SetVector(InterruptSource::SCI_TEI, bit::extract<0, 6>(value)); break;
    case 0x66: INTC.SetVector(InterruptSource::FRT_ICI, bit::extract<0, 6>(value)); break;
    case 0x67: INTC.SetVector(InterruptSource::FRT_OCI, bit::extract<0, 6>(value)); break;
    case 0x68: INTC.SetVector(InterruptSource::FRT_OVI, bit::extract<0, 6>(value)); break;
    case 0x69: /* VCRD bits 7-0 are all reserved */ break;

    case 0x71: m_dmaChannels[0].WriteDRCR(value); break;
    case 0x72: m_dmaChannels[1].WriteDRCR(value); break;

    case 0x91: SBYCR.u8 = value & 0xDF; break;
    case 0x92: m_cache.WriteCCR<poke>(value); break;

    case 0xE0: INTC.WriteICR<false, true, poke>(value << 8u); break;
    case 0xE1: INTC.WriteICR<true, false, poke>(value); break;
    case 0xE2: //
    {
        const uint8 dmacIntrLevel = bit::extract<0, 3>(value);
        const uint8 divuIntrLevel = bit::extract<4, 7>(value);

        using enum InterruptSource;
        INTC.SetLevel(DMAC0_XferEnd, dmacIntrLevel);
        INTC.SetLevel(DMAC1_XferEnd, dmacIntrLevel);
        INTC.SetLevel(DIVU_OVFI, divuIntrLevel);
        UpdateInterruptLevels<DMAC0_XferEnd, DMAC1_XferEnd, DIVU_OVFI>();
        break;
    }
    case 0xE3: //
    {
        const uint8 wdtIntrLevel = bit::extract<4, 7>(value);

        using enum InterruptSource;
        INTC.SetLevel(WDT_ITI, wdtIntrLevel);
        UpdateInterruptLevels<WDT_ITI>();
        break;
    }
    case 0xE4: INTC.SetVector(InterruptSource::WDT_ITI, bit::extract<0, 6>(value)); break;
    case 0xE5: INTC.SetVector(InterruptSource::BSC_REF_CMI, bit::extract<0, 6>(value)); break;

    default: //
        if constexpr (!poke) {
            devlog::debug<grp::reg>(m_logPrefix, "Unhandled 8-bit on-chip register write to {:03X} = {:X}", address,
                                    value);
        }
        break;
    }
}

template <bool poke, bool debug, bool enableCache>
FORCE_INLINE void SH2::OnChipRegWriteWord(uint32 address, uint16 value) {
    switch (address) {
    case 0x60:
    case 0x61:
    case 0x62:
    case 0x63:
    case 0x64:
    case 0x65:
    case 0x66:
    case 0x67:
    case 0x68:
    case 0x69:

    case 0xE2:
    case 0xE3:
    case 0xE4:
    case 0xE5:
        OnChipRegWriteByte<poke, debug, enableCache>(address & ~1, value >> 8u);
        OnChipRegWriteByte<poke, debug, enableCache>(address | 1, value >> 0u);
        break;

    case 0x80:
        if ((value >> 8u) == 0x5A) {
            WDT.WriteWTCNT(value);
        } else if ((value >> 8u) == 0xA5) {
            WDT.WriteWTCSR<poke>(value);
        }
        break;
    case 0x82:
        if ((value >> 8u) == 0x5A) {
            WDT.WriteRSTE_RSTS(value);
        } else if ((value >> 8u) == 0xA5) {
            WDT.WriteWOVF<poke>(value);
        }
        break;

    case 0x88:
        if ((value >> 8u) == 0x5A) {
            WDT.WriteWTCNT(value);
        } else if ((value >> 8u) == 0xA5) {
            WDT.WriteWTCSR<poke>(value);
        }
        break;
    case 0x8A:
        if ((value >> 8u) == 0x5A) {
            WDT.WriteRSTE_RSTS(value);
        } else if ((value >> 8u) == 0xA5) {
            WDT.WriteWOVF<poke>(value);
        }
        break;

    case 0x92: m_cache.WriteCCR<poke>(value); break;

    case 0xE0: INTC.WriteICR<true, true, poke>(value); break;

    case 0x108:
    case 0x10C:

    case 0x1E0:
    case 0x1E4:
    case 0x1E8:
    case 0x1EC:
    case 0x1F0:
    case 0x1F4:
    case 0x1F8: //
        OnChipRegWriteLong<poke, debug, enableCache>(address & ~3, value);
        break;

    default: //
        if constexpr (!poke) {
            devlog::debug<grp::reg>(m_logPrefix, "Illegal 16-bit on-chip register write to {:03X} = {:X}", address,
                                    value);
        }
        break;
    }
}

template <bool poke, bool debug, bool enableCache>
FORCE_INLINE void SH2::OnChipRegWriteLong(uint32 address, uint32 value) {
    if (address < 0x100) {
        if constexpr (poke) {
            OnChipRegWriteWord<true, debug, enableCache>(address + 0, value >> 16u);
            OnChipRegWriteWord<true, debug, enableCache>(address + 2, value >> 0u);
        } else {
            // Registers 0x000-0x0FF do not accept 32-bit accesses
            // TODO: raise CPU address error
            devlog::debug<grp::reg>(m_logPrefix, "Illegal 32-bit on-chip register write to {:03X} = {:X}", address,
                                    value);
        }
        return;
    }

    switch (address) {
    case 0x100:
    case 0x120: DIVU.DVSR = value; break;

    case 0x104:
    case 0x124:
        DIVU.DVDNT = value;
        if constexpr (!poke) {
            ExecuteDiv32<debug>();
        }
        break;

    case 0x108:
    case 0x128: DIVU.DVCR.Write(value); break;

    case 0x10C:
    case 0x12C: INTC.SetVector(InterruptSource::DIVU_OVFI, bit::extract<0, 6>(value)); break;

    case 0x110:
    case 0x130: DIVU.DVDNTH = value; break;

    case 0x114:
    case 0x134:
        DIVU.DVDNTL = value;
        if constexpr (!poke) {
            ExecuteDiv64<debug>();
        }
        break;

    case 0x118:
    case 0x138: DIVU.DVDNTUH = value; break;

    case 0x11C:
    case 0x13C: DIVU.DVDNTUL = value; break;

    case 0x180: m_dmaChannels[0].srcAddress = value; break;
    case 0x184: m_dmaChannels[0].dstAddress = value; break;
    case 0x188: m_dmaChannels[0].xferCount = bit::extract<0, 23>(value); break;
    case 0x18C:
        m_dmaChannels[0].WriteCHCR<poke>(value);
        if constexpr (!poke) {
            RunDMAC<debug, enableCache>(0); // TODO: should be scheduled
        }
        break;

    case 0x190: m_dmaChannels[1].srcAddress = value; break;
    case 0x194: m_dmaChannels[1].dstAddress = value; break;
    case 0x198: m_dmaChannels[1].xferCount = bit::extract<0, 23>(value); break;
    case 0x19C:
        m_dmaChannels[1].WriteCHCR<poke>(value);
        if constexpr (!poke) {
            RunDMAC<debug, enableCache>(1); // TODO: should be scheduled
        }
        break;

    case 0x1A0: INTC.SetVector(InterruptSource::DMAC0_XferEnd, bit::extract<0, 6>(value)); break;
    case 0x1A8: INTC.SetVector(InterruptSource::DMAC1_XferEnd, bit::extract<0, 6>(value)); break;

    case 0x1B0:
        DMAOR.Write<poke>(value);
        if constexpr (!poke) {
            RunDMAC<debug, enableCache>(0); // TODO: should be scheduled
            RunDMAC<debug, enableCache>(1); // TODO: should be scheduled
        }
        break;

    case 0x1E0: // BCR1
        if ((value >> 16u) == 0xA55A) {
            BCR1.u15 = value & 0x1FF7;
        }
        break;
    case 0x1E4: // BCR2
        if ((value >> 16u) == 0xA55A) {
            BCR2.u16 = value & 0xFC;
        }
        break;
    case 0x1E8: // WCR
        if ((value >> 16u) == 0xA55A) {
            WCR.u16 = value;
        }
        break;
    case 0x1EC: // MCR
        if ((value >> 16u) == 0xA55A) {
            MCR.u16 = value & 0xFEFC;
        }
        break;
    case 0x1F0: // RTCSR
        if ((value >> 16u) == 0xA55A) {
            // TODO: implement the set/clear rules for RTCSR.CMF
            RTCSR.u16 = (value & 0x78) | (RTCSR.u16 & 0x80);
        }
        break;
    case 0x1F4: // RTCNT
        if ((value >> 16u) == 0xA55A) {
            RTCNT = value;
        }
        break;
    case 0x1F8: // RTCOR
        if ((value >> 16u) == 0xA55A) {
            RTCOR = value;
        }
        break;
    default: //
        if constexpr (!poke) {
            devlog::debug<grp::reg>(m_logPrefix, "Unhandled 32-bit on-chip register write to {:03X} = {:X}", address,
                                    value);
        }
        break;
    }
}

FLATTEN FORCE_INLINE bool SH2::IsDMATransferActive(const DMAChannel &ch) const {
    return ch.IsEnabled() && DMAOR.DME && !DMAOR.NMIF && !DMAOR.AE;
}

template <bool debug, bool enableCache>
void SH2::RunDMAC(uint32 channel) {
    auto &ch = m_dmaChannels[channel];

    // TODO: prioritize channels based on DMAOR.PR
    // TODO: proper timings, cycle-stealing, etc. (suspend instructions if not cached)

    if (!IsDMATransferActive(ch)) {
        return;
    }

    // Auto request mode will start the transfer right now.
    // Module request mode checks if the signal from the configured source has been raised.
    if (!ch.autoRequest) {
        bool signal = false;
        switch (ch.resSelect) {
        case DMAResourceSelect::DREQ: /*TODO*/ signal = false; break;
        case DMAResourceSelect::RXI: /*TODO*/ signal = false; break;
        case DMAResourceSelect::TXI: /*TODO*/ signal = false; break;
        case DMAResourceSelect::Reserved: signal = false; break;
        }
        if (!signal) {
            return;
        }
    }

    static constexpr uint32 kXferSize[] = {1, 2, 4, 16};
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

    TraceDMAXferBegin<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, ch.xferCount, xferSize, srcInc, dstInc);

    do {
        // Perform one unit of transfer
        switch (ch.xferSize) {
        case DMATransferSize::Byte: {
            const uint8 value = MemReadByte<enableCache>(ch.srcAddress);
            devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} 8-bit transfer from {:08X} to {:08X} -> {:X}", channel,
                                         ch.srcAddress, ch.dstAddress, value);
            MemWriteByte<debug, enableCache>(ch.dstAddress, value);
            TraceDMAXferData<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, value, xferSize);
            break;
        }
        case DMATransferSize::Word: {
            const uint16 value = MemReadWord<enableCache>(ch.srcAddress);
            devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} 16-bit transfer from {:08X} to {:08X} -> {:X}", channel,
                                         ch.srcAddress, ch.dstAddress, value);
            MemWriteWord<debug, enableCache>(ch.dstAddress, value);
            TraceDMAXferData<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, value, xferSize);
            break;
        }
        case DMATransferSize::Longword: {
            const uint32 value = MemReadLong<enableCache>(ch.srcAddress);
            devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} 32-bit transfer from {:08X} to {:08X} -> {:X}", channel,
                                         ch.srcAddress, ch.dstAddress, value);
            MemWriteLong<debug, enableCache>(ch.dstAddress, value);
            TraceDMAXferData<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, value, xferSize);
            break;
        }
        case DMATransferSize::QuadLongword:
            for (int i = 0; i < 4; i++) {
                const uint32 value = MemReadLong<enableCache>(ch.srcAddress + i * sizeof(uint32));
                devlog::trace<grp::dma_xfer>(m_logPrefix, "DMAC{} 16-byte transfer {:d} from {:08X} to {:08X} -> {:X}",
                                             channel, i, ch.srcAddress, ch.dstAddress, value);
                MemWriteLong<debug, enableCache>(ch.dstAddress + i * sizeof(uint32), value);
                TraceDMAXferData<debug>(m_tracer, channel, ch.srcAddress, ch.dstAddress, value, 4);
            }
            break;
        }

        // Update address and remaining count
        ch.srcAddress += srcInc;
        ch.dstAddress += dstInc;

        if (ch.xferSize == DMATransferSize::QuadLongword) {
            if (ch.xferCount >= 4) {
                ch.xferCount -= 4;
            } else {
                devlog::trace<grp::dma>(m_logPrefix, "DMAC{} 16-byte transfer count misaligned", channel);
                ch.xferCount = 0;
            }
        } else {
            ch.xferCount--;
        }
    } while (ch.xferCount > 0);

    TraceDMAXferEnd<debug>(m_tracer, channel, ch.irqEnable);

    ch.xferEnded = true;
    devlog::trace<grp::dma>(m_logPrefix, "DMAC{} transfer finished", channel);
    if (ch.irqEnable) {
        switch (channel) {
        case 0: RaiseInterrupt(InterruptSource::DMAC0_XferEnd); break;
        case 1: RaiseInterrupt(InterruptSource::DMAC1_XferEnd); break;
        }
    }
}

FORCE_INLINE void SH2::AdvanceWDT(uint64 cycles) {
    switch (WDT.Advance(cycles)) {
    case WatchdogTimer::Event::None: break;
    case WatchdogTimer::Event::Reset: Reset(WDT.RSTCSR.RSTS, true); break;
    case WatchdogTimer::Event::RaiseInterrupt: RaiseInterrupt(InterruptSource::WDT_ITI); break;
    }
}

template <bool debug>
FORCE_INLINE void SH2::ExecuteDiv32() {
    DIVU.DVDNTL = DIVU.DVDNT;
    DIVU.DVDNTH = static_cast<sint32>(DIVU.DVDNT) >> 31;
    TraceBegin32x32Division<debug>(m_tracer, DIVU.DVDNTL, DIVU.DVSR, DIVU.DVCR.OVFIE);
    DIVU.Calc32();
    TraceEndDivision<debug>(m_tracer, DIVU.DVDNTL, DIVU.DVDNTH, DIVU.DVCR.OVF);
    if (DIVU.DVCR.OVF && DIVU.DVCR.OVFIE) {
        RaiseInterrupt(InterruptSource::DIVU_OVFI);
    }
}

template <bool debug>
FORCE_INLINE void SH2::ExecuteDiv64() {
    TraceBegin64x32Division<debug>(m_tracer,
                                   (static_cast<sint64>(DIVU.DVDNTH) << 32ll) | static_cast<sint64>(DIVU.DVDNTL),
                                   DIVU.DVSR, DIVU.DVCR.OVFIE);
    DIVU.Calc64();
    TraceEndDivision<debug>(m_tracer, DIVU.DVDNTL, DIVU.DVDNTH, DIVU.DVCR.OVF);
    if (DIVU.DVCR.OVF && DIVU.DVCR.OVFIE) {
        RaiseInterrupt(InterruptSource::DIVU_OVFI);
    }
}

FORCE_INLINE void SH2::AdvanceFRT(uint64 cycles) {
    switch (FRT.Advance(cycles)) {
    case FreeRunningTimer::Event::None: break;
    case FreeRunningTimer::Event::OCI: RaiseInterrupt(InterruptSource::FRT_OCI); break;
    case FreeRunningTimer::Event::OVI: RaiseInterrupt(InterruptSource::FRT_OVI); break;
    }
}

FORCE_INLINE void SH2::TriggerFRTInputCapture() {
    // TODO: FRT.TCR.IEDGA
    FRT.ICR = FRT.FRC;
    FRT.FTCSR.ICF = 1;
    if (FRT.TIER.ICIE) {
        RaiseInterrupt(InterruptSource::FRT_ICI);
    }
}

// -----------------------------------------------------------------------------
// Interrupts

FORCE_INLINE void SH2::SetExternalInterrupt(uint8 level, uint8 vector) {
    assert(level < 16);

    static constexpr InterruptSource source = InterruptSource::IRL;

    INTC.externalVector = vector;
    INTC.SetLevel(source, level);

    if (level > 0) {
        INTC.UpdateIRLVector();
        UpdateInterruptLevels<source>();
        RaiseInterrupt(source);
        devlog::trace<grp::exec>(m_logPrefix, "Set IRL vector/level to {:02X}/{:X}; pending level {:X}", vector, level,
                                 INTC.pending.level);
    } else {
        INTC.SetVector(source, 0);
        LowerInterrupt(source);
    }
}

template <InterruptSource source, InterruptSource... sources>
FLATTEN FORCE_INLINE void SH2::UpdateInterruptLevels() {
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

void SH2::RecalcInterrupts() {
    // Check interrupts and use the vector number of the exception with highest priority
    // See documentation for InterruptSource for related registers and default/tie-breaker priority order

    INTC.pending.level = 0;
    INTC.pending.source = InterruptSource::None;

    // HACK: should be edge-detected
    if (INTC.NMI) {
        RaiseInterrupt(InterruptSource::NMI);
        return;
    }

    // TODO: user break
    /*if (...) {
        RaiseInterrupt(InterruptSource::UserBreak);
        return;
    }*/

    // IRLs
    if (INTC.GetLevel(InterruptSource::IRL) > 0) {
        RaiseInterrupt(InterruptSource::IRL);
    }

    // Division overflow
    if (DIVU.DVCR.OVF && DIVU.DVCR.OVFIE) {
        RaiseInterrupt(InterruptSource::DIVU_OVFI);
        return;
    }

    // DMA channel transfer end
    if (m_dmaChannels[0].xferEnded && m_dmaChannels[0].irqEnable) {
        RaiseInterrupt(InterruptSource::DMAC0_XferEnd);
        return;
    }
    if (m_dmaChannels[1].xferEnded && m_dmaChannels[1].irqEnable) {
        RaiseInterrupt(InterruptSource::DMAC1_XferEnd);
        return;
    }

    // Watchdog timer
    if (WDT.WTCSR.OVF && !WDT.WTCSR.WT_nIT) {
        RaiseInterrupt(InterruptSource::WDT_ITI);
        return;
    }

    // TODO: BSC REF CMI
    /*if (...) {
        RaiseInterrupt(InterruptSource::BSC_REF_CMI);
        return;
    }*/

    // TODO: SCI ERI, RXI, TXI, TEI
    /*if (...) {
        RaiseInterrupt(InterruptSource::SCI_ERI);
        return;
    }*/
    /*if (...) {
        RaiseInterrupt(InterruptSource::SCI_RXI);
        return;
    }*/
    /*if (...) {
        RaiseInterrupt(InterruptSource::SCI_TXI);
        return;
    }*/
    /*if (...) {
        RaiseInterrupt(InterruptSource::SCI_TEI);
        return;
    }*/

    // Free-running timer interrupts
    if (FRT.FTCSR.ICF && FRT.TIER.ICIE) {
        RaiseInterrupt(InterruptSource::FRT_ICI);
        return;
    }
    if ((FRT.FTCSR.OCFA && FRT.TIER.OCIAE) || (FRT.FTCSR.OCFB && FRT.TIER.OCIBE)) {
        RaiseInterrupt(InterruptSource::FRT_OCI);
        return;
    }
    if (FRT.FTCSR.OVF && FRT.TIER.OVIE) {
        RaiseInterrupt(InterruptSource::FRT_OVI);
        return;
    }
}

// -------------------------------------------------------------------------
// Helper functions

template <bool debug, bool enableCache>
FORCE_INLINE void SH2::EnterException(uint8 vectorNumber) {
    TraceException<debug>(m_tracer, vectorNumber, PC, SR.u32);
    R[15] -= 4;
    MemWriteLong<debug, enableCache>(R[15], SR.u32);
    R[15] -= 4;
    MemWriteLong<debug, enableCache>(R[15], PC - 4);
    PC = MemReadLong<enableCache>(VBR + (static_cast<uint32>(vectorNumber) << 2u));
    FillPipeline<enableCache>();
}

// -----------------------------------------------------------------------------
// Instruction interpreters
template <bool debug, bool enableCache>
FORCE_INLINE uint64 SH2::InterpretNext() {
    if (CheckInterrupts()) [[unlikely]] {
        // Service interrupt
        const uint8 vecNum = INTC.GetVector(INTC.pending.source);
        TraceInterrupt<debug>(m_tracer, vecNum, INTC.pending.level, INTC.pending.source, PC);
        devlog::trace<grp::exec>(m_logPrefix, "Handling interrupt level {:02X}, vector number {:02X}, PC {:08X}",
                                 INTC.pending.level, vecNum, PC);
        EnterException<debug, enableCache>(vecNum);
        SR.ILevel = std::min<uint8>(INTC.pending.level, 0xF);

        // Acknowledge interrupt
        switch (INTC.pending.source) {
        case InterruptSource::IRL: m_cbAcknowledgeExternalInterrupt(); break;
        case InterruptSource::NMI:
            INTC.NMI = false;
            LowerInterrupt(InterruptSource::NMI);
            break;
        default: break;
        }
        return 9;
    }

    // TODO: emulate or approximate fetch - decode - execute - memory access - writeback pipeline

    auto jumpToDelaySlot = [&] {
        m_delaySlot = false;
        FetchInstruction<enableCache>(PC + 2);
        PC += 4;
    };

    const uint16 instr = FetchInstruction<enableCache>(PC);
    TraceExecuteInstruction<debug>(m_tracer, PC, instr, m_delaySlot);

    const OpcodeType opcode = DecodeTable::s_instance.opcodes[m_delaySlot][instr];
    const DecodedArgs &args = DecodeTable::s_instance.args[instr];

    switch (opcode) {
    case OpcodeType::NOP: NOP(), PC += 2; return 1;

    case OpcodeType::SLEEP: SLEEP(), PC += 2; return 3;

    case OpcodeType::MOV_R: MOV(args), PC += 2; return 1;
    case OpcodeType::MOVB_L: MOVBL<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_L: MOVWL<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_L: MOVLL<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_L0: MOVBL0<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_L0: MOVWL0<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_L0: MOVLL0<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_L4: MOVBL4<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_L4: MOVWL4<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_L4: MOVLL4<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_LG: MOVBLG<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_LG: MOVWLG<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_LG: MOVLLG<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_M: MOVBM<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_M: MOVWM<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_M: MOVLM<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_P: MOVBP<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_P: MOVWP<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_P: MOVLP<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_S: MOVBS<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_S: MOVWS<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_S: MOVLS<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_S0: MOVBS0<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_S0: MOVWS0<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_S0: MOVLS0<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_S4: MOVBS4<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_S4: MOVWS4<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_S4: MOVLS4<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVB_SG: MOVBSG<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVW_SG: MOVWSG<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOVL_SG: MOVLSG<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::MOV_I: MOVI(args), PC += 2; return 1;
    case OpcodeType::MOVW_I: MOVWI<debug, enableCache, false>(args), PC += 2; return 1;
    case OpcodeType::MOVL_I: MOVLI<debug, enableCache, false>(args), PC += 2; return 1;
    case OpcodeType::MOVA: MOVA<false>(args), PC += 2; return 1;
    case OpcodeType::MOVT: MOVT(args), PC += 2; return 1;
    case OpcodeType::CLRT: CLRT(), PC += 2; return 1;
    case OpcodeType::SETT: SETT(), PC += 2; return 1;

    case OpcodeType::EXTUB: EXTUB(args), PC += 2; return 1;
    case OpcodeType::EXTUW: EXTUW(args), PC += 2; return 1;
    case OpcodeType::EXTSB: EXTSB(args), PC += 2; return 1;
    case OpcodeType::EXTSW: EXTSW(args), PC += 2; return 1;
    case OpcodeType::SWAPB: SWAPB(args), PC += 2; return 1;
    case OpcodeType::SWAPW: SWAPW(args), PC += 2; return 1;
    case OpcodeType::XTRCT: XTRCT(args), PC += 2; return 1;

    case OpcodeType::LDC_GBR_R: LDCGBR(args), PC += 2; return 1;
    case OpcodeType::LDC_SR_R: LDCSR(args), PC += 2; return 1;
    case OpcodeType::LDC_VBR_R: LDCVBR(args), PC += 2; return 1;
    case OpcodeType::LDC_GBR_M: LDCMGBR<debug, enableCache>(args), PC += 2; return 3;
    case OpcodeType::LDC_SR_M: LDCMSR<debug, enableCache>(args), PC += 2; return 3;
    case OpcodeType::LDC_VBR_M: LDCMVBR<debug, enableCache>(args), PC += 2; return 3;
    case OpcodeType::LDS_MACH_R: LDSMACH(args), PC += 2; return 1;
    case OpcodeType::LDS_MACL_R: LDSMACL(args), PC += 2; return 1;
    case OpcodeType::LDS_PR_R: LDSPR(args), PC += 2; return 1;
    case OpcodeType::LDS_MACH_M: LDSMMACH<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::LDS_MACL_M: LDSMMACL<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::LDS_PR_M: LDSMPR<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::STC_GBR_R: STCGBR(args), PC += 2; return 1;
    case OpcodeType::STC_SR_R: STCSR(args), PC += 2; return 1;
    case OpcodeType::STC_VBR_R: STCVBR(args), PC += 2; return 1;
    case OpcodeType::STC_GBR_M: STCMGBR<debug, enableCache>(args), PC += 2; return 2;
    case OpcodeType::STC_SR_M: STCMSR<debug, enableCache>(args), PC += 2; return 2;
    case OpcodeType::STC_VBR_M: STCMVBR<debug, enableCache>(args), PC += 2; return 2;
    case OpcodeType::STS_MACH_R: STSMACH(args), PC += 2; return 1;
    case OpcodeType::STS_MACL_R: STSMACL(args), PC += 2; return 1;
    case OpcodeType::STS_PR_R: STSPR(args), PC += 2; return 1;
    case OpcodeType::STS_MACH_M: STSMMACH<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::STS_MACL_M: STSMMACL<debug, enableCache>(args), PC += 2; return 1;
    case OpcodeType::STS_PR_M: STSMPR<debug, enableCache>(args), PC += 2; return 1;

    case OpcodeType::ADD: ADD(args), PC += 2; return 1;
    case OpcodeType::ADD_I: ADDI(args), PC += 2; return 1;
    case OpcodeType::ADDC: ADDC(args), PC += 2; return 1;
    case OpcodeType::ADDV: ADDV(args), PC += 2; return 1;
    case OpcodeType::AND_R: AND(args), PC += 2; return 1;
    case OpcodeType::AND_I: ANDI(args), PC += 2; return 1;
    case OpcodeType::AND_M: ANDM<debug, enableCache>(args), PC += 2; return 3;
    case OpcodeType::NEG: NEG(args), PC += 2; return 1;
    case OpcodeType::NEGC: NEGC(args), PC += 2; return 1;
    case OpcodeType::NOT: NOT(args), PC += 2; return 1;
    case OpcodeType::OR_R: OR(args), PC += 2; return 1;
    case OpcodeType::OR_I: ORI(args), PC += 2; return 1;
    case OpcodeType::OR_M: ORM<debug, enableCache>(args), PC += 2; return 3;
    case OpcodeType::ROTCL: ROTCL(args), PC += 2; return 1;
    case OpcodeType::ROTCR: ROTCR(args), PC += 2; return 1;
    case OpcodeType::ROTL: ROTL(args), PC += 2; return 1;
    case OpcodeType::ROTR: ROTR(args), PC += 2; return 1;
    case OpcodeType::SHAL: SHAL(args), PC += 2; return 1;
    case OpcodeType::SHAR: SHAR(args), PC += 2; return 1;
    case OpcodeType::SHLL: SHLL(args), PC += 2; return 1;
    case OpcodeType::SHLL2: SHLL2(args), PC += 2; return 1;
    case OpcodeType::SHLL8: SHLL8(args), PC += 2; return 1;
    case OpcodeType::SHLL16: SHLL16(args), PC += 2; return 1;
    case OpcodeType::SHLR: SHLR(args), PC += 2; return 1;
    case OpcodeType::SHLR2: SHLR2(args), PC += 2; return 1;
    case OpcodeType::SHLR8: SHLR8(args), PC += 2; return 1;
    case OpcodeType::SHLR16: SHLR16(args), PC += 2; return 1;
    case OpcodeType::SUB: SUB(args), PC += 2; return 1;
    case OpcodeType::SUBC: SUBC(args), PC += 2; return 1;
    case OpcodeType::SUBV: SUBV(args), PC += 2; return 1;
    case OpcodeType::XOR_R: XOR(args), PC += 2; return 1;
    case OpcodeType::XOR_I: XORI(args), PC += 2; return 1;
    case OpcodeType::XOR_M: XORM<debug, enableCache>(args), PC += 2; return 3;

    case OpcodeType::DT: DT(args), PC += 2; return 1;

    case OpcodeType::CLRMAC: CLRMAC(), PC += 2; return 1;
    case OpcodeType::MACW: MACW<debug, enableCache>(args), PC += 2; return 2;
    case OpcodeType::MACL: MACL<debug, enableCache>(args), PC += 2; return 2;
    case OpcodeType::MUL: MULL(args), PC += 2; return 2;
    case OpcodeType::MULS: MULS(args), PC += 2; return 1;
    case OpcodeType::MULU: MULU(args), PC += 2; return 1;
    case OpcodeType::DMULS: DMULS(args), PC += 2; return 2;
    case OpcodeType::DMULU: DMULU(args), PC += 2; return 2;

    case OpcodeType::DIV0S: DIV0S(args), PC += 2; return 1;
    case OpcodeType::DIV0U: DIV0U(), PC += 2; return 1;
    case OpcodeType::DIV1: DIV1(args), PC += 2; return 1;

    case OpcodeType::CMP_EQ_I: CMPIM(args), PC += 2; return 1;
    case OpcodeType::CMP_EQ_R: CMPEQ(args), PC += 2; return 1;
    case OpcodeType::CMP_GE: CMPGE(args), PC += 2; return 1;
    case OpcodeType::CMP_GT: CMPGT(args), PC += 2; return 1;
    case OpcodeType::CMP_HI: CMPHI(args), PC += 2; return 1;
    case OpcodeType::CMP_HS: CMPHS(args), PC += 2; return 1;
    case OpcodeType::CMP_PL: CMPPL(args), PC += 2; return 1;
    case OpcodeType::CMP_PZ: CMPPZ(args), PC += 2; return 1;
    case OpcodeType::CMP_STR: CMPSTR(args), PC += 2; return 1;
    case OpcodeType::TAS: TAS<debug, enableCache>(args), PC += 2; return 4;
    case OpcodeType::TST_R: TST(args), PC += 2; return 1;
    case OpcodeType::TST_I: TSTI(args), PC += 2; return 1;
    case OpcodeType::TST_M: TSTM<debug, enableCache>(args), PC += 2; return 3;

    case OpcodeType::BF: return BF<debug, enableCache>(args);
    case OpcodeType::BFS: return BFS(args);
    case OpcodeType::BT: return BT<debug, enableCache>(args);
    case OpcodeType::BTS: return BTS(args);
    case OpcodeType::BRA: BRA(args); return 2;
    case OpcodeType::BRAF: BRAF(args); return 2;
    case OpcodeType::BSR: BSR(args); return 2;
    case OpcodeType::BSRF: BSRF(args); return 2;
    case OpcodeType::JMP: JMP(args); return 2;
    case OpcodeType::JSR: JSR(args); return 2;
    case OpcodeType::TRAPA: TRAPA<debug, enableCache>(args); return 8;

    case OpcodeType::RTE: RTE<debug, enableCache>(); return 4;
    case OpcodeType::RTS: RTS(); return 2;

    case OpcodeType::Illegal: EnterException<debug, enableCache>(xvGenIllegalInstr); return 8;

    case OpcodeType::Delay_NOP: NOP(), jumpToDelaySlot(); return 1;

    case OpcodeType::Delay_SLEEP: SLEEP(), jumpToDelaySlot(); return 3;

    case OpcodeType::Delay_MOV_R: MOV(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_L: MOVBL<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_L: MOVWL<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_L: MOVLL<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_L0: MOVBL0<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_L0: MOVWL0<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_L0: MOVLL0<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_L4: MOVBL4<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_L4: MOVWL4<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_L4: MOVLL4<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_LG: MOVBLG<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_LG: MOVWLG<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_LG: MOVLLG<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_M: MOVBM<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_M: MOVWM<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_M: MOVLM<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_P: MOVBP<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_P: MOVWP<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_P: MOVLP<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_S: MOVBS<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_S: MOVWS<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_S: MOVLS<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_S0: MOVBS0<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_S0: MOVWS0<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_S0: MOVLS0<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_S4: MOVBS4<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_S4: MOVWS4<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_S4: MOVLS4<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVB_SG: MOVBSG<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_SG: MOVWSG<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_SG: MOVLSG<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOV_I: MOVI(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVW_I: MOVWI<debug, enableCache, true>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVL_I: MOVLI<debug, enableCache, true>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVA: MOVA<true>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MOVT: MOVT(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CLRT: CLRT(), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SETT: SETT(), jumpToDelaySlot(); return 1;

    case OpcodeType::Delay_EXTUB: EXTUB(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_EXTUW: EXTUW(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_EXTSB: EXTSB(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_EXTSW: EXTSW(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SWAPB: SWAPB(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SWAPW: SWAPW(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_XTRCT: XTRCT(args), jumpToDelaySlot(); return 1;

    case OpcodeType::Delay_LDC_GBR_R: LDCGBR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_LDC_SR_R: LDCSR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_LDC_VBR_R: LDCVBR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_LDC_GBR_M: LDCMGBR<debug, enableCache>(args), jumpToDelaySlot(); return 3;
    case OpcodeType::Delay_LDC_SR_M: LDCMSR<debug, enableCache>(args), jumpToDelaySlot(); return 3;
    case OpcodeType::Delay_LDC_VBR_M: LDCMVBR<debug, enableCache>(args), jumpToDelaySlot(); return 3;
    case OpcodeType::Delay_LDS_MACH_R: LDSMACH(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_LDS_MACL_R: LDSMACL(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_LDS_PR_R: LDSPR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_LDS_MACH_M: LDSMMACH<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_LDS_MACL_M: LDSMMACL<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_LDS_PR_M: LDSMPR<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STC_GBR_R: STCGBR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STC_SR_R: STCSR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STC_VBR_R: STCVBR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STC_GBR_M: STCMGBR<debug, enableCache>(args), jumpToDelaySlot(); return 2;
    case OpcodeType::Delay_STC_SR_M: STCMSR<debug, enableCache>(args), jumpToDelaySlot(); return 2;
    case OpcodeType::Delay_STC_VBR_M: STCMVBR<debug, enableCache>(args), jumpToDelaySlot(); return 2;
    case OpcodeType::Delay_STS_MACH_R: STSMACH(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STS_MACL_R: STSMACL(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STS_PR_R: STSPR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STS_MACH_M: STSMMACH<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STS_MACL_M: STSMMACL<debug, enableCache>(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_STS_PR_M: STSMPR<debug, enableCache>(args), jumpToDelaySlot(); return 1;

    case OpcodeType::Delay_ADD: ADD(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_ADD_I: ADDI(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_ADDC: ADDC(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_ADDV: ADDV(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_AND_R: AND(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_AND_I: ANDI(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_AND_M: ANDM<debug, enableCache>(args), jumpToDelaySlot(); return 3;
    case OpcodeType::Delay_NEG: NEG(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_NEGC: NEGC(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_NOT: NOT(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_OR_R: OR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_OR_I: ORI(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_OR_M: ORM<debug, enableCache>(args), jumpToDelaySlot(); return 3;
    case OpcodeType::Delay_ROTCL: ROTCL(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_ROTCR: ROTCR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_ROTL: ROTL(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_ROTR: ROTR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHAL: SHAL(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHAR: SHAR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHLL: SHLL(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHLL2: SHLL2(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHLL8: SHLL8(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHLL16: SHLL16(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHLR: SHLR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHLR2: SHLR2(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHLR8: SHLR8(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SHLR16: SHLR16(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SUB: SUB(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SUBC: SUBC(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_SUBV: SUBV(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_XOR_R: XOR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_XOR_I: XORI(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_XOR_M: XORM<debug, enableCache>(args), jumpToDelaySlot(); return 3;

    case OpcodeType::Delay_DT: DT(args), jumpToDelaySlot(); return 1;

    case OpcodeType::Delay_CLRMAC: CLRMAC(), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MACW: MACW<debug, enableCache>(args), jumpToDelaySlot(); return 2;
    case OpcodeType::Delay_MACL: MACL<debug, enableCache>(args), jumpToDelaySlot(); return 2;
    case OpcodeType::Delay_MUL: MULL(args), jumpToDelaySlot(); return 2;
    case OpcodeType::Delay_MULS: MULS(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_MULU: MULU(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_DMULS: DMULS(args), jumpToDelaySlot(); return 2;
    case OpcodeType::Delay_DMULU: DMULU(args), jumpToDelaySlot(); return 2;

    case OpcodeType::Delay_DIV0S: DIV0S(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_DIV0U: DIV0U(), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_DIV1: DIV1(args), jumpToDelaySlot(); return 1;

    case OpcodeType::Delay_CMP_EQ_I: CMPIM(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CMP_EQ_R: CMPEQ(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CMP_GE: CMPGE(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CMP_GT: CMPGT(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CMP_HI: CMPHI(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CMP_HS: CMPHS(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CMP_PL: CMPPL(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CMP_PZ: CMPPZ(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_CMP_STR: CMPSTR(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_TAS: TAS<debug, enableCache>(args), jumpToDelaySlot(); return 4;
    case OpcodeType::Delay_TST_R: TST(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_TST_I: TSTI(args), jumpToDelaySlot(); return 1;
    case OpcodeType::Delay_TST_M: TSTM<debug, enableCache>(args), jumpToDelaySlot(); return 3;

    case OpcodeType::IllegalSlot: EnterException<debug, enableCache>(xvSlotIllegalInstr); return 8;
    }

    util::unreachable();
}

template uint64 SH2::InterpretNext<false, false>();
template uint64 SH2::InterpretNext<false, true>();
template uint64 SH2::InterpretNext<true, false>();
template uint64 SH2::InterpretNext<true, true>();

// nop
FORCE_INLINE void SH2::NOP() {}

// sleep
FORCE_INLINE void SH2::SLEEP() {
    PC -= 2;

    if (SBYCR.SBY) {
        devlog::trace<grp::exec>(m_logPrefix, "Entering standby");

        // Initialize DMAC, FRT, WDT and SCI
        for (auto &ch : m_dmaChannels) {
            ch.WriteCHCR<false>(0);
        }
        DMAOR.Reset();
        FRT.Reset();
        WDT.Reset(false);
        // TODO: reset SCI

        // TODO: enter standby state
    } else {
        devlog::trace<grp::exec>(m_logPrefix, "Entering sleep");
        // TODO: enter sleep state
    }
}

// mov Rm, Rn
FORCE_INLINE void SH2::MOV(const DecodedArgs &args) {
    R[args.rn] = R[args.rm];
}

// mov.b @Rm, Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBL(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<8>(MemReadByte<enableCache>(R[args.rm]));
}

// mov.w @Rm, Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWL(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<16>(MemReadWord<enableCache>(R[args.rm]));
}

// mov.l @Rm, Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLL(const DecodedArgs &args) {
    R[args.rn] = MemReadLong<enableCache>(R[args.rm]);
}

// mov.b @(R0,Rm), Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBL0(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<8>(MemReadByte<enableCache>(R[args.rm] + R[0]));
}

// mov.w @(R0,Rm), Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWL0(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<16>(MemReadWord<enableCache>(R[args.rm] + R[0]));
}

// mov.l @(R0,Rm), Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLL0(const DecodedArgs &args) {
    R[args.rn] = MemReadLong<enableCache>(R[args.rm] + R[0]);
}

// mov.b @(disp,Rm), R0
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBL4(const DecodedArgs &args) {
    R[0] = bit::sign_extend<8>(MemReadByte<enableCache>(R[args.rm] + args.dispImm));
}

// mov.w @(disp,Rm), R0
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWL4(const DecodedArgs &args) {
    R[0] = bit::sign_extend<16>(MemReadWord<enableCache>(R[args.rm] + args.dispImm));
}

// mov.l @(disp,Rm), Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLL4(const DecodedArgs &args) {
    R[args.rn] = MemReadLong<enableCache>(R[args.rm] + args.dispImm);
}

// mov.b @(disp,GBR), R0
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBLG(const DecodedArgs &args) {
    R[0] = bit::sign_extend<8>(MemReadByte<enableCache>(GBR + args.dispImm));
}

// mov.w @(disp,GBR), R0
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWLG(const DecodedArgs &args) {
    R[0] = bit::sign_extend<16>(MemReadWord<enableCache>(GBR + args.dispImm));
}

// mov.l @(disp,GBR), R0
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLLG(const DecodedArgs &args) {
    R[0] = MemReadLong<enableCache>(GBR + args.dispImm);
}

// mov.b Rm, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBM(const DecodedArgs &args) {
    MemWriteByte<debug, enableCache>(R[args.rn] - 1, R[args.rm]);
    R[args.rn] -= 1;
}

// mov.w Rm, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWM(const DecodedArgs &args) {
    MemWriteWord<debug, enableCache>(R[args.rn] - 2, R[args.rm]);
    R[args.rn] -= 2;
}

// mov.l Rm, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLM(const DecodedArgs &args) {
    MemWriteLong<debug, enableCache>(R[args.rn] - 4, R[args.rm]);
    R[args.rn] -= 4;
}

// mov.b @Rm+, Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBP(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<8>(MemReadByte<enableCache>(R[args.rm]));
    if (args.rn != args.rm) {
        R[args.rm] += 1;
    }
}

// mov.w @Rm+, Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWP(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<16>(MemReadWord<enableCache>(R[args.rm]));
    if (args.rn != args.rm) {
        R[args.rm] += 2;
    }
}

// mov.l @Rm+, Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLP(const DecodedArgs &args) {
    R[args.rn] = MemReadLong<enableCache>(R[args.rm]);
    if (args.rn != args.rm) {
        R[args.rm] += 4;
    }
}

// mov.b Rm, @Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBS(const DecodedArgs &args) {
    MemWriteByte<debug, enableCache>(R[args.rn], R[args.rm]);
}

// mov.w Rm, @Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWS(const DecodedArgs &args) {
    MemWriteWord<debug, enableCache>(R[args.rn], R[args.rm]);
}

// mov.l Rm, @Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLS(const DecodedArgs &args) {
    MemWriteLong<debug, enableCache>(R[args.rn], R[args.rm]);
}

// mov.b Rm, @(R0,Rn)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBS0(const DecodedArgs &args) {
    MemWriteByte<debug, enableCache>(R[args.rn] + R[0], R[args.rm]);
}

// mov.w Rm, @(R0,Rn)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWS0(const DecodedArgs &args) {
    MemWriteWord<debug, enableCache>(R[args.rn] + R[0], R[args.rm]);
}

// mov.l Rm, @(R0,Rn)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLS0(const DecodedArgs &args) {
    MemWriteLong<debug, enableCache>(R[args.rn] + R[0], R[args.rm]);
}

// mov.b R0, @(disp,Rn)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBS4(const DecodedArgs &args) {
    MemWriteByte<debug, enableCache>(R[args.rn] + args.dispImm, R[0]);
}

// mov.w R0, @(disp,Rn)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWS4(const DecodedArgs &args) {
    MemWriteWord<debug, enableCache>(R[args.rn] + args.dispImm, R[0]);
}

// mov.l Rm, @(disp,Rn)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLS4(const DecodedArgs &args) {
    MemWriteLong<debug, enableCache>(R[args.rn] + args.dispImm, R[args.rm]);
}

// mov.b R0, @(disp,GBR)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVBSG(const DecodedArgs &args) {
    MemWriteByte<debug, enableCache>(GBR + args.dispImm, R[0]);
}

// mov.w R0, @(disp,GBR)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVWSG(const DecodedArgs &args) {
    MemWriteWord<debug, enableCache>(GBR + args.dispImm, R[0]);
}

// mov.l R0, @(disp,GBR)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MOVLSG(const DecodedArgs &args) {
    MemWriteLong<debug, enableCache>(GBR + args.dispImm, R[0]);
}

// mov #imm, Rn
FORCE_INLINE void SH2::MOVI(const DecodedArgs &args) {
    R[args.rn] = args.dispImm;
}

// mov.w @(disp,PC), Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE void SH2::MOVWI(const DecodedArgs &args) {
    const uint32 address = PC + args.dispImm + (delaySlot ? 2u : 0u);
    R[args.rn] = bit::sign_extend<16>(MemReadWord<enableCache>(address));
}

// mov.l @(disp,PC), Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE void SH2::MOVLI(const DecodedArgs &args) {
    const uint32 address = ((PC + (delaySlot ? 2u : 0u)) & ~3u) + args.dispImm;
    R[args.rn] = MemReadLong<enableCache>(address);
}

// mova @(disp,PC), R0
template <bool delaySlot>
FORCE_INLINE void SH2::MOVA(const DecodedArgs &args) {
    const uint32 address = ((PC + (delaySlot ? 2u : 0u)) & ~3u) + args.dispImm;
    R[0] = address;
}

// movt Rn
FORCE_INLINE void SH2::MOVT(const DecodedArgs &args) {
    R[args.rn] = SR.T;
}

// clrt
FORCE_INLINE void SH2::CLRT() {
    SR.T = 0;
}

// sett
FORCE_INLINE void SH2::SETT() {
    SR.T = 1;
}

// exts.b Rm, Rn
FORCE_INLINE void SH2::EXTSB(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<8>(R[args.rm]);
}

// exts.w Rm, Rn
FORCE_INLINE void SH2::EXTSW(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<16>(R[args.rm]);
}

// extu.b Rm, Rn
FORCE_INLINE void SH2::EXTUB(const DecodedArgs &args) {
    R[args.rn] = R[args.rm] & 0xFF;
}

// extu.w Rm, Rn
FORCE_INLINE void SH2::EXTUW(const DecodedArgs &args) {
    R[args.rn] = R[args.rm] & 0xFFFF;
}

// swap.b Rm, Rn
FORCE_INLINE void SH2::SWAPB(const DecodedArgs &args) {
    const uint32 tmp0 = R[args.rm] & 0xFFFF0000;
    const uint32 tmp1 = (R[args.rm] & 0xFF) << 8u;
    R[args.rn] = ((R[args.rm] >> 8u) & 0xFF) | tmp1 | tmp0;
}

// swap.w Rm, Rn
FORCE_INLINE void SH2::SWAPW(const DecodedArgs &args) {
    const uint32 tmp = R[args.rm] >> 16u;
    R[args.rn] = (R[args.rm] << 16u) | tmp;
}

// xtrct Rm, Rn
FORCE_INLINE void SH2::XTRCT(const DecodedArgs &args) {
    R[args.rn] = (R[args.rn] >> 16u) | (R[args.rm] << 16u);
}

// ldc Rm, GBR
FORCE_INLINE void SH2::LDCGBR(const DecodedArgs &args) {
    GBR = R[args.rm];
}

// ldc Rm, SR
FORCE_INLINE void SH2::LDCSR(const DecodedArgs &args) {
    SR.u32 = R[args.rm] & 0x000003F3;
}

// ldc Rm, VBR
FORCE_INLINE void SH2::LDCVBR(const DecodedArgs &args) {
    VBR = R[args.rm];
}

// ldc.l @Rm+, GBR
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::LDCMGBR(const DecodedArgs &args) {
    GBR = MemReadLong<enableCache>(R[args.rm]);
    R[args.rm] += 4;
}

// ldc.l @Rm+, SR
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::LDCMSR(const DecodedArgs &args) {
    SR.u32 = MemReadLong<enableCache>(R[args.rm]) & 0x000003F3;
    R[args.rm] += 4;
}

// ldc.l @Rm+, VBR
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::LDCMVBR(const DecodedArgs &args) {
    VBR = MemReadLong<enableCache>(R[args.rm]);
    R[args.rm] += 4;
}

// lds Rm, MACH
FORCE_INLINE void SH2::LDSMACH(const DecodedArgs &args) {
    MAC.H = R[args.rm];
}

// lds Rm, MACL
FORCE_INLINE void SH2::LDSMACL(const DecodedArgs &args) {
    MAC.L = R[args.rm];
}

// lds Rm, PR
FORCE_INLINE void SH2::LDSPR(const DecodedArgs &args) {
    PR = R[args.rm];
}

// lds.l @Rm+, MACH
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::LDSMMACH(const DecodedArgs &args) {
    MAC.H = MemReadLong<enableCache>(R[args.rm]);
    R[args.rm] += 4;
}

// lds.l @Rm+, MACL
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::LDSMMACL(const DecodedArgs &args) {
    MAC.L = MemReadLong<enableCache>(R[args.rm]);
    R[args.rm] += 4;
}

// lds.l @Rm+, PR
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::LDSMPR(const DecodedArgs &args) {
    PR = MemReadLong<enableCache>(R[args.rm]);
    R[args.rm] += 4;
}

// stc GBR, Rn
FORCE_INLINE void SH2::STCGBR(const DecodedArgs &args) {
    R[args.rn] = GBR;
}

// stc SR, Rn
FORCE_INLINE void SH2::STCSR(const DecodedArgs &args) {
    R[args.rn] = SR.u32;
}

// stc VBR, Rn
FORCE_INLINE void SH2::STCVBR(const DecodedArgs &args) {
    R[args.rn] = VBR;
}

// stc.l GBR, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::STCMGBR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    MemWriteLong<debug, enableCache>(R[args.rn], GBR);
}

// stc.l SR, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::STCMSR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    MemWriteLong<debug, enableCache>(R[args.rn], SR.u32);
}

// stc.l VBR, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::STCMVBR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    MemWriteLong<debug, enableCache>(R[args.rn], VBR);
}

// sts MACH, Rn
FORCE_INLINE void SH2::STSMACH(const DecodedArgs &args) {
    R[args.rn] = MAC.H;
}

// sts MACL, Rn
FORCE_INLINE void SH2::STSMACL(const DecodedArgs &args) {
    R[args.rn] = MAC.L;
}

// sts PR, Rn
FORCE_INLINE void SH2::STSPR(const DecodedArgs &args) {
    R[args.rn] = PR;
}

// sts.l MACH, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::STSMMACH(const DecodedArgs &args) {
    R[args.rn] -= 4;
    MemWriteLong<debug, enableCache>(R[args.rn], MAC.H);
}

// sts.l MACL, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::STSMMACL(const DecodedArgs &args) {
    R[args.rn] -= 4;
    MemWriteLong<debug, enableCache>(R[args.rn], MAC.L);
}

// sts.l PR, @-Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::STSMPR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    MemWriteLong<debug, enableCache>(R[args.rn], PR);
}

// add Rm, Rn
FORCE_INLINE void SH2::ADD(const DecodedArgs &args) {
    R[args.rn] += R[args.rm];
}

// add #imm, Rn
FORCE_INLINE void SH2::ADDI(const DecodedArgs &args) {
    R[args.rn] += args.dispImm;
}

// addc Rm, Rn
FORCE_INLINE void SH2::ADDC(const DecodedArgs &args) {
    const uint32 tmp1 = R[args.rn] + R[args.rm];
    const uint32 tmp0 = R[args.rn];
    R[args.rn] = tmp1 + SR.T;
    SR.T = (tmp0 > tmp1) || (tmp1 > R[args.rn]);
}

// addv Rm, Rn
FORCE_INLINE void SH2::ADDV(const DecodedArgs &args) {
    const bool dst = static_cast<sint32>(R[args.rn]) < 0;
    const bool src = static_cast<sint32>(R[args.rm]) < 0;

    R[args.rn] += R[args.rm];

    bool ans = static_cast<sint32>(R[args.rn]) < 0;
    ans ^= dst;
    SR.T = (src == dst) & ans;
}

// and Rm, Rn
FORCE_INLINE void SH2::AND(const DecodedArgs &args) {
    R[args.rn] &= R[args.rm];
}

// and #imm, R0
FORCE_INLINE void SH2::ANDI(const DecodedArgs &args) {
    R[0] &= args.dispImm;
}

// and.b #imm, @(R0,GBR)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::ANDM(const DecodedArgs &args) {
    uint8 tmp = MemReadByte<enableCache>(GBR + R[0]);
    tmp &= args.dispImm;
    MemWriteByte<debug, enableCache>(GBR + R[0], tmp);
}

// neg Rm, Rn
FORCE_INLINE void SH2::NEG(const DecodedArgs &args) {
    R[args.rn] = -R[args.rm];
}

// negc Rm, Rn
FORCE_INLINE void SH2::NEGC(const DecodedArgs &args) {
    const uint32 tmp = -R[args.rm];
    R[args.rn] = tmp - SR.T;
    SR.T = (0 < tmp) || (tmp < R[args.rn]);
}

// not Rm, Rn
FORCE_INLINE void SH2::NOT(const DecodedArgs &args) {
    R[args.rn] = ~R[args.rm];
}

// or Rm, Rn
FORCE_INLINE void SH2::OR(const DecodedArgs &args) {
    R[args.rn] |= R[args.rm];
}

// or #imm, R0
FORCE_INLINE void SH2::ORI(const DecodedArgs &args) {
    R[0] |= args.dispImm;
}

// or.b #imm, @(R0,GBR)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::ORM(const DecodedArgs &args) {
    uint8 tmp = MemReadByte<enableCache>(GBR + R[0]);
    tmp |= args.dispImm;
    MemWriteByte<debug, enableCache>(GBR + R[0], tmp);
}

// rotcl Rn
FORCE_INLINE void SH2::ROTCL(const DecodedArgs &args) {
    const bool tmp = R[args.rn] >> 31u;
    R[args.rn] = (R[args.rn] << 1u) | SR.T;
    SR.T = tmp;
}

// rotcr Rn
FORCE_INLINE void SH2::ROTCR(const DecodedArgs &args) {
    const bool tmp = R[args.rn] & 1u;
    R[args.rn] = (R[args.rn] >> 1u) | (SR.T << 31u);
    SR.T = tmp;
}

// rotl Rn
FORCE_INLINE void SH2::ROTL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] = (R[args.rn] << 1u) | SR.T;
}

// rotr Rn
FORCE_INLINE void SH2::ROTR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] = (R[args.rn] >> 1u) | (SR.T << 31u);
}

// shal Rn
FORCE_INLINE void SH2::SHAL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] <<= 1u;
}

// shar Rn
FORCE_INLINE void SH2::SHAR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] = static_cast<sint32>(R[args.rn]) >> 1;
}

// shll Rn
FORCE_INLINE void SH2::SHLL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] <<= 1u;
}

// shll2 Rn
FORCE_INLINE void SH2::SHLL2(const DecodedArgs &args) {
    R[args.rn] <<= 2u;
}

// shll8 Rn
FORCE_INLINE void SH2::SHLL8(const DecodedArgs &args) {
    R[args.rn] <<= 8u;
}

// shll16 Rn
FORCE_INLINE void SH2::SHLL16(const DecodedArgs &args) {
    R[args.rn] <<= 16u;
}

// shlr Rn
FORCE_INLINE void SH2::SHLR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] >>= 1u;
}

// shlr2 Rn
FORCE_INLINE void SH2::SHLR2(const DecodedArgs &args) {
    R[args.rn] >>= 2u;
}

// shlr8 Rn
FORCE_INLINE void SH2::SHLR8(const DecodedArgs &args) {
    R[args.rn] >>= 8u;
}

// shlr16 Rn
FORCE_INLINE void SH2::SHLR16(const DecodedArgs &args) {
    R[args.rn] >>= 16u;
}

// sub Rm, Rn
FORCE_INLINE void SH2::SUB(const DecodedArgs &args) {
    R[args.rn] -= R[args.rm];
}

// subc Rm, Rn
FORCE_INLINE void SH2::SUBC(const DecodedArgs &args) {
    const uint32 tmp1 = R[args.rn] - R[args.rm];
    const uint32 tmp0 = R[args.rn];
    R[args.rn] = tmp1 - SR.T;
    SR.T = (tmp0 < tmp1) || (tmp1 < R[args.rn]);
}

// subv Rm, Rn
FORCE_INLINE void SH2::SUBV(const DecodedArgs &args) {

    const bool dst = static_cast<sint32>(R[args.rn]) < 0;
    const bool src = static_cast<sint32>(R[args.rm]) < 0;

    R[args.rn] -= R[args.rm];

    bool ans = static_cast<sint32>(R[args.rn]) < 0;
    ans ^= dst;
    SR.T = (src != dst) & ans;
}

// xor Rm, Rn
FORCE_INLINE void SH2::XOR(const DecodedArgs &args) {
    R[args.rn] ^= R[args.rm];
}

// xor #imm, R0
FORCE_INLINE void SH2::XORI(const DecodedArgs &args) {
    R[0] ^= args.dispImm;
}

// xor.b #imm, @(R0,GBR)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::XORM(const DecodedArgs &args) {
    uint8 tmp = MemReadByte<enableCache>(GBR + R[0]);
    tmp ^= args.dispImm;
    MemWriteByte<debug, enableCache>(GBR + R[0], tmp);
}

// dt Rn
FORCE_INLINE void SH2::DT(const DecodedArgs &args) {
    R[args.rn]--;
    SR.T = R[args.rn] == 0;
}

// clrmac
FORCE_INLINE void SH2::CLRMAC() {
    MAC.u64 = 0;
}

// mac.w @Rm+, @Rn+
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MACW(const DecodedArgs &args) {
    const sint32 op1 = static_cast<sint16>(MemReadWord<enableCache>(R[args.rm]));
    R[args.rm] += 2;
    const sint32 op2 = static_cast<sint16>(MemReadWord<enableCache>(R[args.rn]));
    R[args.rn] += 2;

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
        MAC.u64 += mul;
    }
}

// mac.l @Rm+, @Rn+
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::MACL(const DecodedArgs &args) {
    const sint64 op1 = static_cast<sint64>(static_cast<sint32>(MemReadLong<enableCache>(R[args.rm])));
    R[args.rm] += 4;
    const sint64 op2 = static_cast<sint64>(static_cast<sint32>(MemReadLong<enableCache>(R[args.rn])));
    R[args.rn] += 4;

    const sint64 mul = op1 * op2;
    sint64 result = mul + MAC.u64;
    if (SR.S && result > 0x00007FFFFFFFFFFFull && result < 0xFFFF800000000000ull) {
        if (static_cast<sint32>(op1 ^ op2) < 0) {
            result = 0xFFFF800000000000ull;
        } else {
            result = 0x00007FFFFFFFFFFFull;
        }
    }
    MAC.u64 = result;
}

// mul.l Rm, Rn
FORCE_INLINE void SH2::MULL(const DecodedArgs &args) {
    MAC.L = R[args.rm] * R[args.rn];
}

// muls.w Rm, Rn
FORCE_INLINE void SH2::MULS(const DecodedArgs &args) {
    MAC.L = bit::sign_extend<16>(R[args.rm]) * bit::sign_extend<16>(R[args.rn]);
}

// mulu.w Rm, Rn
FORCE_INLINE void SH2::MULU(const DecodedArgs &args) {
    auto cast = [](uint32 val) { return static_cast<uint32>(static_cast<uint16>(val)); };
    MAC.L = cast(R[args.rm]) * cast(R[args.rn]);
}

// dmuls.l Rm, Rn
FORCE_INLINE void SH2::DMULS(const DecodedArgs &args) {
    auto cast = [](uint32 val) { return static_cast<sint64>(static_cast<sint32>(val)); };
    MAC.u64 = cast(R[args.rm]) * cast(R[args.rn]);
}

// dmulu.l Rm, Rn
FORCE_INLINE void SH2::DMULU(const DecodedArgs &args) {
    MAC.u64 = static_cast<uint64>(R[args.rm]) * static_cast<uint64>(R[args.rn]);
}

// div0s r{}, Rm, Rn
FORCE_INLINE void SH2::DIV0S(const DecodedArgs &args) {
    SR.M = static_cast<sint32>(R[args.rm]) < 0;
    SR.Q = static_cast<sint32>(R[args.rn]) < 0;
    SR.T = SR.M != SR.Q;
}

// div0u
FORCE_INLINE void SH2::DIV0U() {
    SR.M = 0;
    SR.Q = 0;
    SR.T = 0;
}

// div1 Rm, Rn
FORCE_INLINE void SH2::DIV1(const DecodedArgs &args) {
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
}

// cmp/eq #imm, R0
FORCE_INLINE void SH2::CMPIM(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[0]) == args.dispImm;
}

// cmp/eq Rm, Rn
FORCE_INLINE void SH2::CMPEQ(const DecodedArgs &args) {
    SR.T = R[args.rn] == R[args.rm];
}

// cmp/ge Rm, Rn
FORCE_INLINE void SH2::CMPGE(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) >= static_cast<sint32>(R[args.rm]);
}

// cmp/gt Rm, Rn
FORCE_INLINE void SH2::CMPGT(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) > static_cast<sint32>(R[args.rm]);
}

// cmp/hi Rm, Rn
FORCE_INLINE void SH2::CMPHI(const DecodedArgs &args) {
    SR.T = R[args.rn] > R[args.rm];
}

// cmp/hs Rm, Rn
FORCE_INLINE void SH2::CMPHS(const DecodedArgs &args) {
    SR.T = R[args.rn] >= R[args.rm];
}

// cmp/pl Rn
FORCE_INLINE void SH2::CMPPL(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) > 0;
}

// cmp/pz Rn
FORCE_INLINE void SH2::CMPPZ(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) >= 0;
}

// cmp/str Rm, Rn
FORCE_INLINE void SH2::CMPSTR(const DecodedArgs &args) {
    const uint32 tmp = R[args.rm] ^ R[args.rn];
    const uint8 hh = tmp >> 24u;
    const uint8 hl = tmp >> 16u;
    const uint8 lh = tmp >> 8u;
    const uint8 ll = tmp >> 0u;
    SR.T = !(hh && hl && lh && ll);
}

// tas.b @Rn
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::TAS(const DecodedArgs &args) {
    // TODO: enable bus lock on this read
    const uint8 tmp = MemReadByte<enableCache>(R[args.rn]);
    SR.T = tmp == 0;
    // TODO: disable bus lock on this write
    MemWriteByte<debug, enableCache>(R[args.rn], tmp | 0x80);
}

// tst Rm, Rn
FORCE_INLINE void SH2::TST(const DecodedArgs &args) {
    SR.T = (R[args.rn] & R[args.rm]) == 0;
}

// tst #imm, R0
FORCE_INLINE void SH2::TSTI(const DecodedArgs &args) {
    SR.T = (R[0] & args.dispImm) == 0;
}

// tst.b #imm, @(R0,GBR)
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::TSTM(const DecodedArgs &args) {
    const uint8 tmp = MemReadByte<enableCache>(GBR + R[0]);
    SR.T = (tmp & args.dispImm) == 0;
}

// bf <label>
template <bool debug, bool enableCache>
FORCE_INLINE uint64 SH2::BF(const DecodedArgs &args) {
    if (!SR.T) {
        PC += args.dispImm;
        FillPipeline<enableCache>();
        return 3;
    } else {
        PC += 2;
        return 1;
    }
}

// bf/s <label>
FORCE_INLINE uint64 SH2::BFS(const DecodedArgs &args) {
    if (!SR.T) {
        PC += args.dispImm;
        m_delaySlot = true;
    } else {
        PC += 2;
    }
    return !SR.T ? 2 : 1;
}

// bt <label>
template <bool debug, bool enableCache>
FORCE_INLINE uint64 SH2::BT(const DecodedArgs &args) {
    if (SR.T) {
        PC += args.dispImm;
        FillPipeline<enableCache>();
        return 3;
    } else {
        PC += 2;
        return 1;
    }
}

// bt/s <label>
FORCE_INLINE uint64 SH2::BTS(const DecodedArgs &args) {
    if (SR.T) {
        PC += args.dispImm;
        m_delaySlot = true;
    } else {
        PC += 2;
    }
    return SR.T ? 2 : 1;
}

// bra <label>
FORCE_INLINE void SH2::BRA(const DecodedArgs &args) {
    PC += args.dispImm;
    m_delaySlot = true;
}

// braf Rm
FORCE_INLINE void SH2::BRAF(const DecodedArgs &args) {
    PC += R[args.rm];
    m_delaySlot = true;
}

// bsr <label>
FORCE_INLINE void SH2::BSR(const DecodedArgs &args) {
    PR = PC;
    PC += args.dispImm;
    m_delaySlot = true;
}

// bsrf Rm
FORCE_INLINE void SH2::BSRF(const DecodedArgs &args) {
    PR = PC;
    PC += R[args.rm];
    m_delaySlot = true;
}

// jmp @Rm
FORCE_INLINE void SH2::JMP(const DecodedArgs &args) {
    PC = R[args.rm];
    m_delaySlot = true;
}

// jsr @Rm
FORCE_INLINE void SH2::JSR(const DecodedArgs &args) {
    PR = PC;
    PC = R[args.rm];
    m_delaySlot = true;
}

// trapa #imm
template <bool debug, bool enableCache>
FORCE_INLINE void SH2::TRAPA(const DecodedArgs &args) {
    R[15] -= 4;
    MemWriteLong<debug, enableCache>(R[15], SR.u32);
    R[15] -= 4;
    MemWriteLong<debug, enableCache>(R[15], PC - 2);
    PC = MemReadLong<enableCache>(VBR + args.dispImm);
    FillPipeline<enableCache>();
}

template <bool debug, bool enableCache>
FORCE_INLINE void SH2::RTE() {
    // rte
    PC = MemReadLong<enableCache>(R[15]);
    m_delaySlot = true;
    R[15] += 4;
    SR.u32 = MemReadLong<enableCache>(R[15]) & 0x000003F3;
    R[15] += 4;
    devlog::trace<grp::exec>(m_logPrefix, "Returning from interrupt handler, PC -> {:08X}", PC);
}

// rts
FORCE_INLINE void SH2::RTS() {
    PC = PR;
    m_delaySlot = true;
}

// -----------------------------------------------------------------------------
// Probe implementation

SH2::Probe::Probe(SH2 &sh2)
    : m_sh2(sh2) {}

uint16 SH2::Probe::FetchInstruction(uint32 address, bool bypassCache) const {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        return m_sh2.FetchInstruction<true>(address);
    } else {
        return m_sh2.FetchInstruction<false>(address);
    }
}

uint8 SH2::Probe::MemReadByte(uint32 address, bool bypassCache) const {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        return m_sh2.MemReadByte<true>(address);
    } else {
        return m_sh2.MemReadByte<false>(address);
    }
}

uint16 SH2::Probe::MemReadWord(uint32 address, bool bypassCache) const {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        return m_sh2.MemReadWord<true>(address);
    } else {
        return m_sh2.MemReadWord<false>(address);
    }
}

uint32 SH2::Probe::MemReadLong(uint32 address, bool bypassCache) const {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        return m_sh2.MemReadLong<true>(address);
    } else {
        return m_sh2.MemReadLong<false>(address);
    }
}

void SH2::Probe::MemWriteByte(uint32 address, uint8 value, bool bypassCache) {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        m_sh2.MemWriteByte<false, true>(address, value);
    } else {
        m_sh2.MemWriteByte<false, false>(address, value);
    }
}

void SH2::Probe::MemWriteWord(uint32 address, uint16 value, bool bypassCache) {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        m_sh2.MemWriteWord<false, true>(address, value);
    } else {
        m_sh2.MemWriteWord<false, false>(address, value);
    }
}

void SH2::Probe::MemWriteLong(uint32 address, uint32 value, bool bypassCache) {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        m_sh2.MemWriteLong<false, true>(address, value);
    } else {
        m_sh2.MemWriteLong<false, false>(address, value);
    }
}

uint16 SH2::Probe::PeekInstruction(uint32 address, bool bypassCache) const {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        return m_sh2.PeekInstruction<true>(address);
    } else {
        return m_sh2.PeekInstruction<false>(address);
    }
}

uint8 SH2::Probe::MemPeekByte(uint32 address, bool bypassCache) const {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        return m_sh2.MemPeekByte<true>(address);
    } else {
        return m_sh2.MemPeekByte<false>(address);
    }
}

uint16 SH2::Probe::MemPeekWord(uint32 address, bool bypassCache) const {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        return m_sh2.MemPeekWord<true>(address);
    } else {
        return m_sh2.MemPeekWord<false>(address);
    }
}

uint32 SH2::Probe::MemPeekLong(uint32 address, bool bypassCache) const {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        return m_sh2.MemPeekLong<true>(address);
    } else {
        return m_sh2.MemPeekLong<false>(address);
    }
}

void SH2::Probe::MemPokeByte(uint32 address, uint8 value, bool bypassCache) {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        m_sh2.MemPokeByte<true>(address, value);
    } else {
        m_sh2.MemPokeByte<false>(address, value);
    }
}

void SH2::Probe::MemPokeWord(uint32 address, uint16 value, bool bypassCache) {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        m_sh2.MemPokeWord<true>(address, value);
    } else {
        m_sh2.MemPokeWord<false>(address, value);
    }
}

void SH2::Probe::MemPokeLong(uint32 address, uint32 value, bool bypassCache) {
    if (m_sh2.m_systemFeatures.emulateSH2Cache && !bypassCache) {
        m_sh2.MemPokeLong<true>(address, value);
    } else {
        m_sh2.MemPokeLong<false>(address, value);
    }
}

bool SH2::Probe::IsInDelaySlot() const {
    return m_sh2.m_delaySlot;
}

void SH2::Probe::ExecuteDiv32() {
    m_sh2.ExecuteDiv32<true>();
}

void SH2::Probe::ExecuteDiv64() {
    m_sh2.ExecuteDiv64<true>();
}

} // namespace ymir::sh2
