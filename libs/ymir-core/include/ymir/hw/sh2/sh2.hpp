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

#include <ymir/sys/bus.hpp>
#include <ymir/sys/system_features.hpp>

#include <ymir/state/state_sh2.hpp>

#include <ymir/debug/sh2_tracer_base.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/inline.hpp>

#include <array>
#include <iosfwd>

namespace ymir::sh2 {

// -----------------------------------------------------------------------------

class SH2 {
public:
    SH2(sys::Bus &bus, bool master, const sys::SystemFeatures &systemFeatures);

    void Reset(bool hard, bool watchdogInitiated = false);

    void MapCallbacks(CBAcknowledgeExternalInterrupt callback) {
        m_cbAcknowledgeExternalInterrupt = callback;
    }

    void MapMemory(sys::Bus &bus);

    void DumpCacheData(std::ostream &out) const;
    void DumpCacheAddressTag(std::ostream &out) const;

    // -------------------------------------------------------------------------
    // Usage

    // Advances the SH2 for at least the specified number of cycles.
    // Returns the total number of cycles executed.
    template <bool debug, bool enableCache>
    uint64 Advance(uint64 cycles);

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
    bool ValidateState(const state::SH2State &state) const;
    void LoadState(const state::SH2State &state);

    // -------------------------------------------------------------------------
    // Debugger

