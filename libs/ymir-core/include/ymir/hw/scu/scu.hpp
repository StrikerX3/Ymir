#pragma once

#include "scu_defs.hpp"
#include "scu_dma.hpp"
#include "scu_dsp.hpp"

#include <ymir/core/scheduler.hpp>

#include <ymir/state/state_scu.hpp>

#include <ymir/debug/scu_tracer_base.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <ymir/hw/scu/scu_callbacks.hpp>

#include <ymir/hw/cdblock/cdblock_internal_callbacks.hpp>
#include <ymir/hw/scsp/scsp_internal_callbacks.hpp>
#include <ymir/hw/scu/scu_internal_callbacks.hpp>
#include <ymir/hw/sh2/sh2_internal_callbacks.hpp>
#include <ymir/hw/smpc/smpc_internal_callbacks.hpp>
#include <ymir/hw/vdp/vdp_internal_callbacks.hpp>

#include <ymir/hw/cart/cart_slot.hpp>

#include <ymir/sys/backup_ram.hpp>
#include <ymir/sys/bus.hpp>

#include <ymir/util/data_ops.hpp>
#include <ymir/util/inline.hpp>

#include <iosfwd>

namespace ymir::scu {

// SCU memory map
//
// TODO? Address range            Mirror size       Description
//       0x200'0000..0x3FF'FFFF   -                 A-Bus CS0
//       0x400'0000..0x4FF'FFFF   -                 A-Bus CS1
// TODO  0x500'0000..0x57F'FFFF   -                 A-Bus Dummy
//       0x580'0000..0x58F'FFFF   -                 A-Bus CS2 (includes CD-ROM registers)
// TODO  0x590'0000..0x59F'FFFF   -                 Lockup when read
//       0x5A0'0000..0x5AF'FFFF   0x40000/0x80000   68000 Work RAM
//       0x5B0'0000..0x5BF'FFFF   0x1000            SCSP registers
//       0x5C0'0000..0x5C7'FFFF   0x80000           VDP1 VRAM
//       0x5C8'0000..0x5CF'FFFF   0x40000           VDP1 Framebuffer (backbuffer only)
//       0x5D0'0000..0x5D7'FFFF   0x18 (no mirror)  VDP1 Registers
// TODO  0x5D8'0000..0x5DF'FFFF   -                 Lockup when read
//       0x5E0'0000..0x5EF'FFFF   0x80000           VDP2 VRAM
//       0x5F0'0000..0x5F7'FFFF   0x1000            VDP2 CRAM
//       0x5F8'0000..0x5FB'FFFF   0x200             VDP2 registers
// TODO  0x5FC'0000..0x5FD'FFFF   -                 Reads 0x000E0000
//       0x5FE'0000..0x5FE'FFFF   0x100             SCU registers
// TODO  0x5FF'0000..0x5FF'FFFF   0x100             Unknown registers
//
// Notes
// - Unless otherwise specified, all regions are mirrored across the designated area
// - Addresses 0x200'0000..0x58F'FFFF comprise the SCU A-Bus
// - Addresses 0x5A0'0000..0x5FB'FFFF comprise the SCU B-Bus
// - A-Bus and B-Bus reads are always 32-bit (split into two 16-bit reads internally)
// - A-Bus and B-Bus 32-bit writes are split into two 16-bit writes internally
// - 68000 Work RAM
//   - [TODO] Area size depends on MEM4MB bit setting:
//       0=only first 256 KiB are used/mirrored
//       1=all 512 KiB are used/mirrored
// - VDP2 CRAM
//   - [TODO] Byte writes write garbage to the odd/even byte counterpart
//   - Byte reads work normally
class SCU {
public:
    SCU(core::Scheduler &scheduler, sys::SH2Bus &bus);

    void Reset(bool hard);

    void MapCallbacks(CBExternalInterrupt master, CBExternalInterrupt slave) {
        m_cbExternalMasterInterrupt = master;
        m_cbExternalSlaveInterrupt = slave;
    }

    void MapMemory(sys::SH2Bus &bus);

    template <bool debug>
    void Advance(uint64 cycles);

