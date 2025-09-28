#pragma once

#include "sh2_excpt.hpp"
#include "sh2_regs.hpp"

#include "sh2_decode.hpp"

#include "sh2_bsc.hpp"
#include "sh2_cache.hpp"
#include "sh2_divu.hpp"
#include "sh2_dmac.hpp"
#include "sh2_frt.hpp"
#include "sh2_intc.hpp"
#include "sh2_power.hpp"
#include "sh2_sci.hpp"
#include "sh2_ubc.hpp"
#include "sh2_wdt.hpp"

#include <ymir/hw/hw_defs.hpp>

#include <ymir/hw/scu/scu_internal_callbacks.hpp>
#include <ymir/hw/sh2/sh2_internal_callbacks.hpp>

#include <ymir/core/scheduler.hpp>
#include <ymir/sys/bus.hpp>
#include <ymir/sys/system_features.hpp>

#include <ymir/state/state_sh2.hpp>

#include <ymir/debug/debug_break.hpp>
#include <ymir/debug/sh2_tracer_base.hpp>
#include <ymir/debug/watchpoint_defs.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/inline.hpp>

#include <array>
#include <iosfwd>
#include <map>
#include <set>

namespace ymir::sh2 {

// -----------------------------------------------------------------------------

class SH2 {
public:
    SH2(core::Scheduler &scheduler, sys::SH2Bus &bus, bool master, const sys::SystemFeatures &systemFeatures);

    void Reset(bool hard, bool watchdogInitiated = false);

    void MapCallbacks(CBAcknowledgeExternalInterrupt callback) {
        m_cbAcknowledgeExternalInterrupt = callback;
    }

    void UseDebugBreakManager(debug::DebugBreakManager *mgr) {
        m_debugBreakMgr = mgr;
    }

    void MapMemory(sys::SH2Bus &bus);

    void DumpCacheData(std::ostream &out) const;
    void DumpCacheAddressTag(std::ostream &out) const;

    // -------------------------------------------------------------------------
    // Usage

    /// @brief Advances the SH2 for at least the specified number of cycles.
    /// @tparam debug whether to enable debug features
    /// @tparam enableCache whether to emulate the cache
    /// @param[in] cycles the minimum number of cycles
    /// @param[in] spilloverCycles cycles spilled over from the previous execution
    /// @return the number of cycles actually executed
    template <bool debug, bool enableCache>
    uint64 Advance(uint64 cycles, uint64 spilloverCycles = 0);

    // Executes a single instruction.
    // Returns the number of cycles executed.
    template <bool debug, bool enableCache>
    uint64 Step();

    bool IsMaster() const {
        return !BCR1.MASTER;
    }

    bool GetNMI() const;
    void SetNMI();

    // Purges the contents of the cache.
    // Should be done before enabling cache emulation to ensure previous cache contents are cleared.
    void PurgeCache();

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SH2State &state) const;
    [[nodiscard]] bool ValidateState(const state::SH2State &state) const;
    void LoadState(const state::SH2State &state);

    // -------------------------------------------------------------------------
    // Debugger

    // Attaches the specified tracer to this component.
    // Pass nullptr to disable tracing.
    void UseTracer(debug::ISH2Tracer *tracer) {
        m_tracer = tracer;
    }

    // Adds the specified address to the set of breakpoints.
    // The address is force-aligned to word boundaries.
    // Returns `true` if the breakpoint was added, `false` if it already exists.
    bool AddBreakpoint(uint32 address) {
        return m_breakpoints.insert(address & ~1u).second;
    }

    // Removes the specified address from the set of breakpoints.
    // The address is force-aligned to word boundaries.
    // Returns `true` if the breakpoint was removed, `false` if it did not exist.
    bool RemoveBreakpoint(uint32 address) {
        return m_breakpoints.erase(address & ~1u);
    }

    // Toggles the breakpoint at the specified address.
    // The address is force-aligned to word boundaries.
    // Returns `true` if the breakpoint was added, `false` if it was removed.
    bool ToggleBreakpoint(uint32 address) {
        address &= ~1u;
        const bool result = m_breakpoints.insert(address).second;
        if (!result) {
            m_breakpoints.erase(address);
        }
        return result;
    }

    // Clears all breakpoints.
    void ClearBreakpoints() {
        m_breakpoints.clear();
    }

    // Retrieves all breakpoints set in this SH-2.
    const std::set<uint32> &GetBreakpoints() const {
        return m_breakpoints;
    }

    // Replaces all breakpoints with those of the provided set.
    // All addresses of the specified set are force-aligned to word boundaries.
    void ReplaceBreakpoints(const std::set<uint32> &breakpoints) {
        // Manage breakpoints manually to sanitize addresses
        m_breakpoints.clear();
        for (auto address : breakpoints) {
            m_breakpoints.insert(address & ~1u);
        }
    }

    // Determines if the specified address has a breakpoint set.
    // The address is force-aligned to word boundaries.
    bool IsBreakpointSet(uint32 address) const {
        return m_breakpoints.contains(address & ~1u);
    }

    // Adds watchpoints to the specified address.
    // Watchpoints set at misaligned addresses will not trigger.
    // e.g. longword-sized watchpoint with address & 3 != 0
    void AddWatchpoint(uint32 address, debug::WatchpointFlags flags) {
        if (flags != debug::WatchpointFlags::None) {
            m_watchpoints[address] |= flags;
        }
    }

    // Removes watchpoints from the specified address.
    void RemoveWatchpoint(uint32 address, debug::WatchpointFlags flags) {
        if (!m_watchpoints.contains(address)) {
            return;
        }
        auto &wtpt = m_watchpoints.at(address);
        wtpt &= ~flags;
        if (wtpt == debug::WatchpointFlags::None) {
            m_watchpoints.erase(address);
        }
    }