    // Attaches the specified tracer to this component.
    // Pass nullptr to disable tracing.
    void UseTracer(debug::ISH2Tracer *tracer) {
        m_tracer = tracer;
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

        // Advance FRT with side-effects
        FORCE_INLINE void AdvanceFRT(uint64 cycles) {
            m_sh2.AdvanceFRT(cycles);
        }

        FORCE_INLINE WatchdogTimer &WDT() {
            return m_sh2.WDT;
        }

        FORCE_INLINE const WatchdogTimer &WDT() const {
            return m_sh2.WDT;
        }

        // Advance WDT with side-effects
        FORCE_INLINE void AdvanceWDT(uint64 cycles) {
            m_sh2.AdvanceWDT(cycles);
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
        // Takes into account the current SR.ILevel.
        FORCE_INLINE bool CheckInterrupts() const {
            return m_sh2.CheckInterrupts();
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

    // [1] fetch
    // [0] decode
    std::array<uint16, 2> m_fetchQueue;

    CBAcknowledgeExternalInterrupt m_cbAcknowledgeExternalInterrupt;

    // -------------------------------------------------------------------------
    // Memory accessors

    sys::Bus &m_bus;
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
    void FillPipeline();

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
    void RunDMAC(uint32 channel);

    // --- WDT module ---

    WatchdogTimer WDT;

    void AdvanceWDT(uint64 cycles);

    // --- Power-down module ---

    RegSBYCR SBYCR; // 091  R/W  8        00        SBYCR   Standby Control Register

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

    void AdvanceFRT(uint64 cycles);

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

    // Checks if the CPU should service an interrupt.
    FORCE_INLINE bool CheckInterrupts() const {
        return !m_delaySlot && INTC.pending.level > SR.ILevel;
    }

    // -------------------------------------------------------------------------
    // Cache

    Cache m_cache;

    // -------------------------------------------------------------------------
    // Debugger

    Probe m_probe{*this};
    debug::ISH2Tracer *m_tracer = nullptr;

    const std::string_view m_logPrefix;

    // -------------------------------------------------------------------------
    // Helper functions

    void SetupDelaySlot(uint32 targetAddress);

    template <bool debug, bool enableCache>
    void EnterException(uint8 vectorNumber);

    // -------------------------------------------------------------------------
    // Instruction interpreters

    // Interprets the next instruction.
    // Returns the number of cycles executed.
    template <bool debug, bool enableCache>
    uint64 InterpretNext();

#define TPL_TRAITS template <bool debug, bool enableCache>

    void NOP(); // nop

    void SLEEP(); // sleep

    void MOV(const DecodedArgs &args);               // mov   Rm, Rn
    TPL_TRAITS void MOVBL(const DecodedArgs &args);  // mov.b @Rm, Rn
    TPL_TRAITS void MOVWL(const DecodedArgs &args);  // mov.w @Rm, Rn
    TPL_TRAITS void MOVLL(const DecodedArgs &args);  // mov.l @Rm, Rn
    TPL_TRAITS void MOVBL0(const DecodedArgs &args); // mov.b @(R0,Rm), Rn
    TPL_TRAITS void MOVWL0(const DecodedArgs &args); // mov.w @(R0,Rm), Rn
    TPL_TRAITS void MOVLL0(const DecodedArgs &args); // mov.l @(R0,Rm), Rn
    TPL_TRAITS void MOVBL4(const DecodedArgs &args); // mov.b @(disp,Rm), R0
    TPL_TRAITS void MOVWL4(const DecodedArgs &args); // mov.w @(disp,Rm), R0
    TPL_TRAITS void MOVLL4(const DecodedArgs &args); // mov.l @(disp,Rm), Rn
    TPL_TRAITS void MOVBLG(const DecodedArgs &args); // mov.b @(disp,GBR), R0
    TPL_TRAITS void MOVWLG(const DecodedArgs &args); // mov.w @(disp,GBR), R0
    TPL_TRAITS void MOVLLG(const DecodedArgs &args); // mov.l @(disp,GBR), R0
    TPL_TRAITS void MOVBM(const DecodedArgs &args);  // mov.b Rm, @-Rn
    TPL_TRAITS void MOVWM(const DecodedArgs &args);  // mov.w Rm, @-Rn
    TPL_TRAITS void MOVLM(const DecodedArgs &args);  // mov.l Rm, @-Rn
    TPL_TRAITS void MOVBP(const DecodedArgs &args);  // mov.b @Rm+, Rn
    TPL_TRAITS void MOVWP(const DecodedArgs &args);  // mov.w @Rm+, Rn
    TPL_TRAITS void MOVLP(const DecodedArgs &args);  // mov.l @Rm+, Rn
    TPL_TRAITS void MOVBS(const DecodedArgs &args);  // mov.b Rm, @Rn
    TPL_TRAITS void MOVWS(const DecodedArgs &args);  // mov.w Rm, @Rn
    TPL_TRAITS void MOVLS(const DecodedArgs &args);  // mov.l Rm, @Rn
    TPL_TRAITS void MOVBS0(const DecodedArgs &args); // mov.b Rm, @(R0,Rn)
    TPL_TRAITS void MOVWS0(const DecodedArgs &args); // mov.w Rm, @(R0,Rn)
    TPL_TRAITS void MOVLS0(const DecodedArgs &args); // mov.l Rm, @(R0,Rn)
    TPL_TRAITS void MOVBS4(const DecodedArgs &args); // mov.b R0, @(disp,Rn)
    TPL_TRAITS void MOVWS4(const DecodedArgs &args); // mov.w R0, @(disp,Rn)
    TPL_TRAITS void MOVLS4(const DecodedArgs &args); // mov.l Rm, @(disp,Rn)
    TPL_TRAITS void MOVBSG(const DecodedArgs &args); // mov.b R0, @(disp,GBR)
    TPL_TRAITS void MOVWSG(const DecodedArgs &args); // mov.w R0, @(disp,GBR)
    TPL_TRAITS void MOVLSG(const DecodedArgs &args); // mov.l R0, @(disp,GBR)
    void MOVI(const DecodedArgs &args);              // mov   #imm, Rn
    TPL_TRAITS void MOVWI(const DecodedArgs &args);  // mov.w @(disp,PC), Rn
    TPL_TRAITS void MOVLI(const DecodedArgs &args);  // mov.l @(disp,PC), Rn
    void MOVA(const DecodedArgs &args);              // mova  @(disp,PC), R0
    void MOVT(const DecodedArgs &args);              // movt  Rn
    void CLRT();                                     // clrt
    void SETT();                                     // sett

    void EXTSB(const DecodedArgs &args); // exts.b Rm, Rn
    void EXTSW(const DecodedArgs &args); // exts.w Rm, Rn
    void EXTUB(const DecodedArgs &args); // extu.b Rm, Rn
    void EXTUW(const DecodedArgs &args); // extu.w Rm, Rn
    void SWAPB(const DecodedArgs &args); // swap.b Rm, Rn
    void SWAPW(const DecodedArgs &args); // swap.w Rm, Rn
    void XTRCT(const DecodedArgs &args); // xtrct  Rm, Rn

    void LDCGBR(const DecodedArgs &args);              // ldc   Rm, GBR
    void LDCSR(const DecodedArgs &args);               // ldc   Rm, SR
    void LDCVBR(const DecodedArgs &args);              // ldc   Rm, VBR
    TPL_TRAITS void LDCMGBR(const DecodedArgs &args);  // ldc.l @Rm+, GBR
    TPL_TRAITS void LDCMSR(const DecodedArgs &args);   // ldc.l @Rm+, SR
    TPL_TRAITS void LDCMVBR(const DecodedArgs &args);  // ldc.l @Rm+, VBR
    void LDSMACH(const DecodedArgs &args);             // lds   Rm, MACH
    void LDSMACL(const DecodedArgs &args);             // lds   Rm, MACL
    void LDSPR(const DecodedArgs &args);               // lds   Rm, PR
    TPL_TRAITS void LDSMMACH(const DecodedArgs &args); // lds.l @Rm+, MACH
    TPL_TRAITS void LDSMMACL(const DecodedArgs &args); // lds.l @Rm+, MACL
    TPL_TRAITS void LDSMPR(const DecodedArgs &args);   // lds.l @Rm+, PR
    void STCGBR(const DecodedArgs &args);              // stc   GBR, Rn
    void STCSR(const DecodedArgs &args);               // stc   SR, Rn
    void STCVBR(const DecodedArgs &args);              // stc   VBR, Rn
    TPL_TRAITS void STCMGBR(const DecodedArgs &args);  // stc.l GBR, @-Rn
    TPL_TRAITS void STCMSR(const DecodedArgs &args);   // stc.l SR, @-Rn
    TPL_TRAITS void STCMVBR(const DecodedArgs &args);  // stc.l VBR, @-Rn
    void STSMACH(const DecodedArgs &args);             // sts   MACH, Rn
    void STSMACL(const DecodedArgs &args);             // sts   MACL, Rn
    void STSPR(const DecodedArgs &args);               // sts   PR, Rn
    TPL_TRAITS void STSMMACH(const DecodedArgs &args); // sts.l MACH, @-Rn
    TPL_TRAITS void STSMMACL(const DecodedArgs &args); // sts.l MACL, @-Rn
    TPL_TRAITS void STSMPR(const DecodedArgs &args);   // sts.l PR, @-Rn

    void ADD(const DecodedArgs &args);             // add    Rm, Rn
    void ADDI(const DecodedArgs &args);            // add    imm, Rn
    void ADDC(const DecodedArgs &args);            // addc   Rm, Rn
    void ADDV(const DecodedArgs &args);            // addv   Rm, Rn
    void AND(const DecodedArgs &args);             // and    Rm, Rn
    void ANDI(const DecodedArgs &args);            // and    imm, R0
    TPL_TRAITS void ANDM(const DecodedArgs &args); // and.   b imm, @(R0,GBR)
    void NEG(const DecodedArgs &args);             // neg    Rm, Rn
    void NEGC(const DecodedArgs &args);            // negc   Rm, Rn
    void NOT(const DecodedArgs &args);             // not    Rm, Rn
    void OR(const DecodedArgs &args);              // or     Rm, Rn
    void ORI(const DecodedArgs &args);             // or     imm, Rn
    TPL_TRAITS void ORM(const DecodedArgs &args);  // or.b   imm, @(R0,GBR)
    void ROTCL(const DecodedArgs &args);           // rotcl  Rn
    void ROTCR(const DecodedArgs &args);           // rotcr  Rn
    void ROTL(const DecodedArgs &args);            // rotl   Rn
    void ROTR(const DecodedArgs &args);            // rotr   Rn
    void SHAL(const DecodedArgs &args);            // shal   Rn
    void SHAR(const DecodedArgs &args);            // shar   Rn
    void SHLL(const DecodedArgs &args);            // shll   Rn
    void SHLL2(const DecodedArgs &args);           // shll2  Rn
    void SHLL8(const DecodedArgs &args);           // shll8  Rn
    void SHLL16(const DecodedArgs &args);          // shll16 Rn
    void SHLR(const DecodedArgs &args);            // shlr   Rn
    void SHLR2(const DecodedArgs &args);           // shlr2  Rn
    void SHLR8(const DecodedArgs &args);           // shlr8  Rn
    void SHLR16(const DecodedArgs &args);          // shlr16 Rn
    void SUB(const DecodedArgs &args);             // sub    Rm, Rn
    void SUBC(const DecodedArgs &args);            // subc   Rm, Rn
    void SUBV(const DecodedArgs &args);            // subv   Rm, Rn
    void XOR(const DecodedArgs &args);             // xor    Rm, Rn
    void XORI(const DecodedArgs &args);            // xor    imm, Rn
    TPL_TRAITS void XORM(const DecodedArgs &args); // xor.b  imm, @(R0,GBR)

    void DT(const DecodedArgs &args); // dt Rn

    void CLRMAC();                                 // clrmac
    TPL_TRAITS void MACW(const DecodedArgs &args); // mac.w   @Rm+, @Rn+
    TPL_TRAITS void MACL(const DecodedArgs &args); // mac.l   @Rm+, @Rn+
    void MULL(const DecodedArgs &args);            // mul.l   Rm, Rn
    void MULS(const DecodedArgs &args);            // muls.w  Rm, Rn
    void MULU(const DecodedArgs &args);            // mulu.w  Rm, Rn
    void DMULS(const DecodedArgs &args);           // dmuls.l Rm, Rn
    void DMULU(const DecodedArgs &args);           // dmulu.l Rm, Rn

    void DIV0S(const DecodedArgs &args); // div0s Rm, Rn
    void DIV0U();                        // div0u
    void DIV1(const DecodedArgs &args);  // div1  Rm, Rn

    void CMPIM(const DecodedArgs &args);           // cmp/eq  imm, R0
    void CMPEQ(const DecodedArgs &args);           // cmp/eq  Rm, Rn
    void CMPGE(const DecodedArgs &args);           // cmp/ge  Rm, Rn
    void CMPGT(const DecodedArgs &args);           // cmp/gt  Rm, Rn
    void CMPHI(const DecodedArgs &args);           // cmp/hi  Rm, Rn
    void CMPHS(const DecodedArgs &args);           // cmp/hs  Rm, Rn
    void CMPPL(const DecodedArgs &args);           // cmp/pl  Rn
    void CMPPZ(const DecodedArgs &args);           // cmp/pz  Rn
    void CMPSTR(const DecodedArgs &args);          // cmp/str Rm, Rn
    TPL_TRAITS void TAS(const DecodedArgs &args);  // tas.b   @Rn
    void TST(const DecodedArgs &args);             // tst     Rm, Rn
    void TSTI(const DecodedArgs &args);            // tst     imm, R0
    TPL_TRAITS void TSTM(const DecodedArgs &args); // tst.b   imm, @(R0,GBR)

    TPL_TRAITS uint64 BF(const DecodedArgs &args);  // bf    disp
    uint64 BFS(const DecodedArgs &args);            // bf/s  disp
    TPL_TRAITS uint64 BT(const DecodedArgs &args);  // bt    disp
    uint64 BTS(const DecodedArgs &args);            // bt/s  disp
    void BRA(const DecodedArgs &args);              // bra   disp
    void BRAF(const DecodedArgs &args);             // braf  Rm
    void BSR(const DecodedArgs &args);              // bsr   disp
    void BSRF(const DecodedArgs &args);             // bsrf  Rm
    void JMP(const DecodedArgs &args);              // jmp   @Rm
    void JSR(const DecodedArgs &args);              // jsr   @Rm
    TPL_TRAITS void TRAPA(const DecodedArgs &args); // trapa imm

    TPL_TRAITS void RTE(); // rte
    void RTS();            // rts

#undef TPL_TRAITS

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const scu::CBExternalInterrupt CbExtIntr = util::MakeClassMemberRequiredCallback<&SH2::SetExternalInterrupt>(this);
};

} // namespace ymir::sh2