    void SetDebugPortWriteCallback(CBDebugPortWrite callback) {
        m_cbDebugPortWrite = callback;
    }

    // Checks if a DMA transfer is active.
    // This is used to stall the SH-2s.
    bool IsDMAActive() const {
        return m_activeDMAChannelLevel < m_dmaChannels.size() || m_dsp.dmaRun;
    }

    // -------------------------------------------------------------------------
    // Cartridge slot

    template <typename T, typename... Args>
        requires std::derived_from<T, cart::BaseCartridge>
    T *InsertCartridge(Args &&...args) {
        T *cart = m_cartSlot.InsertCartridge<T>(std::forward<Args>(args)...);
        // TODO: m_cartSlot.MapMemory(bus)
        return cart;
    }

    void RemoveCartridge() {
        // TODO: unmap cartridge memory
        m_cartSlot.RemoveCartridge();
    }

    // Returns a reference to the inserted cartridge.
    [[nodiscard]] cart::BaseCartridge &GetCartridge() {
        return m_cartSlot.GetCartridge();
    }

    // -------------------------------------------------------------------------
    // DSP

    SCUDSP &GetDSP() {
        return m_dsp;
    }

    const SCUDSP &GetDSP() const {
        return m_dsp;
    }

    // -------------------------------------------------------------------------
    // External interrupt triggers

    void UpdateHBlank(bool hb, bool vb);
    void UpdateVBlank(bool vb);
    void TriggerTimer0();
    void TriggerTimer1();
    void TriggerDSPEnd();
    void TriggerSoundRequest(bool level);
    void TriggerSystemManager();
    void TriggerPad();
    void TriggerDMAEnd(uint32 level);
    void TriggerDMAIllegal();
    void TriggerSpriteDrawEnd();

    void TriggerExternalInterrupt0();
    void AcknowledgeExternalInterrupt();

    // -------------------------------------------------------------------------
    // RAM/register dumps

    void DumpDSPProgramRAM(std::ostream &out) const;
    void DumpDSPDataRAM(std::ostream &out) const;
    void DumpDSPRegs(std::ostream &out) const;

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SCUState &state) const;
    [[nodiscard]] bool ValidateState(const state::SCUState &state) const;
    void LoadState(const state::SCUState &state);

private:
    sys::SH2Bus &m_bus;

    CBExternalInterrupt m_cbExternalMasterInterrupt;
    CBExternalInterrupt m_cbExternalSlaveInterrupt;

    CBDebugPortWrite m_cbDebugPortWrite;

    core::Scheduler &m_scheduler;
    core::EventID m_timer1Event;

    static void OnTimer1Event(core::EventContext &eventContext, void *userContext);

    // -------------------------------------------------------------------------
    // Memory accessors

    template <mem_primitive T, bool peek>
    T ReadCartridge(uint32 address);

    template <mem_primitive T, bool poke>
    void WriteCartridge(uint32 address, T value);

    // -------------------------------------------------------------------------
    // Cartridge slot

    cart::CartridgeSlot m_cartSlot;
    std::string m_debugOutput; // mednafen debug port at 0x2100001, only accepts 8-bit writes

    // -------------------------------------------------------------------------
    // Interrupts

    InterruptMask m_intrMask;
    InterruptStatus m_intrStatus;
    uint16 m_abusIntrsPendingAck;
    uint8 m_pendingIntrLevel;
    uint8 m_pendingIntrIndex;

    void UpdateMasterInterruptLevel();

    // -------------------------------------------------------------------------
    // DMA

    std::array<DMAChannel, 3> m_dmaChannels;
    uint8 m_activeDMAChannelLevel;

    void DMAReadIndirectTransfer(uint8 level);

    // Runs DMA transfers.
    // The cycle counter is used to trigger delayed interrupt signals.
    void RunDMA(uint64 cycles);

    void ForceRunImmediateDMA(uint32 index);
    void RecalcDMAChannel();

    void TriggerImmediateDMA(uint32 index);
    void TriggerDMATransfer(DMATrigger trigger);

    // -------------------------------------------------------------------------
    // DSP