    // Clears all watchpoints at the specified address.
    void ClearWatchpointsAt(uint32 address) {
        m_watchpoints.erase(address);
    }

    // Clears all watchpoints.
    void ClearWatchpoints() {
        m_watchpoints.clear();
    }

    // Retrieves configured watchpoints for the specified address.
    debug::WatchpointFlags GetWatchpoint(uint32 address) const {
        if (m_watchpoints.contains(address)) {
            return m_watchpoints.at(address);
        }
        return debug::WatchpointFlags::None;
    }

    // Retrieves all watchpoints set in this SH-2.
    const std::map<uint32, debug::WatchpointFlags> &GetWatchpoints() const {
        return m_watchpoints;
    }

    // Replaces all watchpoints with those of the provided set.
    // All addresses of the specified set are force-aligned to word boundaries.
    void ReplaceWatchpoints(const std::map<uint32, debug::WatchpointFlags> &watchpoints) {
        // Manage watchpoints manually to sanitize addresses
        m_watchpoints.clear();
        for (auto &[address, flags] : watchpoints) {
            AddWatchpoint(address, flags);
        }
    }

    // Suspends (disabled) the CPU in debug mode.
    void SetCPUSuspended(bool suspend) {
        m_debugSuspend = suspend;
    }

    // Determines if the CPU is suspended in debug mode.
    bool IsCPUSuspended() const {
        return m_debugSuspend;
    }

    class Probe {
    public:
        Probe(SH2 &sh2);

        // ---------------------------------------------------------------------
        // Registers

        FORCE_INLINE std::array<uint32, 16> &R() {
            return m_sh2.R;
        }
        FORCE_INLINE const std::array<uint32, 16> &R() const {
            return m_sh2.R;
        }

        FORCE_INLINE uint32 &R(uint8 rn) {
            assert(rn <= 15);
            return m_sh2.R[rn];
        }
        FORCE_INLINE const uint32 &R(uint8 rn) const {
            assert(rn <= 15);
            return m_sh2.R[rn];
        }

        FORCE_INLINE uint32 &PC() {
            return m_sh2.PC;
        }
        FORCE_INLINE uint32 PC() const {
            return m_sh2.PC;
        }

        FORCE_INLINE uint32 &PR() {
            return m_sh2.PR;
        }
        FORCE_INLINE uint32 PR() const {
            return m_sh2.PR;
        }

        FORCE_INLINE RegMAC &MAC() {
            return m_sh2.MAC;
        }
        FORCE_INLINE RegMAC MAC() const {
            return m_sh2.MAC;
        }

        FORCE_INLINE RegSR &SR() {
            return m_sh2.SR;
        }
        FORCE_INLINE RegSR SR() const {
            return m_sh2.SR;
        }

        FORCE_INLINE uint32 &GBR() {
            return m_sh2.GBR;
        }
        FORCE_INLINE uint32 GBR() const {
            return m_sh2.GBR;
        }

        FORCE_INLINE uint32 &VBR() {
            return m_sh2.VBR;
        }
        FORCE_INLINE uint32 VBR() const {
            return m_sh2.VBR;
        }

        // ---------------------------------------------------------------------
        // Regular memory accessors (with side-effects)

        uint16 FetchInstruction(uint32 address, bool bypassCache) const;

        uint8 MemReadByte(uint32 address, bool bypassCache) const;
        uint16 MemReadWord(uint32 address, bool bypassCache) const;
        uint32 MemReadLong(uint32 address, bool bypassCache) const;

        void MemWriteByte(uint32 address, uint8 value, bool bypassCache);
        void MemWriteWord(uint32 address, uint16 value, bool bypassCache);
        void MemWriteLong(uint32 address, uint32 value, bool bypassCache);

        // ---------------------------------------------------------------------
        // Debug memory accessors (without side-effects)

        uint16 PeekInstruction(uint32 address, bool bypassCache) const;

        uint8 MemPeekByte(uint32 address, bool bypassCache) const;
        uint16 MemPeekWord(uint32 address, bool bypassCache) const;
        uint32 MemPeekLong(uint32 address, bool bypassCache) const;

        void MemPokeByte(uint32 address, uint8 value, bool bypassCache);
        void MemPokeWord(uint32 address, uint16 value, bool bypassCache);
        void MemPokeLong(uint32 address, uint32 value, bool bypassCache);

        // ---------------------------------------------------------------------
        // Execution state

        bool IsInDelaySlot() const;
        uint32 DelaySlotTarget() const;

        bool GetSleepState() const;
        void SetSleepState(bool sleep);

        // ---------------------------------------------------------------------
        // On-chip peripheral registers

        FORCE_INLINE DivisionUnit &DIVU() {
            return m_sh2.DIVU;
        }

        FORCE_INLINE const DivisionUnit &DIVU() const {
            return m_sh2.DIVU;
        }

        FORCE_INLINE InterruptController &INTC() {
            return m_sh2.INTC;
        }

        FORCE_INLINE const InterruptController &INTC() const {
            return m_sh2.INTC;
        }

        FORCE_INLINE FreeRunningTimer &FRT() {
            return m_sh2.FRT;
        }

        FORCE_INLINE const FreeRunningTimer &FRT() const {
            return m_sh2.FRT;
        }

        FORCE_INLINE WatchdogTimer &WDT() {
            return m_sh2.WDT;
        }

        FORCE_INLINE const WatchdogTimer &WDT() const {
            return m_sh2.WDT;
        }

        FORCE_INLINE RegSBYCR &SBYCR() {
            return m_sh2.SBYCR;
        }

