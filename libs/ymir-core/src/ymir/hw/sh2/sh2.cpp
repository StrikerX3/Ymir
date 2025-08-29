#include <ymir/hw/sh2/sh2.hpp>

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

namespace ymir::sh2 {

// -----------------------------------------------------------------------------
// Dev log groups

namespace grp {

    // Hierarchy:
    //
    // base
    //   exec
    //     exec_dump
    //   intr
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

    struct intr : public base {
        // static constexpr bool enabled = true;
        static constexpr std::string Name(std::string_view prefix) {
            return std::string(prefix) + "-Interrupt";
        }
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

SH2::SH2(core::Scheduler &scheduler, sys::Bus &bus, bool master, const sys::SystemFeatures &systemFeatures)
    : m_scheduler(scheduler)
    , m_bus(bus)
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

    PC = MemReadLong<false>(0x00000000);
    R[15] = MemReadLong<false>(0x00000004);

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
    m_sleep = false;

    DIVU.Reset();
    FRT.Reset();
    INTC.Reset();
    m_intrPending = false;

    m_delaySlotTarget = 0;
    m_delaySlot = false;

    m_cache.Reset();
}

void SH2::MapMemory(sys::Bus &bus) {
    const uint32 addressOffset = !BCR1.MASTER * 0x80'0000;

    // Map MINIT/SINIT area
    bus.MapNormal(
        0x100'0000 + addressOffset, 0x17F'FFFF + addressOffset, this,

        // Reads are prohibited
        [](uint32 address, void *) -> uint8 { return 0; }, [](uint32 address, void *) -> uint16 { return 0; },
        [](uint32 address, void *) -> uint32 { return 0; },

        // Writes trigger FRT ICI
        // - 8-bit writes only work on odd addresses
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
FLATTEN uint64 SH2::Advance(uint64 cycles, uint64 spilloverCycles) {
    m_cyclesExecuted = spilloverCycles;
    AdvanceWDT<false>();
    AdvanceFRT<false>();

    if constexpr (debug) {
        if (m_debugSuspend) {
            m_cyclesExecuted = cycles;
            return m_cyclesExecuted;
        }
    }
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
        // [[maybe_unused]] uint32 prevPC = PC; // debug aid

        // TODO: choose between interpreter (cached or uncached) and JIT recompiler
        m_cyclesExecuted += InterpretNext<debug, enableCache>();

        // If PC is not in any of these places, something went horribly wrong

        // Address bits 28 and 27 are disconnected and games generally don't use these mirrors.
        // Might not always be horribly wrong, but is highly likely to be a bad jump.
        // Unaligned addresses (bit 0 set) are a sign of potential memory corruption.
        YMIR_DEV_ASSERT((PC & 0x18000001) == 0);
        // PC should be in the cached and uncached spaces or the cache data array areas.
        // Anywhere else is highly suspicious or outright forbidden by the CPU.
        YMIR_DEV_ASSERT((PC >> 29u) == 0b000 || (PC >> 29u) == 0b001 || (PC >> 29u) == 0b100 || (PC >> 29u) == 0b101 ||
                        (PC >> 29u) == 0b110);

        // Check for breakpoints in debug tracing mode
        if constexpr (debug) {
            if (m_debugBreakMgr) {
                if (CheckBreakpoint()) {
                    break;
                }

                const uint16 instr = MemRead<uint16, true, true, enableCache>(PC);
                const auto &mem = DecodeTable::s_instance.mem[instr];
                if (CheckWatchpoints(mem)) {
                    break;
                }

                // Bail out if debug break was raised on watchpoints or other cases
                // TODO: probably needs to be an internal signal instead, otherwise it breaks SSH2 cycle counting
                /*if (m_debugBreakMgr->IsDebugBreakRaised()) {
                    break;
                }*/
            }
        }

        if constexpr (devlog::debug_enabled<grp::exec_dump>) {
            // Dump stack trace on SYS_EXECDMP
            if ((PC & 0x7FFFFFF) == config::sysExecDumpAddress) {
                devlog::debug<grp::exec_dump>(m_logPrefix, "SYS_EXECDMP triggered");
                // TODO: trace event
            }
        }
    }
    return m_cyclesExecuted;
}

template uint64 SH2::Advance<false, false>(uint64, uint64);
template uint64 SH2::Advance<false, true>(uint64, uint64);
template uint64 SH2::Advance<true, false>(uint64, uint64);
template uint64 SH2::Advance<true, true>(uint64, uint64);

template <bool debug, bool enableCache>
FLATTEN uint64 SH2::Step() {
    m_cyclesExecuted = 0; // so that AdvanceWDT/FRT sync to the scheduler time
    AdvanceWDT<false>();
    AdvanceFRT<false>();
    m_cyclesExecuted = InterpretNext<debug, enableCache>();
    return m_cyclesExecuted;
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
    state.delaySlotTarget = m_delaySlotTarget;
    state.delaySlot = m_delaySlot;

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
    state.wdt.busValue = m_WDTBusValue;
    DIVU.SaveState(state.divu);
    FRT.SaveState(state.frt);
    INTC.SaveState(state.intc);
    m_cache.SaveState(state.cache);
    state.SBYCR = SBYCR.u8;
    state.sleep = m_sleep;
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
    m_delaySlotTarget = state.delaySlotTarget;
    m_delaySlot = state.delaySlot;

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
    m_WDTBusValue = state.wdt.busValue;
    DIVU.LoadState(state.divu);
    FRT.LoadState(state.frt);
    INTC.LoadState(state.intc);
    m_cache.LoadState(state.cache);
    SBYCR.u8 = state.SBYCR;
    m_sleep = state.sleep;

    m_intrPending = !m_delaySlot && INTC.pending.level > SR.ILevel;
}

// -----------------------------------------------------------------------------
// Memory accessors

template <mem_primitive T, bool instrFetch, bool peek, bool enableCache>
T SH2::MemRead(uint32 address) {
    static constexpr uint32 kAddressMask = ~(static_cast<uint32>(sizeof(T)) - 1u);

    const uint32 partition = (address >> 29u) & 0b111;
    if (address & ~kAddressMask) {
        if constexpr (!peek) {
            devlog::trace<grp::mem>(m_logPrefix, "WARNING: misaligned {}-bit read from {:08X}", sizeof(T) * 8, address);
            // TODO: raise CPU address error due to misaligned access
            // - might have to store data in a class member instead of returning
        }
        address &= kAddressMask;
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
                                util::WriteNE<uint32>(&entry.line[way][addressInc], memValue);
                            }
                        }
                    }
                }