    SCUDSP m_dsp;

    // -------------------------------------------------------------------------
    // Timers

    // Timer 0 counts up at every HBlank IN
    // Resets to 0 at VBlank OUT of the first line before the display area
    // Raises interrupt when counter == compare
    uint16 m_timer0Counter;
    uint16 m_timer0Compare;

    // Timer 1 reloads at HBlank IN
    // Counts down every 4 cycles (at 7 MHz) when enabled
    // Raises interrupt when counter == 0 depending on mode:
    // - false: every line
    // - true: only if Timer 0 counter matched on previous line
    uint16 m_timer1Reload; // 2 fractional bits
    bool m_timer1Mode;
    bool m_timer1Triggered;
    bool m_timerEnable; // Enables both timers

    FORCE_INLINE uint16 ReadTimer0Counter() const {
        return m_timer0Counter;
    }

    FORCE_INLINE void WriteTimer0Counter(uint16 value) {
        m_timer0Counter = bit::extract<0, 9>(value);
    }

    FORCE_INLINE uint16 ReadTimer0Compare() const {
        return m_timer0Compare;
    }

    FORCE_INLINE void WriteTimer0Compare(uint16 value) {
        m_timer0Compare = bit::extract<0, 9>(value);
    }
    FORCE_INLINE void WriteTimer0CompareHi(uint8 value) {
        bit::deposit_into<8, 9>(m_timer0Compare, value);
    }
    FORCE_INLINE void WriteTimer0CompareLo(uint8 value) {
        bit::deposit_into<0, 7>(m_timer0Compare, value);
    }

    FORCE_INLINE uint16 ReadTimer1Reload() const {
        return m_timer1Reload >> 2u;
    }

    FORCE_INLINE void WriteTimer1Reload(uint16 value) {
        m_timer1Reload = bit::extract<0, 8>(value) << 2u;
    }
    FORCE_INLINE void WriteTimer1ReloadHi(uint8 value) {
        bit::deposit_into<10>(m_timer1Reload, value);
    }
    FORCE_INLINE void WriteTimer1ReloadLo(uint8 value) {
        bit::deposit_into<2, 9>(m_timer1Reload, value);
    }

    void TickTimer1();

    // -------------------------------------------------------------------------
    // SCU registers

    bool m_WRAMSizeSelect; // false=2x2Mbit, true=2x4Mbit

    void WriteWRAMSizeSelect(bool value) {
        // TODO: adjust memory address masks
        m_WRAMSizeSelect = value;
    }

    template <mem_primitive T, bool peek>
    T ReadReg(uint32 address);

    template <bool poke>
    void WriteRegByte(uint32 address, uint8 value);
    template <bool poke>
    void WriteRegWord(uint32 address, uint16 value);
    template <bool poke>
    void WriteRegLong(uint32 address, uint32 value);

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const cdblock::CBTriggerExternalInterrupt0 CbTriggerExtIntr0 =
        util::MakeClassMemberRequiredCallback<&SCU::TriggerExternalInterrupt0>(this);

    const sh2::CBAcknowledgeExternalInterrupt CbAckExtIntr =
        util::MakeClassMemberRequiredCallback<&SCU::AcknowledgeExternalInterrupt>(this);

    const vdp::CBHBlankStateChange CbHBlankStateChange =
        util::MakeClassMemberRequiredCallback<&SCU::UpdateHBlank>(this);
    const vdp::CBVBlankStateChange CbVBlankStateChange =
        util::MakeClassMemberRequiredCallback<&SCU::UpdateVBlank>(this);
    const vdp::CBTriggerEvent CbTriggerSpriteDrawEnd =
        util::MakeClassMemberRequiredCallback<&SCU::TriggerSpriteDrawEnd>(this);

    const scsp::CBTriggerSoundRequestInterrupt CbTriggerSoundRequest =
        util::MakeClassMemberRequiredCallback<&SCU::TriggerSoundRequest>(this);

    const smpc::CBInterruptCallback CbTriggerSystemManager =
        util::MakeClassMemberRequiredCallback<&SCU::TriggerSystemManager>(this);
    const smpc::CBInterruptCallback CbTriggerPad = util::MakeClassMemberRequiredCallback<&SCU::TriggerPad>(this);