        FORCE_INLINE const RegSBYCR &SBYCR() const {
            return m_sh2.SBYCR;
        }

        FORCE_INLINE DMAChannel &DMAC0() {
            return m_sh2.m_dmaChannels[0];
        }

        FORCE_INLINE const DMAChannel &DMAC0() const {
            return m_sh2.m_dmaChannels[0];
        }

        FORCE_INLINE DMAChannel &DMAC1() {
            return m_sh2.m_dmaChannels[1];
        }

        FORCE_INLINE const DMAChannel &DMAC1() const {
            return m_sh2.m_dmaChannels[1];
        }

        FORCE_INLINE RegDMAOR &DMAOR() {
            return m_sh2.DMAOR;
        }

        FORCE_INLINE const RegDMAOR &DMAOR() const {
            return m_sh2.DMAOR;
        }

        // ---------------------------------------------------------------------
        // Cache

        Cache &GetCache() {
            return m_sh2.m_cache;
        }

        const Cache &GetCache() const {
            return m_sh2.m_cache;
        }

        // ---------------------------------------------------------------------
        // Division unit

        void ExecuteDiv32();
        void ExecuteDiv64();

        // ---------------------------------------------------------------------
        // Interrupts

        // Raise an interrupt, also setting the corresponding signals.
        FORCE_INLINE void RaiseInterrupt(InterruptSource source) {
            // Set the corresponding signals
            switch (source) {
            case InterruptSource::None: break;
            case InterruptSource::FRT_OVI:
                m_sh2.FRT.FTCSR.OVF = 1;
                m_sh2.FRT.TIER.OVIE = 1;
                break;
            case InterruptSource::FRT_OCI:
                m_sh2.FRT.FTCSR.OCFA = 1;
                m_sh2.FRT.TIER.OCIAE = 1;
                break;
            case InterruptSource::FRT_ICI:
                m_sh2.FRT.FTCSR.ICF = 1;
                m_sh2.FRT.TIER.ICIE = 1;
                break;
            case InterruptSource::SCI_TEI: /*TODO*/ break;
            case InterruptSource::SCI_TXI: /*TODO*/ break;
            case InterruptSource::SCI_RXI: /*TODO*/ break;
            case InterruptSource::SCI_ERI: /*TODO*/ break;
            case InterruptSource::BSC_REF_CMI: /*TODO*/ break;
            case InterruptSource::WDT_ITI:
                m_sh2.WDT.WTCSR.OVF = 1;
                m_sh2.WDT.WTCSR.WT_nIT = 0;
                break;
            case InterruptSource::DMAC1_XferEnd:
                m_sh2.m_dmaChannels[1].xferEnded = true;
                m_sh2.m_dmaChannels[1].irqEnable = true;
                break;
            case InterruptSource::DMAC0_XferEnd:
                m_sh2.m_dmaChannels[0].xferEnded = true;
                m_sh2.m_dmaChannels[0].irqEnable = true;
                break;
            case InterruptSource::DIVU_OVFI:
                m_sh2.DIVU.DVCR.OVF = 1;
                m_sh2.DIVU.DVCR.OVFIE = 1;
                break;
            case InterruptSource::IRL: /*relies on level being set*/ break;
            case InterruptSource::UserBreak: /*TODO*/ break;
            case InterruptSource::NMI: m_sh2.INTC.NMI = 1; break;
            }

            m_sh2.RaiseInterrupt(source);
        }

        // Lower an interrupt, also clearing the corresponding signals.
        FORCE_INLINE void LowerInterrupt(InterruptSource source) {
            // Clear the corresponding signals
            switch (source) {
            case InterruptSource::None: break;
            case InterruptSource::FRT_OVI: m_sh2.FRT.FTCSR.OVF = 0; break;
            case InterruptSource::FRT_OCI: m_sh2.FRT.FTCSR.OCFA = 0; break;
            case InterruptSource::FRT_ICI: m_sh2.FRT.FTCSR.ICF = 0; break;
            case InterruptSource::SCI_TEI: /*TODO*/ break;
            case InterruptSource::SCI_TXI: /*TODO*/ break;
            case InterruptSource::SCI_RXI: /*TODO*/ break;
            case InterruptSource::SCI_ERI: /*TODO*/ break;
            case InterruptSource::BSC_REF_CMI: /*TODO*/ break;
            case InterruptSource::WDT_ITI: m_sh2.WDT.WTCSR.OVF = 0; break;
            case InterruptSource::DMAC1_XferEnd: m_sh2.m_dmaChannels[1].xferEnded = false; break;
            case InterruptSource::DMAC0_XferEnd: m_sh2.m_dmaChannels[0].xferEnded = false; break;
            case InterruptSource::DIVU_OVFI: m_sh2.DIVU.DVCR.OVF = 0; break;
            case InterruptSource::IRL:
                m_sh2.INTC.SetLevel(sh2::InterruptSource::IRL, 0x0);
                m_sh2.INTC.UpdateIRLVector();
                break;
            case InterruptSource::UserBreak: /*TODO*/ break;
            case InterruptSource::NMI: m_sh2.INTC.NMI = 0; break;
            }

            m_sh2.LowerInterrupt(source);
        }