                // If way is valid, fetch from cache
                if (IsValidCacheWay(way)) {
                    const uint32 byte = bit::extract<0, 3>(address) ^ (4 - sizeof(T));
                    const T value = util::ReadNE<T>(&entry.line[way][byte]);
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
    case 0b100: [[fallthrough]];
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
    static constexpr uint32 kAddressMask = ~(static_cast<uint32>(sizeof(T)) - 1u);

    const uint32 partition = address >> 29u;
    if (address & ~kAddressMask) {
        if constexpr (!poke) {
            devlog::trace<grp::mem>(m_logPrefix, "WARNING: misaligned {}-bit write to {:08X} = {:X}", sizeof(T) * 8,
                                    address, value);
            // TODO: address error (misaligned access)
        }
        address &= kAddressMask;
    }

    switch (partition) {
    case 0b000: // cache
        if constexpr (enableCache) {
            if (m_cache.CCR.CE) {
                auto &entry = m_cache.GetEntry(address);
                const uint8 way = entry.FindWay(address);
                if (IsValidCacheWay(way)) {
                    const uint32 byte = bit::extract<0, 3>(address) ^ (4 - sizeof(T));
                    util::WriteNE<T>(&entry.line[way][byte], value);
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
FLATTEN FORCE_INLINE uint16 SH2::FetchInstruction(uint32 address) {
    return MemRead<uint16, true, false, enableCache>(address);
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
FLATTEN_EX FORCE_INLINE_EX uint16 SH2::PeekInstruction(uint32 address) {
    return MemRead<uint16, true, true, enableCache>(address);
}

template <bool enableCache>
FLATTEN_EX FORCE_INLINE_EX uint8 SH2::MemPeekByte(uint32 address) {
    return MemRead<uint8, false, true, enableCache>(address);
}

template <bool enableCache>
FLATTEN_EX FORCE_INLINE_EX uint16 SH2::MemPeekWord(uint32 address) {
    return MemRead<uint16, false, true, enableCache>(address);
}

template <bool enableCache>
FLATTEN_EX FORCE_INLINE_EX uint32 SH2::MemPeekLong(uint32 address) {
    return MemRead<uint32, false, true, enableCache>(address);
}

template <bool enableCache>
FLATTEN_EX FORCE_INLINE_EX void SH2::MemPokeByte(uint32 address, uint8 value) {
    MemWrite<uint8, true, false, enableCache>(address, value);
}

template <bool enableCache>
FLATTEN_EX FORCE_INLINE_EX void SH2::MemPokeWord(uint32 address, uint16 value) {
    MemWrite<uint16, true, false, enableCache>(address, value);
}

template <bool enableCache>
FLATTEN_EX FORCE_INLINE_EX void SH2::MemPokeLong(uint32 address, uint32 value) {
    MemWrite<uint32, true, false, enableCache>(address, value);
}

template <mem_primitive T>
/*FLATTEN_EX FORCE_INLINE_EX*/ T SH2::OpenBusSeqRead(uint32 address) {
    if constexpr (std::is_same_v<T, uint8>) {
        return (address & 1u) * ((address >> 1u) & 0x7);
        // return OpenBusSeqRead<uint16>(address) >> (((address & 1) ^ 1) * 8);
    } else if constexpr (std::is_same_v<T, uint16>) {
        return (address >> 1u) & 0x7;
    } else if constexpr (std::is_same_v<T, uint32>) {
        return (OpenBusSeqRead<uint16>(address + 1) << 16u) | OpenBusSeqRead<uint16>(address);
    }
    util::unreachable();
}

template <bool enableCache>
FORCE_INLINE uint64 SH2::AccessCycles(uint32 address) {
    // TODO: might need to distinguish between different sizes
    const uint32 partition = (address >> 29u) & 0b111;
    switch (partition) {
    case 0b000: // cache
        if constexpr (enableCache) {
            // TODO: check for cache hit/miss, compute timings for cache miss (4x uint32 fetches)
            return 1;
        }
        [[fallthrough]];
    case 0b001: [[fallthrough]];
    case 0b101: return 1; // cache-through  (TODO: get from Bus; use a table)
    case 0b010: return 1; // associative purge
    case 0b011: return 1; // cache address array
    case 0b100: [[fallthrough]];
    case 0b110: return 1; // cache data array
    case 0b111: return 4; // I/O area
    }

    util::unreachable();
}

// -----------------------------------------------------------------------------
// On-chip peripherals

template <mem_primitive T, bool peek>
/*FLATTEN_EX FORCE_INLINE_EX*/ T SH2::OnChipRegRead(uint32 address) {
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
    case 0x11:
        if constexpr (!peek) {
            AdvanceFRT<false>();
        }
        return FRT.ReadFTCSR();
    case 0x12:
        if constexpr (!peek) {
            AdvanceFRT<false>();
        }
        return FRT.ReadFRCH<peek>();
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

    case 0x80: [[fallthrough]];
    case 0x88:
        if constexpr (peek) {
            return WDT.ReadWTCSR();
        } else {
            AdvanceWDT<false>();
            return m_WDTBusValue = WDT.ReadWTCSR();
        }

    case 0x81: [[fallthrough]];
    case 0x89:
        if constexpr (peek) {
            return WDT.ReadWTCNT();
        } else {
            AdvanceWDT<false>();
            return m_WDTBusValue = WDT.ReadWTCNT();
        }

    case 0x83: [[fallthrough]];
    case 0x8B:
        if constexpr (peek) {
            return WDT.ReadRSTCSR();
        } else {
            AdvanceWDT<false>();
            return m_WDTBusValue = WDT.ReadRSTCSR();
        }

    case 0x82: [[fallthrough]];
    case 0x85: [[fallthrough]];
    case 0x86: [[fallthrough]];
    case 0x87: [[fallthrough]];
    case 0x8A: [[fallthrough]];
    case 0x8D: [[fallthrough]];
    case 0x8E: [[fallthrough]];
    case 0x8F: return 0xFF;
    case 0x84: [[fallthrough]];
    case 0x8C: return m_WDTBusValue;

    case 0x91: return SBYCR.u8;

    case 0x92: [[fallthrough]];
    case 0x93: [[fallthrough]];
    case 0x94: [[fallthrough]];
    case 0x95: [[fallthrough]];
    case 0x96: [[fallthrough]];
    case 0x97: [[fallthrough]];
    case 0x98: [[fallthrough]];
    case 0x99: [[fallthrough]];
    case 0x9A: [[fallthrough]];
    case 0x9B: [[fallthrough]];
    case 0x9C: [[fallthrough]];
    case 0x9D: [[fallthrough]];
    case 0x9E: [[fallthrough]];
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
        switch (address) {
        case 0x82: [[fallthrough]];
        case 0x85: [[fallthrough]];
        case 0x86: [[fallthrough]];
        case 0x87: [[fallthrough]];
        case 0x8A: [[fallthrough]];
        case 0x8D: [[fallthrough]];
        case 0x8E: [[fallthrough]];
        case 0x8F:
            if constexpr (!peek) {
                m_WDTBusValue = 0xFF;
            }
            return 0xFFFF;
        case 0x84: [[fallthrough]];
        case 0x8C: return (m_WDTBusValue << 8u) | m_WDTBusValue;

        case 0xE0: return INTC.ReadICR();
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
    case 0x100: [[fallthrough]];
    case 0x120: return DIVU.DVSR;

    case 0x104: [[fallthrough]];
    case 0x124: return DIVU.DVDNT;

    case 0x108: [[fallthrough]];
    case 0x128: return DIVU.DVCR.Read();

    case 0x10C: [[fallthrough]];
    case 0x12C: return DIVU.VCRDIV;

    case 0x110: [[fallthrough]];
    case 0x130: return DIVU.DVDNTH;

    case 0x114: [[fallthrough]];
    case 0x134: return DIVU.DVDNTL;

    case 0x118: [[fallthrough]];
    case 0x138: return DIVU.DVDNTUH;

    case 0x11C: [[fallthrough]];
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
/*FLATTEN_EX FORCE_INLINE_EX*/ void SH2::OnChipRegWrite(uint32 address, T value) {
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
        case 0x80: [[fallthrough]];
        case 0x88: WDT.WriteWTCSR<poke>(value); break;

        case 0x81: [[fallthrough]];
        case 0x89: WDT.WriteWTCNT(value); break;

        case 0x83: [[fallthrough]];
        case 0x8B: WDT.WriteRSTCSR<poke>(value); break;

        case 0x93: [[fallthrough]];
        case 0x94: [[fallthrough]];
        case 0x95: [[fallthrough]];
        case 0x96: [[fallthrough]];
        case 0x97: [[fallthrough]];
        case 0x98: [[fallthrough]];
        case 0x99: [[fallthrough]];
        case 0x9A: [[fallthrough]];
        case 0x9B: [[fallthrough]];
        case 0x9C: [[fallthrough]];
        case 0x9D: [[fallthrough]];
        case 0x9E: [[fallthrough]];
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
        if constexpr (!poke) {
            AdvanceFRT<true>();
        }
        FRT.WriteFTCSR<poke>(value);
        if (INTC.pending.source == InterruptSource::FRT_OVI || INTC.pending.source == InterruptSource::FRT_OCI ||
            INTC.pending.source == InterruptSource::FRT_ICI) {
            RecalcInterrupts();
        }
        break;
    case 0x12: FRT.WriteFRCH<poke>(value); break;
    case 0x13:
        if constexpr (!poke) {
            AdvanceFRT<true>();
        }
        FRT.WriteFRCL<poke>(value);
        break;
    case 0x14: FRT.WriteOCRH<poke>(value); break;
    case 0x15: FRT.WriteOCRL<poke>(value); break;
    case 0x16:
        if constexpr (!poke) {
            AdvanceFRT<true>();
        }
        FRT.WriteTCR(value);
        break;
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

    case 0x80: [[fallthrough]]; // WDT registers only accept 16-bit writes
    case 0x81: [[fallthrough]];
    case 0x82: [[fallthrough]];
    case 0x83: [[fallthrough]];
    case 0x84: [[fallthrough]];
    case 0x85: [[fallthrough]];
    case 0x86: [[fallthrough]];
    case 0x87: [[fallthrough]];
    case 0x88: [[fallthrough]];
    case 0x89: [[fallthrough]];
    case 0x8A: [[fallthrough]];
    case 0x8B: [[fallthrough]];
    case 0x8C: [[fallthrough]];
    case 0x8D: [[fallthrough]];
    case 0x8E: [[fallthrough]];
    case 0x8F:
        if constexpr (!poke) {
            m_WDTBusValue = value;
        }
        break;

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

    case 0x80: [[fallthrough]];
    case 0x88:
        if constexpr (!poke) {
            m_WDTBusValue = value;
        }
        if ((value >> 8u) == 0x5A) {
            if constexpr (!poke) {
                AdvanceWDT<true>();
            }
            WDT.WriteWTCNT(value);
        } else if ((value >> 8u) == 0xA5) {
            if constexpr (!poke) {
                AdvanceWDT<true>();
            }
            WDT.WriteWTCSR<poke>(value);
            if (!WDT.WTCSR.TME || !WDT.WTCSR.OVF) {
                LowerInterrupt(InterruptSource::WDT_ITI);
            }
        }
        break;

    case 0x82: [[fallthrough]];
    case 0x8A:
        if constexpr (!poke) {
            m_WDTBusValue = value;
        }
        if ((value >> 8u) == 0x5A) {
            WDT.WriteRSTE_RSTS(value);
        } else if ((value >> 8u) == 0xA5) {
            WDT.WriteWOVF<poke>(value);
        }
        break;

    case 0x81: [[fallthrough]];
    case 0x83: [[fallthrough]];
    case 0x85: [[fallthrough]];
    case 0x86: [[fallthrough]];
    case 0x87: [[fallthrough]];
    case 0x89: [[fallthrough]];
    case 0x8B: [[fallthrough]];
    case 0x8D: [[fallthrough]];
    case 0x8E: [[fallthrough]];
    case 0x8F:
        if constexpr (!poke) {
            m_WDTBusValue = value;
        }
        break;
    case 0x84: [[fallthrough]];
    case 0x8C:
        if constexpr (!poke) {
            m_WDTBusValue = value >> 8u;
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
    case 0x12C:
        INTC.SetVector(InterruptSource::DIVU_OVFI, bit::extract<0, 6>(value));
        DIVU.VCRDIV = value;
        break;

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
            if (!DMAOR.DME || !m_dmaChannels[0].xferEnded || !m_dmaChannels[0].irqEnable) {
                LowerInterrupt(InterruptSource::DMAC0_XferEnd);
            }
        }
        break;

    case 0x190: m_dmaChannels[1].srcAddress = value; break;
    case 0x194: m_dmaChannels[1].dstAddress = value; break;
    case 0x198: m_dmaChannels[1].xferCount = bit::extract<0, 23>(value); break;
    case 0x19C:
        m_dmaChannels[1].WriteCHCR<poke>(value);
        if constexpr (!poke) {
            RunDMAC<debug, enableCache>(1); // TODO: should be scheduled
            if (!DMAOR.DME || !m_dmaChannels[1].xferEnded || !m_dmaChannels[1].irqEnable) {
                LowerInterrupt(InterruptSource::DMAC1_XferEnd);
            }
        }
        break;

    case 0x1A0: INTC.SetVector(InterruptSource::DMAC0_XferEnd, bit::extract<0, 6>(value)); break;
    case 0x1A8: INTC.SetVector(InterruptSource::DMAC1_XferEnd, bit::extract<0, 6>(value)); break;

    case 0x1B0:
        DMAOR.Write<poke>(value);
        if constexpr (!poke) {
            RunDMAC<debug, enableCache>(0); // TODO: should be scheduled
            RunDMAC<debug, enableCache>(1); // TODO: should be scheduled
            if (!DMAOR.DME) {
                LowerInterrupt(InterruptSource::DMAC0_XferEnd);
                LowerInterrupt(InterruptSource::DMAC1_XferEnd);
            }
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

FORCE_INLINE uint64 SH2::GetCurrentCycleCount() const {
    return m_scheduler.CurrentCount() + m_cyclesExecuted;
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

template <bool write>
FORCE_INLINE void SH2::AdvanceWDT() {
    const uint64 cycles = GetCurrentCycleCount() + (write ? 4 : 0);

    switch (WDT.AdvanceTo(cycles)) {
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

template <bool write>
FORCE_INLINE void SH2::AdvanceFRT() {
    const uint64 cycles = GetCurrentCycleCount() + (write ? 4 : 0);

    switch (FRT.AdvanceTo(cycles)) {
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
    m_intrPending = false;

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
        // fallthrough; IRL may have lower priority than other interrupts
    }

    // Division overflow
    if (DIVU.DVCR.OVF && DIVU.DVCR.OVFIE) {
        RaiseInterrupt(InterruptSource::DIVU_OVFI);
        return;
    }

    // DMA channel transfer end
    if (DMAOR.DME && m_dmaChannels[0].xferEnded && m_dmaChannels[0].irqEnable) {
        RaiseInterrupt(InterruptSource::DMAC0_XferEnd);
        return;
    }
    if (DMAOR.DME && m_dmaChannels[1].xferEnded && m_dmaChannels[1].irqEnable) {
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
// Debugger

FORCE_INLINE bool SH2::CheckBreakpoint() {
    if (m_breakpoints.contains(PC)) {
        m_debugBreakMgr->SignalDebugBreak(debug::DebugBreakInfo::SH2Breakpoint(IsMaster(), PC));
        return true;
    }
    return false;
}

FORCE_INLINE bool SH2::CheckWatchpoints(const DecodedMemAccesses &mem) {
    const bool wtpt1 = CheckWatchpoint(mem.first);
    const bool wtpt2 = CheckWatchpoint(mem.second);
    return wtpt1 || wtpt2;
}

bool SH2::CheckWatchpoint(const DecodedMemAccesses::Access &access) {
    uint32 address;

    using AccType = DecodedMemAccesses::Type;
    switch (access.type) {
    case AccType::None: return false;
    case AccType::AtReg: address = R[access.reg]; break;
    case AccType::AtR0Reg: address = R[0] + R[access.reg]; break;
    case AccType::AtR0GBR: address = R[0] + GBR; break;
    case AccType::AtDispReg: address = access.disp + R[access.reg]; break;
    case AccType::AtDispGBR: address = access.disp + GBR; break;
    case AccType::AtDispPC: address = (PC & ~(access.size - 1)) + access.disp; break;
    }

    if (!m_watchpoints.contains(address)) {
        return false;
    }

    debug::WatchpointFlags flags;
    switch (access.size) {
    case 1: flags = access.write ? debug::WatchpointFlags::Write8 : debug::WatchpointFlags::Read8; break;
    case 2: flags = access.write ? debug::WatchpointFlags::Write16 : debug::WatchpointFlags::Read16; break;
    case 4: flags = access.write ? debug::WatchpointFlags::Write32 : debug::WatchpointFlags::Read32; break;
    default: return false; // should never happen
    }

    if (BitmaskEnum(m_watchpoints.at(address)).AnyOf(flags)) {
        m_debugBreakMgr->SignalDebugBreak(
            debug::DebugBreakInfo::SH2Watchpoint(IsMaster(), access.write, access.size, address, PC));
        return true;
    }

    return false;
}

// -------------------------------------------------------------------------
// Helper functions

FORCE_INLINE void SH2::SetupDelaySlot(uint32 targetAddress) {
    m_delaySlot = true;
    m_delaySlotTarget = targetAddress;
    m_intrPending = false;
}

template <bool delaySlot>
FORCE_INLINE void SH2::AdvancePC() {
    if constexpr (delaySlot) {
        PC = m_delaySlotTarget;
        m_delaySlot = false;
        m_intrPending = INTC.pending.level > SR.ILevel;
    } else {
        PC += 2;
    }
}

template <bool debug, bool enableCache>
FORCE_INLINE uint64 SH2::EnterException(uint8 vectorNumber) {
    const uint32 address1 = R[15] - 4;
    const uint32 address2 = R[15] - 8;
    const uint32 address3 = VBR + (static_cast<uint32>(vectorNumber) << 2u);
    TraceException<debug>(m_tracer, vectorNumber, PC, SR.u32);
    MemWriteLong<debug, enableCache>(address1, SR.u32);
    MemWriteLong<debug, enableCache>(address2, PC);
    PC = MemReadLong<enableCache>(address3);
    R[15] -= 8;
    return AccessCycles<enableCache>(address1) + AccessCycles<enableCache>(address2) +
           AccessCycles<enableCache>(address3) + 5;
}

// -----------------------------------------------------------------------------
// Instruction interpreters

template <bool debug, bool enableCache>
FORCE_INLINE uint64 SH2::InterpretNext() {
    if (m_intrPending) [[unlikely]] {
        // Service interrupt
        const uint8 vecNum = INTC.GetVector(INTC.pending.source);
        TraceInterrupt<debug>(m_tracer, vecNum, INTC.pending.level, INTC.pending.source, PC);
        devlog::trace<grp::intr>(m_logPrefix, "Handling interrupt level {:02X}, vector number {:02X}, PC {:08X}",
                                 INTC.pending.level, vecNum, PC);
        const uint64 cycles = EnterException<debug, enableCache>(vecNum);
        SR.ILevel = std::min<uint8>(INTC.pending.level, 0xF);
        m_intrPending = false;

        // Acknowledge interrupt
        switch (INTC.pending.source) {
        case InterruptSource::IRL:
            if (INTC.ICR.VECMD) {
                m_cbAcknowledgeExternalInterrupt();
            }
            break;
        case InterruptSource::NMI:
            INTC.NMI = false;
            LowerInterrupt(InterruptSource::NMI);
            break;
        default: break;
        }
        return cycles + 1;
    }

    // TODO: emulate or approximate fetch - decode - execute - memory access - writeback pipeline

    const uint16 instr = FetchInstruction<enableCache>(PC);
    TraceExecuteInstruction<debug>(m_tracer, PC, instr, m_delaySlot);

    const OpcodeType opcode = DecodeTable::s_instance.opcodes[m_delaySlot][instr];
    const DecodedArgs &args = DecodeTable::s_instance.args[instr];

    // TODO: check program execution
    switch (opcode) {
    case OpcodeType::NOP: return NOP<false>();

    case OpcodeType::SLEEP: return SLEEP();

    case OpcodeType::MOV_R: return MOV<false>(args);
    case OpcodeType::MOVB_L: return MOVBL<debug, enableCache, false>(args);
    case OpcodeType::MOVW_L: return MOVWL<debug, enableCache, false>(args);
    case OpcodeType::MOVL_L: return MOVLL<debug, enableCache, false>(args);
    case OpcodeType::MOVB_L0: return MOVBL0<debug, enableCache, false>(args);
    case OpcodeType::MOVW_L0: return MOVWL0<debug, enableCache, false>(args);
    case OpcodeType::MOVL_L0: return MOVLL0<debug, enableCache, false>(args);
    case OpcodeType::MOVB_L4: return MOVBL4<debug, enableCache, false>(args);
    case OpcodeType::MOVW_L4: return MOVWL4<debug, enableCache, false>(args);
    case OpcodeType::MOVL_L4: return MOVLL4<debug, enableCache, false>(args);
    case OpcodeType::MOVB_LG: return MOVBLG<debug, enableCache, false>(args);
    case OpcodeType::MOVW_LG: return MOVWLG<debug, enableCache, false>(args);
    case OpcodeType::MOVL_LG: return MOVLLG<debug, enableCache, false>(args);
    case OpcodeType::MOVB_M: return MOVBM<debug, enableCache, false>(args);
    case OpcodeType::MOVW_M: return MOVWM<debug, enableCache, false>(args);
    case OpcodeType::MOVL_M: return MOVLM<debug, enableCache, false>(args);
    case OpcodeType::MOVB_P: return MOVBP<debug, enableCache, false>(args);
    case OpcodeType::MOVW_P: return MOVWP<debug, enableCache, false>(args);
    case OpcodeType::MOVL_P: return MOVLP<debug, enableCache, false>(args);
    case OpcodeType::MOVB_S: return MOVBS<debug, enableCache, false>(args);
    case OpcodeType::MOVW_S: return MOVWS<debug, enableCache, false>(args);
    case OpcodeType::MOVL_S: return MOVLS<debug, enableCache, false>(args);
    case OpcodeType::MOVB_S0: return MOVBS0<debug, enableCache, false>(args);
    case OpcodeType::MOVW_S0: return MOVWS0<debug, enableCache, false>(args);
    case OpcodeType::MOVL_S0: return MOVLS0<debug, enableCache, false>(args);
    case OpcodeType::MOVB_S4: return MOVBS4<debug, enableCache, false>(args);
    case OpcodeType::MOVW_S4: return MOVWS4<debug, enableCache, false>(args);
    case OpcodeType::MOVL_S4: return MOVLS4<debug, enableCache, false>(args);
    case OpcodeType::MOVB_SG: return MOVBSG<debug, enableCache, false>(args);
    case OpcodeType::MOVW_SG: return MOVWSG<debug, enableCache, false>(args);
    case OpcodeType::MOVL_SG: return MOVLSG<debug, enableCache, false>(args);
    case OpcodeType::MOV_I: return MOVI<false>(args);
    case OpcodeType::MOVW_I: return MOVWI<debug, enableCache, false>(args);
    case OpcodeType::MOVL_I: return MOVLI<debug, enableCache, false>(args);
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
    case OpcodeType::LDC_GBR_M: return LDCMGBR<debug, enableCache, false>(args);
    case OpcodeType::LDC_SR_M: return LDCMSR<debug, enableCache, false>(args);
    case OpcodeType::LDC_VBR_M: return LDCMVBR<debug, enableCache, false>(args);
    case OpcodeType::LDS_MACH_M: return LDSMMACH<debug, enableCache, false>(args);
    case OpcodeType::LDS_MACL_M: return LDSMMACL<debug, enableCache, false>(args);
    case OpcodeType::LDS_PR_M: return LDSMPR<debug, enableCache, false>(args);
    case OpcodeType::STC_GBR_M: return STCMGBR<debug, enableCache, false>(args);
    case OpcodeType::STC_SR_M: return STCMSR<debug, enableCache, false>(args);
    case OpcodeType::STC_VBR_M: return STCMVBR<debug, enableCache, false>(args);
    case OpcodeType::STS_MACH_M: return STSMMACH<debug, enableCache, false>(args);
    case OpcodeType::STS_MACL_M: return STSMMACL<debug, enableCache, false>(args);
    case OpcodeType::STS_PR_M: return STSMPR<debug, enableCache, false>(args);

    case OpcodeType::ADD: return ADD<false>(args);
    case OpcodeType::ADD_I: return ADDI<false>(args);
    case OpcodeType::ADDC: return ADDC<false>(args);
    case OpcodeType::ADDV: return ADDV<false>(args);
    case OpcodeType::AND_R: return AND<false>(args);
    case OpcodeType::AND_I: return ANDI<false>(args);
    case OpcodeType::AND_M: return ANDM<debug, enableCache, false>(args);
    case OpcodeType::NEG: return NEG<false>(args);
    case OpcodeType::NEGC: return NEGC<false>(args);
    case OpcodeType::NOT: return NOT<false>(args);
    case OpcodeType::OR_R: return OR<false>(args);
    case OpcodeType::OR_I: return ORI<false>(args);
    case OpcodeType::OR_M: return ORM<debug, enableCache, false>(args);
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
    case OpcodeType::XOR_M: return XORM<debug, enableCache, false>(args);

    case OpcodeType::DT: return DT<false>(args);

    case OpcodeType::CLRMAC: return CLRMAC<false>();
    case OpcodeType::MACW: return MACW<debug, enableCache, false>(args);
    case OpcodeType::MACL: return MACL<debug, enableCache, false>(args);
    case OpcodeType::MUL: return MULL<false>(args);
    case OpcodeType::MULS: return MULS<false>(args);
    case OpcodeType::MULU: return MULU<false>(args);
    case OpcodeType::DMULS: return DMULS<false>(args);
    case OpcodeType::DMULU: return DMULU<false>(args);

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
    case OpcodeType::TAS: return TAS<debug, enableCache, false>(args);
    case OpcodeType::TST_R: return TST<false>(args);
    case OpcodeType::TST_I: return TSTI<false>(args);
    case OpcodeType::TST_M: return TSTM<debug, enableCache, false>(args);

    case OpcodeType::BF: return BF(args);
    case OpcodeType::BFS: return BFS(args);
    case OpcodeType::BT: return BT(args);
    case OpcodeType::BTS: return BTS(args);
    case OpcodeType::BRA: return BRA(args);
    case OpcodeType::BRAF: return BRAF(args);
    case OpcodeType::BSR: return BSR(args);
    case OpcodeType::BSRF: return BSRF(args);
    case OpcodeType::JMP: return JMP(args);
    case OpcodeType::JSR: return JSR(args);
    case OpcodeType::TRAPA: return TRAPA<debug, enableCache>(args);

    case OpcodeType::RTE: return RTE<debug, enableCache>();
    case OpcodeType::RTS: return RTS();

    case OpcodeType::Illegal: return EnterException<debug, enableCache>(xvGenIllegalInstr);

    case OpcodeType::Delay_NOP: return NOP<true>();

    case OpcodeType::Delay_SLEEP: return SLEEP();

    case OpcodeType::Delay_MOV_R: return MOV<true>(args);
    case OpcodeType::Delay_MOVB_L: return MOVBL<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_L: return MOVWL<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_L: return MOVLL<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_L0: return MOVBL0<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_L0: return MOVWL0<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_L0: return MOVLL0<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_L4: return MOVBL4<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_L4: return MOVWL4<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_L4: return MOVLL4<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_LG: return MOVBLG<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_LG: return MOVWLG<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_LG: return MOVLLG<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_M: return MOVBM<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_M: return MOVWM<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_M: return MOVLM<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_P: return MOVBP<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_P: return MOVWP<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_P: return MOVLP<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_S: return MOVBS<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_S: return MOVWS<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_S: return MOVLS<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_S0: return MOVBS0<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_S0: return MOVWS0<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_S0: return MOVLS0<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_S4: return MOVBS4<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_S4: return MOVWS4<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_S4: return MOVLS4<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVB_SG: return MOVBSG<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVW_SG: return MOVWSG<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_SG: return MOVLSG<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOV_I: return MOVI<true>(args);
    case OpcodeType::Delay_MOVW_I: return MOVWI<debug, enableCache, true>(args);
    case OpcodeType::Delay_MOVL_I: return MOVLI<debug, enableCache, true>(args);
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
    case OpcodeType::Delay_LDC_GBR_M: return LDCMGBR<debug, enableCache, true>(args);
    case OpcodeType::Delay_LDC_SR_M: return LDCMSR<debug, enableCache, true>(args);
    case OpcodeType::Delay_LDC_VBR_M: return LDCMVBR<debug, enableCache, true>(args);
    case OpcodeType::Delay_LDS_MACH_M: return LDSMMACH<debug, enableCache, true>(args);
    case OpcodeType::Delay_LDS_MACL_M: return LDSMMACL<debug, enableCache, true>(args);
    case OpcodeType::Delay_LDS_PR_M: return LDSMPR<debug, enableCache, true>(args);
    case OpcodeType::Delay_STC_GBR_M: return STCMGBR<debug, enableCache, true>(args);
    case OpcodeType::Delay_STC_SR_M: return STCMSR<debug, enableCache, true>(args);
    case OpcodeType::Delay_STC_VBR_M: return STCMVBR<debug, enableCache, true>(args);
    case OpcodeType::Delay_STS_MACH_M: return STSMMACH<debug, enableCache, true>(args);
    case OpcodeType::Delay_STS_MACL_M: return STSMMACL<debug, enableCache, true>(args);
    case OpcodeType::Delay_STS_PR_M: return STSMPR<debug, enableCache, true>(args);

    case OpcodeType::Delay_ADD: return ADD<true>(args);
    case OpcodeType::Delay_ADD_I: return ADDI<true>(args);
    case OpcodeType::Delay_ADDC: return ADDC<true>(args);
    case OpcodeType::Delay_ADDV: return ADDV<true>(args);
    case OpcodeType::Delay_AND_R: return AND<true>(args);
    case OpcodeType::Delay_AND_I: return ANDI<true>(args);
    case OpcodeType::Delay_AND_M: return ANDM<debug, enableCache, true>(args);
    case OpcodeType::Delay_NEG: return NEG<true>(args);
    case OpcodeType::Delay_NEGC: return NEGC<true>(args);
    case OpcodeType::Delay_NOT: return NOT<true>(args);
    case OpcodeType::Delay_OR_R: return OR<true>(args);
    case OpcodeType::Delay_OR_I: return ORI<true>(args);
    case OpcodeType::Delay_OR_M: return ORM<debug, enableCache, true>(args);
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
    case OpcodeType::Delay_XOR_M: return XORM<debug, enableCache, true>(args);

    case OpcodeType::Delay_DT: return DT<true>(args);

    case OpcodeType::Delay_CLRMAC: return CLRMAC<true>();
    case OpcodeType::Delay_MACW: return MACW<debug, enableCache, true>(args);
    case OpcodeType::Delay_MACL: return MACL<debug, enableCache, true>(args);
    case OpcodeType::Delay_MUL: return MULL<true>(args);
    case OpcodeType::Delay_MULS: return MULS<true>(args);
    case OpcodeType::Delay_MULU: return MULU<true>(args);
    case OpcodeType::Delay_DMULS: return DMULS<true>(args);
    case OpcodeType::Delay_DMULU: return DMULU<true>(args);

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
    case OpcodeType::Delay_TAS: return TAS<debug, enableCache, true>(args);
    case OpcodeType::Delay_TST_R: return TST<true>(args);
    case OpcodeType::Delay_TST_I: return TSTI<true>(args);
    case OpcodeType::Delay_TST_M: return TSTM<debug, enableCache, true>(args);

    case OpcodeType::IllegalSlot: return EnterException<debug, enableCache>(xvSlotIllegalInstr);
    }

    util::unreachable();
}

template uint64 SH2::InterpretNext<false, false>();
template uint64 SH2::InterpretNext<false, true>();
template uint64 SH2::InterpretNext<true, false>();
template uint64 SH2::InterpretNext<true, true>();

// nop
template <bool delaySlot>
FORCE_INLINE uint64 SH2::NOP() {
    AdvancePC<delaySlot>();
    return 1;
}

// sleep
FORCE_INLINE uint64 SH2::SLEEP() {
    if (!m_sleep) {
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
        m_sleep = true;
    }

    return 3;
}

// mov Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::MOV(const DecodedArgs &args) {
    R[args.rn] = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// mov.b @Rm, Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBL(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = bit::sign_extend<8>(MemReadByte<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w @Rm, Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWL(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = bit::sign_extend<16>(MemReadWord<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l @Rm, Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLL(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = MemReadLong<enableCache>(address);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b @(R0,Rm), Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBL0(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + R[0];
    R[args.rn] = bit::sign_extend<8>(MemReadByte<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w @(R0,Rm), Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWL0(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + R[0];
    R[args.rn] = bit::sign_extend<16>(MemReadWord<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l @(R0,Rm), Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLL0(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + R[0];
    R[args.rn] = MemReadLong<enableCache>(address);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b @(disp,Rm), R0
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBL4(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + args.dispImm;
    R[0] = bit::sign_extend<8>(MemReadByte<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w @(disp,Rm), R0
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWL4(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + args.dispImm;
    R[0] = bit::sign_extend<16>(MemReadWord<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l @(disp,Rm), Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLL4(const DecodedArgs &args) {
    const uint32 address = R[args.rm] + args.dispImm;
    R[args.rn] = MemReadLong<enableCache>(address);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b @(disp,GBR), R0
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBLG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    R[0] = bit::sign_extend<8>(MemReadByte<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w @(disp,GBR), R0
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWLG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    R[0] = bit::sign_extend<16>(MemReadWord<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l @(disp,GBR), R0
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLLG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    R[0] = MemReadLong<enableCache>(address);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b Rm, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBM(const DecodedArgs &args) {
    const uint32 address = R[args.rn] - 1;
    MemWriteByte<debug, enableCache>(address, R[args.rm]);
    R[args.rn] -= 1;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w Rm, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWM(const DecodedArgs &args) {
    const uint32 address = R[args.rn] - 2;
    MemWriteWord<debug, enableCache>(address, R[args.rm]);
    R[args.rn] -= 2;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l Rm, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLM(const DecodedArgs &args) {
    const uint32 address = R[args.rn] - 4;
    MemWriteLong<debug, enableCache>(address, R[args.rm]);
    R[args.rn] -= 4;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b @Rm+, Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBP(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = bit::sign_extend<8>(MemReadByte<enableCache>(address));
    if (args.rn != args.rm) {
        R[args.rm] += 1;
    }
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w @Rm+, Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWP(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = bit::sign_extend<16>(MemReadWord<enableCache>(address));
    if (args.rn != args.rm) {
        R[args.rm] += 2;
    }
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l @Rm+, Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLP(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    R[args.rn] = MemReadLong<enableCache>(address);
    if (args.rn != args.rm) {
        R[args.rm] += 4;
    }
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b Rm, @Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBS(const DecodedArgs &args) {
    const uint32 address = R[args.rn];
    MemWriteByte<debug, enableCache>(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w Rm, @Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWS(const DecodedArgs &args) {
    const uint32 address = R[args.rn];
    MemWriteWord<debug, enableCache>(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l Rm, @Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLS(const DecodedArgs &args) {
    const uint32 address = R[args.rn];
    MemWriteLong<debug, enableCache>(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b Rm, @(R0,Rn)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBS0(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + R[0];
    MemWriteByte<debug, enableCache>(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w Rm, @(R0,Rn)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWS0(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + R[0];
    MemWriteWord<debug, enableCache>(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l Rm, @(R0,Rn)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLS0(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + R[0];
    MemWriteLong<debug, enableCache>(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b R0, @(disp,Rn)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBS4(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + args.dispImm;
    MemWriteByte<debug, enableCache>(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w R0, @(disp,Rn)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWS4(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + args.dispImm;
    MemWriteWord<debug, enableCache>(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l Rm, @(disp,Rn)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLS4(const DecodedArgs &args) {
    const uint32 address = R[args.rn] + args.dispImm;
    MemWriteLong<debug, enableCache>(address, R[args.rm]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.b R0, @(disp,GBR)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVBSG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    MemWriteByte<debug, enableCache>(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.w R0, @(disp,GBR)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWSG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    MemWriteWord<debug, enableCache>(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l R0, @(disp,GBR)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLSG(const DecodedArgs &args) {
    const uint32 address = GBR + args.dispImm;
    MemWriteLong<debug, enableCache>(address, R[0]);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov #imm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::MOVI(const DecodedArgs &args) {
    R[args.rn] = args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// mov.w @(disp,PC), Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVWI(const DecodedArgs &args) {
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = pc + args.dispImm;
    R[args.rn] = bit::sign_extend<16>(MemReadWord<enableCache>(address));
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mov.l @(disp,PC), Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MOVLI(const DecodedArgs &args) {
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = (pc & ~3u) + args.dispImm;
    R[args.rn] = MemReadLong<enableCache>(address);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// mova @(disp,PC), R0
template <bool delaySlot>
FORCE_INLINE uint64 SH2::MOVA(const DecodedArgs &args) {
    const uint32 pc = (delaySlot ? m_delaySlotTarget - 2u : PC);
    const uint32 address = (pc & ~3u) + args.dispImm;
    R[0] = address;
    AdvancePC<delaySlot>();
    return 1;
}

// movt Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::MOVT(const DecodedArgs &args) {
    R[args.rn] = SR.T;
    AdvancePC<delaySlot>();
    return 1;
}

// clrt
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CLRT() {
    SR.T = 0;
    AdvancePC<delaySlot>();
    return 1;
}

// sett
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SETT() {
    SR.T = 1;
    AdvancePC<delaySlot>();
    return 1;
}

// exts.b Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::EXTSB(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<8>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// exts.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::EXTSW(const DecodedArgs &args) {
    R[args.rn] = bit::sign_extend<16>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// extu.b Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::EXTUB(const DecodedArgs &args) {
    R[args.rn] = R[args.rm] & 0xFF;
    AdvancePC<delaySlot>();
    return 1;
}

// extu.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::EXTUW(const DecodedArgs &args) {
    R[args.rn] = R[args.rm] & 0xFFFF;
    AdvancePC<delaySlot>();
    return 1;
}

// swap.b Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SWAPB(const DecodedArgs &args) {
    const uint32 tmp0 = R[args.rm] & 0xFFFF0000;
    const uint32 tmp1 = (R[args.rm] & 0xFF) << 8u;
    R[args.rn] = ((R[args.rm] >> 8u) & 0xFF) | tmp1 | tmp0;
    AdvancePC<delaySlot>();
    return 1;
}

// swap.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SWAPW(const DecodedArgs &args) {
    const uint32 tmp = R[args.rm] >> 16u;
    R[args.rn] = (R[args.rm] << 16u) | tmp;
    AdvancePC<delaySlot>();
    return 1;
}

// xtrct Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::XTRCT(const DecodedArgs &args) {
    R[args.rn] = (R[args.rn] >> 16u) | (R[args.rm] << 16u);
    AdvancePC<delaySlot>();
    return 1;
}

// ldc Rm, GBR
template <bool delaySlot>
FORCE_INLINE uint64 SH2::LDCGBR(const DecodedArgs &args) {
    GBR = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// ldc Rm, SR
template <bool delaySlot>
FORCE_INLINE uint64 SH2::LDCSR(const DecodedArgs &args) {
    SR.u32 = R[args.rm] & 0x000003F3;
    m_intrPending = !delaySlot && INTC.pending.level > SR.ILevel;
    AdvancePC<delaySlot>();
    return 1;
}

// ldc Rm, VBR
template <bool delaySlot>
FORCE_INLINE uint64 SH2::LDCVBR(const DecodedArgs &args) {
    VBR = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// lds Rm, MACH
template <bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMACH(const DecodedArgs &args) {
    MAC.H = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// lds Rm, MACL
template <bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMACL(const DecodedArgs &args) {
    MAC.L = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// lds Rm, PR
template <bool delaySlot>
FORCE_INLINE uint64 SH2::LDSPR(const DecodedArgs &args) {
    PR = R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// stc GBR, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::STCGBR(const DecodedArgs &args) {
    R[args.rn] = GBR;
    AdvancePC<delaySlot>();
    return 1;
}

// stc SR, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::STCSR(const DecodedArgs &args) {
    R[args.rn] = SR.u32;
    AdvancePC<delaySlot>();
    return 1;
}

// stc VBR, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::STCVBR(const DecodedArgs &args) {
    R[args.rn] = VBR;
    AdvancePC<delaySlot>();
    return 1;
}

// sts MACH, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::STSMACH(const DecodedArgs &args) {
    R[args.rn] = MAC.H;
    AdvancePC<delaySlot>();
    return 1;
}

// sts MACL, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::STSMACL(const DecodedArgs &args) {
    R[args.rn] = MAC.L;
    AdvancePC<delaySlot>();
    return 1;
}

// sts PR, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::STSPR(const DecodedArgs &args) {
    R[args.rn] = PR;
    AdvancePC<delaySlot>();
    return 1;
}

// ldc.l @Rm+, GBR
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCMGBR(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    GBR = MemReadLong<enableCache>(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) + 2;
}

// ldc.l @Rm+, SR
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCMSR(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    SR.u32 = MemReadLong<enableCache>(address) & 0x000003F3;
    m_intrPending = !delaySlot && INTC.pending.level > SR.ILevel;
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) + 2;
}

// ldc.l @Rm+, VBR
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDCMVBR(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    VBR = MemReadLong<enableCache>(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) + 2;
}

// lds.l @Rm+, MACH
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMMACH(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    MAC.H = MemReadLong<enableCache>(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// lds.l @Rm+, MACL
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMMACL(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    MAC.L = MemReadLong<enableCache>(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// lds.l @Rm+, PR
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::LDSMPR(const DecodedArgs &args) {
    const uint32 address = R[args.rm];
    PR = MemReadLong<enableCache>(address);
    R[args.rm] += 4;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// stc.l GBR, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCMGBR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong<debug, enableCache>(address, GBR);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) + 1;
}

// stc.l SR, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCMSR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong<debug, enableCache>(address, SR.u32);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) + 1;
}

// stc.l VBR, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STCMVBR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong<debug, enableCache>(address, VBR);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) + 1;
}

// sts.l MACH, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSMMACH(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong<debug, enableCache>(address, MAC.H);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// sts.l MACL, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSMMACL(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong<debug, enableCache>(address, MAC.L);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// sts.l PR, @-Rn
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::STSMPR(const DecodedArgs &args) {
    R[args.rn] -= 4;
    const uint32 address = R[args.rn];
    MemWriteLong<debug, enableCache>(address, PR);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address);
}

// add Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ADD(const DecodedArgs &args) {
    R[args.rn] += R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// add #imm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ADDI(const DecodedArgs &args) {
    R[args.rn] += args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// addc Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ADDC(const DecodedArgs &args) {
    const uint32 tmp1 = R[args.rn] + R[args.rm];
    const uint32 tmp0 = R[args.rn];
    R[args.rn] = tmp1 + SR.T;
    SR.T = (tmp0 > tmp1) || (tmp1 > R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// addv Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ADDV(const DecodedArgs &args) {
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
FORCE_INLINE uint64 SH2::AND(const DecodedArgs &args) {
    R[args.rn] &= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// and #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ANDI(const DecodedArgs &args) {
    R[0] &= args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// and.b #imm, @(R0,GBR)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ANDM(const DecodedArgs &args) {
    const uint32 address = GBR + R[0];
    uint8 tmp = MemReadByte<enableCache>(address);
    tmp &= args.dispImm;
    MemWriteByte<debug, enableCache>(address, tmp);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) * 2 + 1;
}

// neg Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::NEG(const DecodedArgs &args) {
    R[args.rn] = -R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// negc Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::NEGC(const DecodedArgs &args) {
    const uint32 tmp = -R[args.rm];
    R[args.rn] = tmp - SR.T;
    SR.T = (0 < tmp) || (tmp < R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// not Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::NOT(const DecodedArgs &args) {
    R[args.rn] = ~R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// or Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::OR(const DecodedArgs &args) {
    R[args.rn] |= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// or #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ORI(const DecodedArgs &args) {
    R[0] |= args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// or.b #imm, @(R0,GBR)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::ORM(const DecodedArgs &args) {
    const uint32 address = GBR + R[0];
    uint8 tmp = MemReadByte<enableCache>(address);
    tmp |= args.dispImm;
    MemWriteByte<debug, enableCache>(address, tmp);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) * 2 + 1;
}

// rotcl Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ROTCL(const DecodedArgs &args) {
    const bool tmp = R[args.rn] >> 31u;
    R[args.rn] = (R[args.rn] << 1u) | SR.T;
    SR.T = tmp;
    AdvancePC<delaySlot>();
    return 1;
}

// rotcr Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ROTCR(const DecodedArgs &args) {
    const bool tmp = R[args.rn] & 1u;
    R[args.rn] = (R[args.rn] >> 1u) | (SR.T << 31u);
    SR.T = tmp;
    AdvancePC<delaySlot>();
    return 1;
}

// rotl Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ROTL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] = (R[args.rn] << 1u) | SR.T;
    AdvancePC<delaySlot>();
    return 1;
}

// rotr Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::ROTR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] = (R[args.rn] >> 1u) | (SR.T << 31u);
    AdvancePC<delaySlot>();
    return 1;
}

// shal Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHAL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] <<= 1u;
    AdvancePC<delaySlot>();
    return 1;
}

// shar Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHAR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] = static_cast<sint32>(R[args.rn]) >> 1;
    AdvancePC<delaySlot>();
    return 1;
}

// shll Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHLL(const DecodedArgs &args) {
    SR.T = R[args.rn] >> 31u;
    R[args.rn] <<= 1u;
    AdvancePC<delaySlot>();
    return 1;
}

// shll2 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHLL2(const DecodedArgs &args) {
    R[args.rn] <<= 2u;
    AdvancePC<delaySlot>();
    return 1;
}

// shll8 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHLL8(const DecodedArgs &args) {
    R[args.rn] <<= 8u;
    AdvancePC<delaySlot>();
    return 1;
}

// shll16 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHLL16(const DecodedArgs &args) {
    R[args.rn] <<= 16u;
    AdvancePC<delaySlot>();
    return 1;
}

// shlr Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHLR(const DecodedArgs &args) {
    SR.T = R[args.rn] & 1u;
    R[args.rn] >>= 1u;
    AdvancePC<delaySlot>();
    return 1;
}

// shlr2 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHLR2(const DecodedArgs &args) {
    R[args.rn] >>= 2u;
    AdvancePC<delaySlot>();
    return 1;
}

// shlr8 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHLR8(const DecodedArgs &args) {
    R[args.rn] >>= 8u;
    AdvancePC<delaySlot>();
    return 1;
}

// shlr16 Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SHLR16(const DecodedArgs &args) {
    R[args.rn] >>= 16u;
    AdvancePC<delaySlot>();
    return 1;
}

// sub Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SUB(const DecodedArgs &args) {
    R[args.rn] -= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// subc Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SUBC(const DecodedArgs &args) {
    const uint32 tmp1 = R[args.rn] - R[args.rm];
    const uint32 tmp0 = R[args.rn];
    R[args.rn] = tmp1 - SR.T;
    SR.T = (tmp0 < tmp1) || (tmp1 < R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// subv Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::SUBV(const DecodedArgs &args) {

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
FORCE_INLINE uint64 SH2::XOR(const DecodedArgs &args) {
    R[args.rn] ^= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// xor #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH2::XORI(const DecodedArgs &args) {
    R[0] ^= args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// xor.b #imm, @(R0,GBR)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::XORM(const DecodedArgs &args) {
    const uint32 address = GBR + R[0];
    uint8 tmp = MemReadByte<enableCache>(address);
    tmp ^= args.dispImm;
    MemWriteByte<debug, enableCache>(address, tmp);
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) * 2 + 1;
}

// dt Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::DT(const DecodedArgs &args) {
    R[args.rn]--;
    SR.T = R[args.rn] == 0;
    AdvancePC<delaySlot>();
    return 1;
}

// clrmac
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CLRMAC() {
    MAC.u64 = 0;
    AdvancePC<delaySlot>();
    return 1;
}

// mac.w @Rm+, @Rn+
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MACW(const DecodedArgs &args) {
    const uint32 address2 = R[args.rn];
    const sint32 op2 = static_cast<sint16>(MemReadWord<enableCache>(address2));
    R[args.rn] += 2;
    const uint32 address1 = R[args.rm];
    const sint32 op1 = static_cast<sint16>(MemReadWord<enableCache>(address1));
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
        MAC.u64 += mul;
    }

    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address1) + AccessCycles<enableCache>(address2);
}

// mac.l @Rm+, @Rn+
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::MACL(const DecodedArgs &args) {
    const uint32 address2 = R[args.rn];
    const sint64 op2 = static_cast<sint64>(static_cast<sint32>(MemReadLong<enableCache>(address2)));
    R[args.rn] += 4;
    const uint32 address1 = R[args.rm];
    const sint64 op1 = static_cast<sint64>(static_cast<sint32>(MemReadLong<enableCache>(address1)));
    R[args.rm] += 4;

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

    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address1) + AccessCycles<enableCache>(address2);
}

// mul.l Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::MULL(const DecodedArgs &args) {
    MAC.L = R[args.rm] * R[args.rn];
    AdvancePC<delaySlot>();
    return 2;
}

// muls.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::MULS(const DecodedArgs &args) {
    MAC.L = bit::sign_extend<16>(R[args.rm]) * bit::sign_extend<16>(R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// mulu.w Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::MULU(const DecodedArgs &args) {
    auto cast = [](uint32 val) { return static_cast<uint32>(static_cast<uint16>(val)); };
    MAC.L = cast(R[args.rm]) * cast(R[args.rn]);
    AdvancePC<delaySlot>();
    return 1;
}

// dmuls.l Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::DMULS(const DecodedArgs &args) {
    auto cast = [](uint32 val) { return static_cast<sint64>(static_cast<sint32>(val)); };
    MAC.u64 = cast(R[args.rm]) * cast(R[args.rn]);
    AdvancePC<delaySlot>();
    return 2;
}

// dmulu.l Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::DMULU(const DecodedArgs &args) {
    MAC.u64 = static_cast<uint64>(R[args.rm]) * static_cast<uint64>(R[args.rn]);
    AdvancePC<delaySlot>();
    return 2;
}

// div0s r{}, Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::DIV0S(const DecodedArgs &args) {
    SR.M = static_cast<sint32>(R[args.rm]) < 0;
    SR.Q = static_cast<sint32>(R[args.rn]) < 0;
    SR.T = SR.M != SR.Q;
    AdvancePC<delaySlot>();
    return 1;
}

// div0u
template <bool delaySlot>
FORCE_INLINE uint64 SH2::DIV0U() {
    SR.M = 0;
    SR.Q = 0;
    SR.T = 0;
    AdvancePC<delaySlot>();
    return 1;
}

// div1 Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::DIV1(const DecodedArgs &args) {
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
FORCE_INLINE uint64 SH2::CMPIM(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[0]) == args.dispImm;
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/eq Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CMPEQ(const DecodedArgs &args) {
    SR.T = R[args.rn] == R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/ge Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CMPGE(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) >= static_cast<sint32>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/gt Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CMPGT(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) > static_cast<sint32>(R[args.rm]);
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/hi Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CMPHI(const DecodedArgs &args) {
    SR.T = R[args.rn] > R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/hs Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CMPHS(const DecodedArgs &args) {
    SR.T = R[args.rn] >= R[args.rm];
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/pl Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CMPPL(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) > 0;
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/pz Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CMPPZ(const DecodedArgs &args) {
    SR.T = static_cast<sint32>(R[args.rn]) >= 0;
    AdvancePC<delaySlot>();
    return 1;
}

// cmp/str Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::CMPSTR(const DecodedArgs &args) {
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
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::TAS(const DecodedArgs &args) {
    const uint32 address = R[args.rn];
    // TODO: enable bus lock on this read
    const uint8 tmp = MemReadByte<false>(address);
    SR.T = tmp == 0;
    // TODO: disable bus lock on this write
    MemWriteByte<debug, enableCache>(address, tmp | 0x80);

    AdvancePC<delaySlot>();
    return AccessCycles<false>(address) + AccessCycles<enableCache>(address) + 2;
}

// tst Rm, Rn
template <bool delaySlot>
FORCE_INLINE uint64 SH2::TST(const DecodedArgs &args) {
    SR.T = (R[args.rn] & R[args.rm]) == 0;
    AdvancePC<delaySlot>();
    return 1;
}

// tst #imm, R0
template <bool delaySlot>
FORCE_INLINE uint64 SH2::TSTI(const DecodedArgs &args) {
    SR.T = (R[0] & args.dispImm) == 0;
    AdvancePC<delaySlot>();
    return 1;
}

// tst.b #imm, @(R0,GBR)
template <bool debug, bool enableCache, bool delaySlot>
FORCE_INLINE uint64 SH2::TSTM(const DecodedArgs &args) {
    const uint32 address = GBR + R[0];
    const uint8 tmp = MemReadByte<enableCache>(address);
    SR.T = (tmp & args.dispImm) == 0;
    AdvancePC<delaySlot>();
    return AccessCycles<enableCache>(address) + 2;
}

// bf <label>
FORCE_INLINE uint64 SH2::BF(const DecodedArgs &args) {
    if (!SR.T) {
        PC += args.dispImm;
        return 3;
    } else {
        PC += 2;
        return 1;
    }
}

// bf/s <label>
FORCE_INLINE uint64 SH2::BFS(const DecodedArgs &args) {
    if (!SR.T) {
        SetupDelaySlot(PC + args.dispImm);
    }
    PC += 2;
    return !SR.T ? 2 : 1;
}

// bt <label>
FORCE_INLINE uint64 SH2::BT(const DecodedArgs &args) {
    if (SR.T) {
        PC += args.dispImm;
        return 3;
    } else {
        PC += 2;
        return 1;
    }
}

// bt/s <label>
FORCE_INLINE uint64 SH2::BTS(const DecodedArgs &args) {
    if (SR.T) {
        SetupDelaySlot(PC + args.dispImm);
    }
    PC += 2;
    return SR.T ? 2 : 1;
}

// bra <label>
FORCE_INLINE uint64 SH2::BRA(const DecodedArgs &args) {
    SetupDelaySlot(PC + args.dispImm);
    PC += 2;
    return 2;
}

// braf Rm
FORCE_INLINE uint64 SH2::BRAF(const DecodedArgs &args) {
    SetupDelaySlot(PC + R[args.rm] + 4);
    PC += 2;
    return 2;
}

// bsr <label>
FORCE_INLINE uint64 SH2::BSR(const DecodedArgs &args) {
    PR = PC + 4;
    SetupDelaySlot(PC + args.dispImm);
    PC += 2;
    return 2;
}

// bsrf Rm
FORCE_INLINE uint64 SH2::BSRF(const DecodedArgs &args) {
    PR = PC + 4;
    SetupDelaySlot(PC + R[args.rm] + 4);
    PC += 2;
    return 2;
}

// jmp @Rm
FORCE_INLINE uint64 SH2::JMP(const DecodedArgs &args) {
    SetupDelaySlot(R[args.rm]);
    PC += 2;
    return 2;
}

// jsr @Rm
FORCE_INLINE uint64 SH2::JSR(const DecodedArgs &args) {
    PR = PC + 4;
    SetupDelaySlot(R[args.rm]);
    PC += 2;
    return 2;
}

// trapa #imm
template <bool debug, bool enableCache>
FORCE_INLINE uint64 SH2::TRAPA(const DecodedArgs &args) {
    devlog::trace<grp::intr>(m_logPrefix, "Handling TRAPA, vector number {:02X}, PC {:08X}", args.dispImm, PC);
    const uint32 address1 = R[15] - 4;
    const uint32 address2 = R[15] - 8;
    const uint32 address3 = VBR + args.dispImm;
    MemWriteLong<debug, enableCache>(address1, SR.u32);
    MemWriteLong<debug, enableCache>(address2, PC + 2);
    PC = MemReadLong<enableCache>(address3);
    R[15] -= 8;
    return AccessCycles<enableCache>(address1) + AccessCycles<enableCache>(address2) +
           AccessCycles<enableCache>(address3) + 5;
}

template <bool debug, bool enableCache>
FORCE_INLINE uint64 SH2::RTE() {
    const uint32 address1 = R[15];
    const uint32 address2 = R[15] + 4;
    // rte
    SetupDelaySlot(MemReadLong<enableCache>(address1));
    SR.u32 = MemReadLong<enableCache>(address2) & 0x000003F3;
    PC += 2;
    R[15] += 8;
    devlog::trace<grp::intr>(m_logPrefix, "Returning from interrupt handler, PC {:08X} -> {:08X}", PC,
                             m_delaySlotTarget);
    return AccessCycles<enableCache>(address1) + AccessCycles<enableCache>(address2) + 2;
}

// rts
FORCE_INLINE uint64 SH2::RTS() {
    SetupDelaySlot(PR);
    PC += 2;
    return 2;
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

uint32 SH2::Probe::DelaySlotTarget() const {
    return m_sh2.m_delaySlotTarget;
}

bool SH2::Probe::GetSleepState() const {
    return m_sh2.m_sleep;
}

void SH2::Probe::SetSleepState(bool sleep) {
    m_sh2.m_sleep = sleep;
}

void SH2::Probe::ExecuteDiv32() {
    m_sh2.ExecuteDiv32<true>();
}

void SH2::Probe::ExecuteDiv64() {
    m_sh2.ExecuteDiv64<true>();
}

} // namespace ymir::sh2