    const CBTriggerDSPEnd CbTriggerDSPEnd = util::MakeClassMemberRequiredCallback<&SCU::TriggerDSPEnd>(this);

    // -------------------------------------------------------------------------
    // Debugger

    // Attaches the specified tracer to this component.
    // Pass nullptr to disable tracing.
    void UseTracer(debug::ISCUTracer *tracer) {
        m_tracer = tracer;
        m_dsp.UseTracer(m_tracer);
    }

    class Probe {
    public:
        Probe(SCU &scu);

        // ---------------------------------------------------------------------
        // Registers

        bool GetWRAMSizeSelect() const;
        void SetWRAMSizeSelect(bool value) const;

        // ---------------------------------------------------------------------
        // Interrupts

        InterruptMask &GetInterruptMask();
        InterruptStatus &GetInterruptStatus();
        uint16 &GetABusInterruptsPendingAcknowledge();

        const InterruptMask &GetInterruptMask() const;
        const InterruptStatus &GetInterruptStatus() const;
        const uint16 &GetABusInterruptsPendingAcknowledge() const;

        uint8 GetPendingInterruptLevel() const;
        uint8 GetPendingInterruptIndex() const;

        // ---------------------------------------------------------------------
        // Timers

        uint16 GetTimer0Counter() const;
        void SetTimer0Counter(uint16 value);

        uint16 GetTimer0Compare() const;
        void SetTimer0Compare(uint16 value) const;

        uint16 GetTimer1Reload() const;
        void SetTimer1Reload(uint16 value);

        bool IsTimerEnabled() const;
        void SetTimerEnabled(bool enabled);

        bool GetTimer1Mode() const;
        void SetTimer1Mode(bool mode);

        // ---------------------------------------------------------------------
        // DMA registers

        uint32 GetDMASourceAddress(uint8 channel) const;
        void SetDMASourceAddress(uint8 channel, uint32 value);

        uint32 GetDMADestinationAddress(uint8 channel) const;
        void SetDMADestinationAddress(uint8 channel, uint32 value);

        uint32 GetDMATransferCount(uint8 channel) const;
        void SetDMATransferCount(uint8 channel, uint32 value);

        uint32 GetDMASourceAddressIncrement(uint8 channel) const;
        void SetDMASourceAddressIncrement(uint8 channel, uint32 value);

        uint32 GetDMADestinationAddressIncrement(uint8 channel) const;
        void SetDMADestinationAddressIncrement(uint8 channel, uint32 value);

        bool IsDMAUpdateSourceAddress(uint8 channel) const;
        void SetDMAUpdateSourceAddress(uint8 channel, bool value) const;

        bool IsDMAUpdateDestinationAddress(uint8 channel) const;
        void SetDMAUpdateDestinationAddress(uint8 channel, bool value) const;

        bool IsDMAEnabled(uint8 channel) const;
        void SetDMAEnabled(uint8 channel, bool value) const;

        bool IsDMAIndirect(uint8 channel) const;
        void SetDMAIndirect(uint8 channel, bool value) const;

        DMATrigger GetDMATrigger(uint8 channel) const;
        void SetDMATrigger(uint8 channel, DMATrigger trigger);

        // ---------------------------------------------------------------------
        // DMA state

        bool IsDMATransferActive(uint8 channel) const;
        uint32 GetCurrentDMASourceAddress(uint8 channel) const;
        uint32 GetCurrentDMADestinationAddress(uint8 channel) const;
        uint32 GetCurrentDMATransferCount(uint8 channel) const;
        uint32 GetCurrentDMASourceAddressIncrement(uint8 channel) const;
        uint32 GetCurrentDMADestinationAddressIncrement(uint8 channel) const;
        uint32 GetCurrentDMAIndirectSourceAddress(uint8 channel) const;

    private:
        SCU &m_scu;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    Probe m_probe{*this};
    debug::ISCUTracer *m_tracer = nullptr;
};

} // namespace ymir::scu