        // Determines if the given interrupt source signal is raised.
        FORCE_INLINE bool IsInterruptRaised(InterruptSource source) const {
            switch (source) {
            case InterruptSource::None: return false;
            case InterruptSource::FRT_OVI: return m_sh2.FRT.FTCSR.OVF && m_sh2.FRT.TIER.OVIE;
            case InterruptSource::FRT_OCI:
                return (m_sh2.FRT.FTCSR.OCFA && m_sh2.FRT.TIER.OCIAE) || (m_sh2.FRT.FTCSR.OCFB && m_sh2.FRT.TIER.OCIBE);
            case InterruptSource::FRT_ICI: return m_sh2.FRT.FTCSR.ICF && m_sh2.FRT.TIER.ICIE;
            case InterruptSource::SCI_TEI: return false;     // TODO
            case InterruptSource::SCI_TXI: return false;     // TODO
            case InterruptSource::SCI_RXI: return false;     // TODO
            case InterruptSource::SCI_ERI: return false;     // TODO
            case InterruptSource::BSC_REF_CMI: return false; // TODO
            case InterruptSource::WDT_ITI: return m_sh2.WDT.WTCSR.OVF && !m_sh2.WDT.WTCSR.WT_nIT;
            case InterruptSource::DMAC1_XferEnd:
                return m_sh2.m_dmaChannels[1].xferEnded && m_sh2.m_dmaChannels[1].irqEnable;
            case InterruptSource::DMAC0_XferEnd:
                return m_sh2.m_dmaChannels[0].xferEnded && m_sh2.m_dmaChannels[0].irqEnable;
            case InterruptSource::DIVU_OVFI: return m_sh2.DIVU.DVCR.OVF && m_sh2.DIVU.DVCR.OVFIE;
            case InterruptSource::IRL: return m_sh2.INTC.GetLevel(InterruptSource::IRL) > 0;
            case InterruptSource::UserBreak: return false; // TODO
            case InterruptSource::NMI: return m_sh2.INTC.NMI;
            default: return false;
            }
        }

        // Check if the CPU should service an interrupt.
        // Takes into account the current SR.ILevel and delay slot state.
        FORCE_INLINE bool CheckInterrupts() const {
            return m_sh2.m_intrPending;
        }

    private:
        SH2 &m_sh2;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    // -------------------------------------------------------------------------
    // CPU state

    // R0 through R15.
    // R15 is also used as the hardware stack pointer (SP).
    alignas(64) std::array<uint32, 16> R;

    uint32 PC;
    uint32 PR;

    RegMAC MAC;

    RegSR SR;

    uint32 GBR;
    uint32 VBR;

    uint32 m_delaySlotTarget;
    bool m_delaySlot;

    CBAcknowledgeExternalInterrupt m_cbAcknowledgeExternalInterrupt;
    debug::DebugBreakManager *m_debugBreakMgr = nullptr;

    // -------------------------------------------------------------------------
    // Cycle counting

    core::Scheduler &m_scheduler;

    // Number of cycles executed in the current Advance invocation
    uint64 m_cyclesExecuted;

    // Retrieves the current absolute cycle count
    uint64 GetCurrentCycleCount() const;

    // -------------------------------------------------------------------------
    // Memory accessors

    sys::SH2Bus &m_bus;
    const sys::SystemFeatures &m_systemFeatures;

    // According to the SH7604/SH7095 manuals, the address space is divided into these areas:
    //
    // Address range            Space                           Memory
    // 0x00000000..0x01FFFFFF   CS0 space, cache area           Ordinary space or burst ROM
    // 0x02000000..0x03FFFFFF   CS1 space, cache area           Ordinary space
    // 0x04000000..0x05FFFFFF   CS2 space, cache area           Ordinary space or synchronous DRAM
    // 0x06000000..0x07FFFFFF   CS3 space, cache area           Ordinary space, synchronous SDRAM, DRAM or pseudo-DRAM
    // 0x08000000..0x1FFFFFFF   Reserved
    // 0x20000000..0x21FFFFFF   CS0 space, cache-through area   Ordinary space or burst ROM
    // 0x22000000..0x23FFFFFF   CS1 space, cache-through area   Ordinary space
    // 0x24000000..0x25FFFFFF   CS2 space, cache-through area   Ordinary space or synchronous DRAM
    // 0x26000000..0x27FFFFFF   CS3 space, cache-through area   Ordinary space, synchronous SDRAM, DRAM or pseudo-DRAM
    // 0x28000000..0x3FFFFFFF   Reserved
    // 0x40000000..0x47FFFFFF   Associative purge space
    // 0x48000000..0x5FFFFFFF   Reserved
    // 0x60000000..0x7FFFFFFF   Address array, read/write space
    // 0x80000000..0x9FFFFFFF   Reserved  [undocumented mirror of 0xC0000000..0xDFFFFFFF]
    // 0xA0000000..0xBFFFFFFF   Reserved  [undocumented mirror of 0x20000000..0x3FFFFFFF]
    // 0xC0000000..0xC0000FFF   Data array, read/write space
    // 0xC0001000..0xDFFFFFFF   Reserved
    // 0xE0000000..0xFFFF7FFF   Reserved
    // 0xFFFF8000..0xFFFFBFFF   For setting synchronous DRAM mode
    // 0xFFFFC000..0xFFFFFDFF   Reserved
    // 0xFFFFFE00..0xFFFFFFFF   On-chip peripheral modules
    //
    // The cache uses address bits 31..29 to specify its behavior:
    //    Bits  Partition                       Cache operation
    //    000   Cache area                      Cache used when CCR.CE=1
    //    001   Cache-through area              Cache bypassed
    //    010   Associative purge area          Purge accessed cache lines (reads return 0x2312)
    //    011   Address array read/write area   Cache addresses acessed directly (1 KiB, mirrored)
    //    100   [undocumented, same as 110]
    //    101   [undocumented, same as 001]
    //    110   Data array read/write area      Cache data acessed directly (4 KiB, mirrored)
    //    111   I/O area (on-chip registers)    Cache bypassed

    template <mem_primitive T, bool instrFetch, bool peek, bool enableCache>
    T MemRead(uint32 address);

    template <mem_primitive T, bool poke, bool debug, bool enableCache>
    void MemWrite(uint32 address, T value);

    template <bool enableCache>
    uint16 FetchInstruction(uint32 address);

    template <bool enableCache>
    uint8 MemReadByte(uint32 address);
    template <bool enableCache>
    uint16 MemReadWord(uint32 address);
    template <bool enableCache>
    uint32 MemReadLong(uint32 address);

    template <bool debug, bool enableCache>
    void MemWriteByte(uint32 address, uint8 value);
    template <bool debug, bool enableCache>
    void MemWriteWord(uint32 address, uint16 value);
    template <bool debug, bool enableCache>
    void MemWriteLong(uint32 address, uint32 value);

    template <bool enableCache>
    uint16 PeekInstruction(uint32 address);

    template <bool enableCache>
    uint8 MemPeekByte(uint32 address);
    template <bool enableCache>
    uint16 MemPeekWord(uint32 address);
    template <bool enableCache>
    uint32 MemPeekLong(uint32 address);

    template <bool enableCache>
    void MemPokeByte(uint32 address, uint8 value);
    template <bool enableCache>
    void MemPokeWord(uint32 address, uint16 value);
    template <bool enableCache>
    void MemPokeLong(uint32 address, uint32 value);

    // Returns 00 00 00 01 00 02 00 03 00 04 00 05 00 06 00 07 ... repeating
    template <mem_primitive T>
    T OpenBusSeqRead(uint32 address);

    template <bool write, bool enableCache>
    uint64 AccessCycles(uint32 address);

    // -------------------------------------------------------------------------
    // On-chip peripherals

    template <mem_primitive T, bool peek>
    T OnChipRegRead(uint32 address);

    template <bool peek>
    uint8 OnChipRegReadByte(uint32 address);
    template <bool peek>
    uint16 OnChipRegReadWord(uint32 address);
    template <bool peek>
    uint32 OnChipRegReadLong(uint32 address);

    template <mem_primitive T, bool poke, bool debug, bool enableCache>
    void OnChipRegWrite(uint32 address, T value);

    template <bool poke, bool debug, bool enableCache>
    void OnChipRegWriteByte(uint32 address, uint8 value);
    template <bool poke, bool debug, bool enableCache>
    void OnChipRegWriteWord(uint32 address, uint16 value);
    template <bool poke, bool debug, bool enableCache>
    void OnChipRegWriteLong(uint32 address, uint32 value);

    // --- SCI module ---

    // TODO

    // --- BSC module ---

    RegBCR1 BCR1;   // 1E0  R/W  16,32    03F0      BCR1    Bus Control Register 1
    RegBCR2 BCR2;   // 1E4  R/W  16,32    00FC      BCR2    Bus Control Register 2
    RegWCR WCR;     // 1E8  R/W  16,32    AAFF      WCR     Wait Control Register
    RegMCR MCR;     // 1EC  R/W  16,32    0000      MCR     Individual Memory Control Register
    RegRTCSR RTCSR; // 1F0  R/W  16,32    0000      RTCSR   Refresh Timer Control/Status Register
    RegRTCNT RTCNT; // 1F4  R/W  16,32    0000      RTCNT   Refresh Timer Counter
    RegRTCOR RTCOR; // 1F8  R/W  16,32    0000      RTCOR   Refresh Timer Constant Register

    // --- DMAC module ---

    RegDMAOR DMAOR;
    std::array<DMAChannel, 2> m_dmaChannels;

    // Determines if a DMA transfer is active for the specified channel.
    // A transfer is active if DE = 1, DME = 1, TE = 0, NMIF = 0 and AE = 0.
    bool IsDMATransferActive(const DMAChannel &ch) const;

    template <bool debug, bool enableCache>
    bool StepDMAC(uint32 channel);

    template <bool debug, bool enableCache>
    void AdvanceDMA(uint64 cycles);

    // --- WDT module ---

    WatchdogTimer WDT;
    uint8 m_WDTBusValue;

    template <bool write>
    void AdvanceWDT();

    // --- Power-down module ---

    RegSBYCR SBYCR; // 091  R/W  8        00        SBYCR   Standby Control Register
    bool m_sleep;

    // --- DIVU module ---

    DivisionUnit DIVU;

    template <bool debug>
    void ExecuteDiv32();
    template <bool debug>
    void ExecuteDiv64();

    // --- UBC module ---

    // TODO: implement (channels A and B)

    // --- FRT module ---

    FreeRunningTimer FRT;

    template <bool write>
    void AdvanceFRT();

    void TriggerFRTInputCapture();

    // -------------------------------------------------------------------------
    // Interrupts

    InterruptController INTC;

    void SetExternalInterrupt(uint8 level, uint8 vecNum);

    // Raises the interrupt signal of the specified source.
    FORCE_INLINE void RaiseInterrupt(InterruptSource source) {
        const uint8 level = INTC.GetLevel(source);
        if (level < INTC.pending.level) {
            return;
        }
        if (level == INTC.pending.level && static_cast<uint8>(source) < static_cast<uint8>(INTC.pending.source)) {
            return;
        }
        INTC.pending.level = level;
        INTC.pending.source = source;
        m_intrPending = !m_delaySlot && INTC.pending.level > SR.ILevel;
    }

    // Lowers the interrupt signal of the specified source.
    FORCE_INLINE void LowerInterrupt(InterruptSource source) {
        if (INTC.pending.source == source) {
            RecalcInterrupts();
        }
    }

    // Updates the pending interrupt level if it matches one of the specified sources.
    template <InterruptSource source, InterruptSource... sources>
    void UpdateInterruptLevels();

    // Recalculates the highest priority interrupt to be serviced.
    void RecalcInterrupts();

    // Whether an interrupt should be serviced on the next instruction:
    //   !m_delaySlot && INTC.pending.level > SR.ILevel
    // This value is updated when any of these variables is changed, which happens less often than once per instruction.
    // There's no need to store this in the save state struct since its value can be derived as above.
    bool m_intrPending;

    // -------------------------------------------------------------------------
    // Cache

    Cache m_cache;

    // -------------------------------------------------------------------------
    // Debugger

    Probe m_probe{*this};
    debug::ISH2Tracer *m_tracer = nullptr;

    std::array<bool, 2> m_dmacTraced; // whether each DMA channel has had a transfer traced

    std::set<uint32> m_breakpoints;
    std::map<uint32, debug::WatchpointFlags> m_watchpoints;

    bool m_debugSuspend = false; // Disables CPU while in debug mode

    bool CheckBreakpoint();
    bool CheckWatchpoints(const DecodedMemAccesses &mem);
    bool CheckWatchpoint(const DecodedMemAccesses::Access &access);

    const std::string_view m_logPrefix; // For devlogs

    // -------------------------------------------------------------------------
    // Helper functions

    void SetupDelaySlot(uint32 targetAddress);

    template <bool delaySlot>
    void AdvancePC();

    template <bool debug, bool enableCache>
    uint64 EnterException(uint8 vectorNumber);

    // -------------------------------------------------------------------------
    // Instruction interpreters

    // Interprets the next instruction.
    // Returns the number of cycles executed.
    template <bool debug, bool enableCache>
    uint64 InterpretNext();

#define TPL_TRAITS template <bool debug, bool enableCache>
#define TPL_TRAITS_DS template <bool debug, bool enableCache, bool delaySlot>
#define TPL_DS template <bool delaySlot>

    TPL_DS uint64 NOP(); // nop

    uint64 SLEEP(); // sleep

    TPL_DS uint64 MOV(const DecodedArgs &args);           // mov   Rm, Rn
    TPL_TRAITS_DS uint64 MOVBL(const DecodedArgs &args);  // mov.b @Rm, Rn
    TPL_TRAITS_DS uint64 MOVWL(const DecodedArgs &args);  // mov.w @Rm, Rn
    TPL_TRAITS_DS uint64 MOVLL(const DecodedArgs &args);  // mov.l @Rm, Rn
    TPL_TRAITS_DS uint64 MOVBL0(const DecodedArgs &args); // mov.b @(R0,Rm), Rn
    TPL_TRAITS_DS uint64 MOVWL0(const DecodedArgs &args); // mov.w @(R0,Rm), Rn
    TPL_TRAITS_DS uint64 MOVLL0(const DecodedArgs &args); // mov.l @(R0,Rm), Rn
    TPL_TRAITS_DS uint64 MOVBL4(const DecodedArgs &args); // mov.b @(disp,Rm), R0
    TPL_TRAITS_DS uint64 MOVWL4(const DecodedArgs &args); // mov.w @(disp,Rm), R0
    TPL_TRAITS_DS uint64 MOVLL4(const DecodedArgs &args); // mov.l @(disp,Rm), Rn
    TPL_TRAITS_DS uint64 MOVBLG(const DecodedArgs &args); // mov.b @(disp,GBR), R0
    TPL_TRAITS_DS uint64 MOVWLG(const DecodedArgs &args); // mov.w @(disp,GBR), R0
    TPL_TRAITS_DS uint64 MOVLLG(const DecodedArgs &args); // mov.l @(disp,GBR), R0
    TPL_TRAITS_DS uint64 MOVBM(const DecodedArgs &args);  // mov.b Rm, @-Rn
    TPL_TRAITS_DS uint64 MOVWM(const DecodedArgs &args);  // mov.w Rm, @-Rn
    TPL_TRAITS_DS uint64 MOVLM(const DecodedArgs &args);  // mov.l Rm, @-Rn
    TPL_TRAITS_DS uint64 MOVBP(const DecodedArgs &args);  // mov.b @Rm+, Rn
    TPL_TRAITS_DS uint64 MOVWP(const DecodedArgs &args);  // mov.w @Rm+, Rn
    TPL_TRAITS_DS uint64 MOVLP(const DecodedArgs &args);  // mov.l @Rm+, Rn
    TPL_TRAITS_DS uint64 MOVBS(const DecodedArgs &args);  // mov.b Rm, @Rn
    TPL_TRAITS_DS uint64 MOVWS(const DecodedArgs &args);  // mov.w Rm, @Rn
    TPL_TRAITS_DS uint64 MOVLS(const DecodedArgs &args);  // mov.l Rm, @Rn
    TPL_TRAITS_DS uint64 MOVBS0(const DecodedArgs &args); // mov.b Rm, @(R0,Rn)
    TPL_TRAITS_DS uint64 MOVWS0(const DecodedArgs &args); // mov.w Rm, @(R0,Rn)
    TPL_TRAITS_DS uint64 MOVLS0(const DecodedArgs &args); // mov.l Rm, @(R0,Rn)
    TPL_TRAITS_DS uint64 MOVBS4(const DecodedArgs &args); // mov.b R0, @(disp,Rn)
    TPL_TRAITS_DS uint64 MOVWS4(const DecodedArgs &args); // mov.w R0, @(disp,Rn)
    TPL_TRAITS_DS uint64 MOVLS4(const DecodedArgs &args); // mov.l Rm, @(disp,Rn)
    TPL_TRAITS_DS uint64 MOVBSG(const DecodedArgs &args); // mov.b R0, @(disp,GBR)
    TPL_TRAITS_DS uint64 MOVWSG(const DecodedArgs &args); // mov.w R0, @(disp,GBR)
    TPL_TRAITS_DS uint64 MOVLSG(const DecodedArgs &args); // mov.l R0, @(disp,GBR)
    TPL_DS uint64 MOVI(const DecodedArgs &args);          // mov   #imm, Rn
    TPL_TRAITS_DS uint64 MOVWI(const DecodedArgs &args);  // mov.w @(disp,PC), Rn
    TPL_TRAITS_DS uint64 MOVLI(const DecodedArgs &args);  // mov.l @(disp,PC), Rn
    TPL_DS uint64 MOVA(const DecodedArgs &args);          // mova  @(disp,PC), R0
    TPL_DS uint64 MOVT(const DecodedArgs &args);          // movt  Rn
    TPL_DS uint64 CLRT();                                 // clrt
    TPL_DS uint64 SETT();                                 // sett

    TPL_DS uint64 EXTSB(const DecodedArgs &args); // exts.b Rm, Rn
    TPL_DS uint64 EXTSW(const DecodedArgs &args); // exts.w Rm, Rn
    TPL_DS uint64 EXTUB(const DecodedArgs &args); // extu.b Rm, Rn
    TPL_DS uint64 EXTUW(const DecodedArgs &args); // extu.w Rm, Rn
    TPL_DS uint64 SWAPB(const DecodedArgs &args); // swap.b Rm, Rn
    TPL_DS uint64 SWAPW(const DecodedArgs &args); // swap.w Rm, Rn
    TPL_DS uint64 XTRCT(const DecodedArgs &args); // xtrct  Rm, Rn

    TPL_DS uint64 LDCGBR(const DecodedArgs &args);          // ldc   Rm, GBR
    TPL_DS uint64 LDCSR(const DecodedArgs &args);           // ldc   Rm, SR
    TPL_DS uint64 LDCVBR(const DecodedArgs &args);          // ldc   Rm, VBR
    TPL_DS uint64 LDSMACH(const DecodedArgs &args);         // lds   Rm, MACH
    TPL_DS uint64 LDSMACL(const DecodedArgs &args);         // lds   Rm, MACL
    TPL_DS uint64 LDSPR(const DecodedArgs &args);           // lds   Rm, PR
    TPL_DS uint64 STCGBR(const DecodedArgs &args);          // stc   GBR, Rn
    TPL_DS uint64 STCSR(const DecodedArgs &args);           // stc   SR, Rn
    TPL_DS uint64 STCVBR(const DecodedArgs &args);          // stc   VBR, Rn
    TPL_DS uint64 STSMACH(const DecodedArgs &args);         // sts   MACH, Rn
    TPL_DS uint64 STSMACL(const DecodedArgs &args);         // sts   MACL, Rn
    TPL_DS uint64 STSPR(const DecodedArgs &args);           // sts   PR, Rn
    TPL_TRAITS_DS uint64 LDCMGBR(const DecodedArgs &args);  // ldc.l @Rm+, GBR
    TPL_TRAITS_DS uint64 LDCMSR(const DecodedArgs &args);   // ldc.l @Rm+, SR
    TPL_TRAITS_DS uint64 LDCMVBR(const DecodedArgs &args);  // ldc.l @Rm+, VBR
    TPL_TRAITS_DS uint64 LDSMMACH(const DecodedArgs &args); // lds.l @Rm+, MACH
    TPL_TRAITS_DS uint64 LDSMMACL(const DecodedArgs &args); // lds.l @Rm+, MACL
    TPL_TRAITS_DS uint64 LDSMPR(const DecodedArgs &args);   // lds.l @Rm+, PR
    TPL_TRAITS_DS uint64 STCMGBR(const DecodedArgs &args);  // stc.l GBR, @-Rn
    TPL_TRAITS_DS uint64 STCMSR(const DecodedArgs &args);   // stc.l SR, @-Rn
    TPL_TRAITS_DS uint64 STCMVBR(const DecodedArgs &args);  // stc.l VBR, @-Rn
    TPL_TRAITS_DS uint64 STSMMACH(const DecodedArgs &args); // sts.l MACH, @-Rn
    TPL_TRAITS_DS uint64 STSMMACL(const DecodedArgs &args); // sts.l MACL, @-Rn
    TPL_TRAITS_DS uint64 STSMPR(const DecodedArgs &args);   // sts.l PR, @-Rn

    TPL_DS uint64 ADD(const DecodedArgs &args);         // add    Rm, Rn
    TPL_DS uint64 ADDI(const DecodedArgs &args);        // add    imm, Rn
    TPL_DS uint64 ADDC(const DecodedArgs &args);        // addc   Rm, Rn
    TPL_DS uint64 ADDV(const DecodedArgs &args);        // addv   Rm, Rn
    TPL_DS uint64 AND(const DecodedArgs &args);         // and    Rm, Rn
    TPL_DS uint64 ANDI(const DecodedArgs &args);        // and    imm, R0
    TPL_TRAITS_DS uint64 ANDM(const DecodedArgs &args); // and.   b imm, @(R0,GBR)
    TPL_DS uint64 NEG(const DecodedArgs &args);         // neg    Rm, Rn
    TPL_DS uint64 NEGC(const DecodedArgs &args);        // negc   Rm, Rn
    TPL_DS uint64 NOT(const DecodedArgs &args);         // not    Rm, Rn
    TPL_DS uint64 OR(const DecodedArgs &args);          // or     Rm, Rn
    TPL_DS uint64 ORI(const DecodedArgs &args);         // or     imm, Rn
    TPL_TRAITS_DS uint64 ORM(const DecodedArgs &args);  // or.b   imm, @(R0,GBR)
    TPL_DS uint64 ROTCL(const DecodedArgs &args);       // rotcl  Rn
    TPL_DS uint64 ROTCR(const DecodedArgs &args);       // rotcr  Rn
    TPL_DS uint64 ROTL(const DecodedArgs &args);        // rotl   Rn
    TPL_DS uint64 ROTR(const DecodedArgs &args);        // rotr   Rn
    TPL_DS uint64 SHAL(const DecodedArgs &args);        // shal   Rn
    TPL_DS uint64 SHAR(const DecodedArgs &args);        // shar   Rn
    TPL_DS uint64 SHLL(const DecodedArgs &args);        // shll   Rn
    TPL_DS uint64 SHLL2(const DecodedArgs &args);       // shll2  Rn
    TPL_DS uint64 SHLL8(const DecodedArgs &args);       // shll8  Rn
    TPL_DS uint64 SHLL16(const DecodedArgs &args);      // shll16 Rn
    TPL_DS uint64 SHLR(const DecodedArgs &args);        // shlr   Rn
    TPL_DS uint64 SHLR2(const DecodedArgs &args);       // shlr2  Rn
    TPL_DS uint64 SHLR8(const DecodedArgs &args);       // shlr8  Rn
    TPL_DS uint64 SHLR16(const DecodedArgs &args);      // shlr16 Rn
    TPL_DS uint64 SUB(const DecodedArgs &args);         // sub    Rm, Rn
    TPL_DS uint64 SUBC(const DecodedArgs &args);        // subc   Rm, Rn
    TPL_DS uint64 SUBV(const DecodedArgs &args);        // subv   Rm, Rn
    TPL_DS uint64 XOR(const DecodedArgs &args);         // xor    Rm, Rn
    TPL_DS uint64 XORI(const DecodedArgs &args);        // xor    imm, Rn
    TPL_TRAITS_DS uint64 XORM(const DecodedArgs &args); // xor.b  imm, @(R0,GBR)

    TPL_DS uint64 DT(const DecodedArgs &args); // dt Rn

    TPL_DS uint64 CLRMAC();                             // clrmac
    TPL_TRAITS_DS uint64 MACW(const DecodedArgs &args); // mac.w   @Rm+, @Rn+
    TPL_TRAITS_DS uint64 MACL(const DecodedArgs &args); // mac.l   @Rm+, @Rn+
    TPL_DS uint64 MULL(const DecodedArgs &args);        // mul.l   Rm, Rn
    TPL_DS uint64 MULS(const DecodedArgs &args);        // muls.w  Rm, Rn
    TPL_DS uint64 MULU(const DecodedArgs &args);        // mulu.w  Rm, Rn
    TPL_DS uint64 DMULS(const DecodedArgs &args);       // dmuls.l Rm, Rn
    TPL_DS uint64 DMULU(const DecodedArgs &args);       // dmulu.l Rm, Rn

    TPL_DS uint64 DIV0S(const DecodedArgs &args); // div0s Rm, Rn
    TPL_DS uint64 DIV0U();                        // div0u
    TPL_DS uint64 DIV1(const DecodedArgs &args);  // div1  Rm, Rn

    TPL_DS uint64 CMPIM(const DecodedArgs &args);       // cmp/eq  imm, R0
    TPL_DS uint64 CMPEQ(const DecodedArgs &args);       // cmp/eq  Rm, Rn
    TPL_DS uint64 CMPGE(const DecodedArgs &args);       // cmp/ge  Rm, Rn
    TPL_DS uint64 CMPGT(const DecodedArgs &args);       // cmp/gt  Rm, Rn
    TPL_DS uint64 CMPHI(const DecodedArgs &args);       // cmp/hi  Rm, Rn
    TPL_DS uint64 CMPHS(const DecodedArgs &args);       // cmp/hs  Rm, Rn
    TPL_DS uint64 CMPPL(const DecodedArgs &args);       // cmp/pl  Rn
    TPL_DS uint64 CMPPZ(const DecodedArgs &args);       // cmp/pz  Rn
    TPL_DS uint64 CMPSTR(const DecodedArgs &args);      // cmp/str Rm, Rn
    TPL_TRAITS_DS uint64 TAS(const DecodedArgs &args);  // tas.b   @Rn
    TPL_DS uint64 TST(const DecodedArgs &args);         // tst     Rm, Rn
    TPL_DS uint64 TSTI(const DecodedArgs &args);        // tst     imm, R0
    TPL_TRAITS_DS uint64 TSTM(const DecodedArgs &args); // tst.b   imm, @(R0,GBR)

    uint64 BF(const DecodedArgs &args);               // bf    disp
    uint64 BFS(const DecodedArgs &args);              // bf/s  disp
    uint64 BT(const DecodedArgs &args);               // bt    disp
    uint64 BTS(const DecodedArgs &args);              // bt/s  disp
    uint64 BRA(const DecodedArgs &args);              // bra   disp
    uint64 BRAF(const DecodedArgs &args);             // braf  Rm
    uint64 BSR(const DecodedArgs &args);              // bsr   disp
    uint64 BSRF(const DecodedArgs &args);             // bsrf  Rm
    uint64 JMP(const DecodedArgs &args);              // jmp   @Rm
    uint64 JSR(const DecodedArgs &args);              // jsr   @Rm
    TPL_TRAITS uint64 TRAPA(const DecodedArgs &args); // trapa imm

    TPL_TRAITS uint64 RTE(); // rte
    uint64 RTS();            // rts

#undef TPL_TRAITS
#undef TPL_TRAITS_DS
#undef TPL_DS

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const scu::CBExternalInterrupt CbExtIntr = util::MakeClassMemberRequiredCallback<&SH2::SetExternalInterrupt>(this);
};

} // namespace ymir::sh2
