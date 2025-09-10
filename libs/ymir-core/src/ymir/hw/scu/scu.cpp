#include <ymir/hw/scu/scu.hpp>

#include <ymir/hw/cart/cart_impl_dram.hpp>
#include <ymir/hw/cart/cart_impl_rom.hpp>

#include "scu_devlog.hpp"

#include <ymir/util/inline.hpp>
#include <ymir/util/size_ops.hpp>

#include <bit>

namespace ymir::scu {

// -----------------------------------------------------------------------------
// Debugger

FORCE_INLINE static void TraceRaiseInterrupt(debug::ISCUTracer *tracer, uint8 index, uint8 level) {
    if (tracer) {
        return tracer->RaiseInterrupt(index, level);
    }
}

FORCE_INLINE static void TraceAcknowledgeInterrupt(debug::ISCUTracer *tracer, uint8 index) {
    if (tracer) {
        return tracer->AcknowledgeInterrupt(index);
    }
}

FORCE_INLINE static void TraceDMA(debug::ISCUTracer *tracer, uint8 channel, uint32 srcAddr, uint32 dstAddr,
                                  uint32 xferCount, uint32 srcAddrInc, uint32 dstAddrInc, bool indirect,
                                  uint32 indirectAddr) {
    if (tracer) {
        return tracer->DMA(channel, srcAddr, dstAddr, xferCount, srcAddrInc, dstAddrInc, indirect, indirectAddr);
    }
}

// -----------------------------------------------------------------------------
// Implementation

SCU::SCU(core::Scheduler &scheduler, sys::Bus &bus)
    : m_bus(bus)
    , m_scheduler(scheduler)
    , m_dsp(bus) {

    m_dsp.SetTriggerDSPEndCallback(CbTriggerDSPEnd);

    RemoveCartridge();

    m_timer1Event = m_scheduler.RegisterEvent(core::events::SCUTimer1, this, OnTimer1Event);

    m_intrStatus.u32 = 0;

    Reset(true);
}

void SCU::Reset(bool hard) {
    m_cartSlot.Reset(hard);

    m_intrMask.u32 = 0xBFFF;
    m_abusIntrsPendingAck = 0x0000;
    m_pendingIntrLevel = 0;
    m_pendingIntrIndex = 0;

    if (hard) {
        for (auto &ch : m_dmaChannels) {
            ch.Reset();
        }
        m_activeDMAChannelLevel = m_dmaChannels.size();
    }

    m_dsp.Reset(hard);

    if (hard) {
        m_timer0Counter = 0;
        m_timer0Compare = 0;

        m_scheduler.Cancel(m_timer1Event);
        m_timer1Reload = 0;
        m_timer1Mode = false;
        m_timer1Triggered = true;
        m_timerEnable = false;
    }

    m_WRAMSizeSelect = false;
}

void SCU::MapMemory(sys::Bus &bus) {
    static constexpr auto cast = [](void *ctx) -> SCU & { return *static_cast<SCU *>(ctx); };

    // A-Bus CS0 and CS1 - Cartridge
    bus.MapNormal(
        0x200'0000, 0x4FF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).ReadCartridge<uint8, false>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).ReadCartridge<uint16, false>(address); },
        [](uint32 address, void *ctx) -> uint32 { return cast(ctx).ReadCartridge<uint32, false>(address); },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).WriteCartridge<uint8, false>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).WriteCartridge<uint16, false>(address, value); },
        [](uint32 address, uint32 value, void *ctx) { cast(ctx).WriteCartridge<uint32, false>(address, value); });

    bus.MapSideEffectFree(
        0x200'0000, 0x4FF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).ReadCartridge<uint8, true>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).ReadCartridge<uint16, true>(address); },
        [](uint32 address, void *ctx) -> uint32 { return cast(ctx).ReadCartridge<uint32, true>(address); },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).WriteCartridge<uint8, true>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).WriteCartridge<uint16, true>(address, value); },
        [](uint32 address, uint32 value, void *ctx) { cast(ctx).WriteCartridge<uint32, true>(address, value); });

    // A-Bus CS2 - 0x580'0000..0x58F'FFFF
    // CD block maps itself here

    // B-Bus - 0x5A0'0000..0x5FB'FFFF
    // VDP and SCSP map themselves here

    // TODO: 0x5FC'0000..0x5FD'FFFF - reads 0x000E0000

    // SCU registers
    bus.MapNormal(
        0x5FE'0000, 0x5FE'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).ReadReg<uint8, false>(address & 0xFF); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).ReadReg<uint16, false>(address & 0xFF); },
        [](uint32 address, void *ctx) -> uint32 { return cast(ctx).ReadReg<uint32, false>(address & 0xFF); },

        [](uint32 address, uint8 value, void *ctx) { cast(ctx).WriteRegByte<false>(address & 0xFF, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).WriteRegWord<false>(address & 0xFF, value); },
        [](uint32 address, uint32 value, void *ctx) { cast(ctx).WriteRegLong<false>(address & 0xFF, value); });

    bus.MapSideEffectFree(
        0x5FE'0000, 0x5FE'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).ReadReg<uint8, true>(address & 0xFF); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).ReadReg<uint16, true>(address & 0xFF); },
        [](uint32 address, void *ctx) -> uint32 { return cast(ctx).ReadReg<uint32, true>(address & 0xFF); },

        [](uint32 address, uint8 value, void *ctx) { cast(ctx).WriteRegByte<true>(address & 0xFF, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).WriteRegWord<true>(address & 0xFF, value); },
        [](uint32 address, uint32 value, void *ctx) { cast(ctx).WriteRegLong<true>(address & 0xFF, value); });

    // TODO: 0x5FF'0000..0x5FF'FFFF - Unknown registers
}

template <bool debug>
void SCU::Advance(uint64 cycles) {
    // RunDMA(cycles);

    m_dsp.Run<debug>(cycles);
}

template void SCU::Advance<false>(uint64 cycles);
template void SCU::Advance<true>(uint64 cycles);

void SCU::UpdateHBlank(bool hb, bool vb) {
    if (hb) {
        m_intrStatus.VDP2_HBlankIN = 1;
        UpdateMasterInterruptLevel();
        if (m_timerEnable) {
            if (m_timer0Counter == m_timer0Compare) {
                TriggerTimer0();
            }
            if (m_timer1Triggered && (!m_timer1Mode || m_timer0Counter == m_timer0Compare)) {
                m_timer1Triggered = false;
                m_scheduler.ScheduleFromNow(m_timer1Event, m_timer1Reload);
            }
            ++m_timer0Counter;
        }
        TriggerDMATransfer(DMATrigger::HBlankIN);
    }
    if (!vb) {
        if (hb) {
            m_cbExternalSlaveInterrupt(2, 0x41);
        } else {
            m_cbExternalSlaveInterrupt(0, 0x00);
        }
    }
}

void SCU::UpdateVBlank(bool vb) {
    if (vb) {
        m_intrStatus.VDP2_VBlankIN = 1;
        TriggerDMATransfer(DMATrigger::VBlankIN);
        m_cbExternalSlaveInterrupt(6, 0x43);
    } else {
        m_intrStatus.VDP2_VBlankOUT = 1;
        m_timer0Counter = 0;
        TriggerDMATransfer(DMATrigger::VBlankOUT);
        m_cbExternalSlaveInterrupt(4, 0x42); // TODO: make this behavior optional
    }
    UpdateMasterInterruptLevel();
}

void SCU::TriggerTimer0() {
    if (m_intrStatus.SCU_Timer0 != 1) {
        m_intrStatus.SCU_Timer0 = 1;
        UpdateMasterInterruptLevel();
        TriggerDMATransfer(DMATrigger::Timer0);
    }
}

void SCU::TriggerTimer1() {
    if (m_intrStatus.SCU_Timer1 != 1) {
        m_intrStatus.SCU_Timer1 = 1;
        UpdateMasterInterruptLevel();
        TriggerDMATransfer(DMATrigger::Timer1);
    }
}

void SCU::TriggerDSPEnd() {
    if (m_intrStatus.SCU_DSPEnd != 1) {
        m_intrStatus.SCU_DSPEnd = 1;
        UpdateMasterInterruptLevel();
    }
}

void SCU::TriggerSoundRequest(bool level) {
    if (m_intrStatus.SCSP_SoundRequest != level) {
        m_intrStatus.SCSP_SoundRequest = level;
        UpdateMasterInterruptLevel();
        if (level) {
            TriggerDMATransfer(DMATrigger::SoundRequest);
        }
    }
}

void SCU::TriggerSystemManager() {
    if (m_intrStatus.SMPC_SystemManager != 1) {
        m_intrStatus.SMPC_SystemManager = 1;
        UpdateMasterInterruptLevel();
    }
}

void SCU::TriggerPad() {
    if (m_intrStatus.SMPC_Pad != 1) {
        m_intrStatus.SMPC_Pad = 1;
        UpdateMasterInterruptLevel();
    }
}

void SCU::TriggerDMAEnd(uint32 level) {
    assert(level < 3);
    switch (level) {
    case 0:
        if (m_intrStatus.SCU_Level0DMAEnd == 1) {
            return;
        }
        m_intrStatus.SCU_Level0DMAEnd = 1;
        break;
    case 1:
        if (m_intrStatus.SCU_Level1DMAEnd == 1) {
            return;
        }
        m_intrStatus.SCU_Level1DMAEnd = 1;
        break;
    case 2:
        if (m_intrStatus.SCU_Level2DMAEnd == 1) {
            return;
        }
        m_intrStatus.SCU_Level2DMAEnd = 1;
        break;
    }
    UpdateMasterInterruptLevel();
}

void SCU::TriggerDMAIllegal() {
    if (m_intrStatus.SCU_DMAIllegal != 1) {
        m_intrStatus.SCU_DMAIllegal = 1;
        UpdateMasterInterruptLevel();
    }
}

void SCU::TriggerSpriteDrawEnd() {
    if (m_intrStatus.VDP1_SpriteDrawEnd != 1) {
        m_intrStatus.VDP1_SpriteDrawEnd = 1;
        UpdateMasterInterruptLevel();
        TriggerDMATransfer(DMATrigger::SpriteDrawEnd);
    }
}

void SCU::TriggerExternalInterrupt0() {
    if (m_intrStatus.ABus_ExtIntr0 != 1 && !bit::test<0>(m_abusIntrsPendingAck)) {
        m_intrStatus.ABus_ExtIntr0 = 1;
        UpdateMasterInterruptLevel();
    }
}

void SCU::AcknowledgeExternalInterrupt() {
    if (m_pendingIntrLevel > 0) {
        devlog::trace<grp::intr>("Acknowledging {} interrupt {:X}",
                                 (m_pendingIntrIndex <= 15 ? "internal" : "external"), m_pendingIntrIndex);
        TraceAcknowledgeInterrupt(m_tracer, m_pendingIntrIndex);

        m_pendingIntrLevel = 0;

        m_intrMask.u32 = 0xBFFF;
    }

    m_cbExternalMasterInterrupt(0, 0x00);
}

void SCU::DumpDSPProgramRAM(std::ostream &out) const {
    for (uint32 i = 0; i < m_dsp.programRAM.size(); ++i) {
        const uint32 value = bit::big_endian_swap(m_dsp.programRAM[i].u32);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCU::DumpDSPDataRAM(std::ostream &out) const {
    for (uint32 i = 0; i < m_dsp.dataRAM.size(); ++i) {
        for (uint32 j = 0; j < m_dsp.dataRAM[i].size(); ++j) {
            const uint32 value = bit::big_endian_swap(m_dsp.dataRAM[i][j]);
            out.write((const char *)&value, sizeof(value));
        }
    }
}

void SCU::DumpDSPRegs(std::ostream &out) const {
    auto write = [&](const auto &reg) { out.write((const char *)&reg, sizeof(reg)); };
    write(m_dsp.programExecuting);
    write(m_dsp.programPaused);
    write(m_dsp.programEnded);
    write(m_dsp.programStep);
    write(m_dsp.PC);
    write(m_dsp.nextInstr.u32);
    write(m_dsp.dataAddress);
    write(m_dsp.sign);
    write(m_dsp.zero);
    write(m_dsp.carry);
    write(m_dsp.overflow);
    write(m_dsp.CT);
    write(m_dsp.ALU);
    write(m_dsp.AC);
    write(m_dsp.P);
    write(m_dsp.RX);
    write(m_dsp.RY);
    write(m_dsp.loopTop);
    write(m_dsp.loopCount);
    write(m_dsp.looping);
    write(m_dsp.dmaRun);
    write(m_dsp.dmaToD0);
    write(m_dsp.dmaHold);
    write(m_dsp.dmaCount);
    write(m_dsp.dmaSrc);
    write(m_dsp.dmaDst);
    write(m_dsp.dmaReadAddr);
    write(m_dsp.dmaWriteAddr);
    write(m_dsp.dmaAddrInc);
}

void SCU::SaveState(state::SCUState &state) const {
    for (size_t i = 0; i < 3; i++) {
        m_dmaChannels[i].SaveState(state.dma[i]);
    }
    m_dsp.SaveState(state.dsp);

    state.cartData.clear();
    switch (m_cartSlot.GetCartridgeType()) {
    case cart::CartType::None: state.cartType = state::SCUState::CartType::None; break;
    case cart::CartType::BackupMemory: state.cartType = state::SCUState::CartType::BackupMemory; break;
    case cart::CartType::DRAM8Mbit: //
    {
        const cart::DRAM8MbitCartridge *cart = m_cartSlot.GetCartridge().As<cart::CartType::DRAM8Mbit>();
        assert(cart != nullptr);
        state.cartType = state::SCUState::CartType::DRAM8Mbit;
        state.cartData.resize(1_MiB);
        cart->DumpRAM(std::span<uint8, 1_MiB>(state.cartData.begin(), 1_MiB));
        break;
    }
    case cart::CartType::DRAM32Mbit: //
    {
        const cart::DRAM32MbitCartridge *cart = m_cartSlot.GetCartridge().As<cart::CartType::DRAM32Mbit>();
        assert(cart != nullptr);
        state.cartType = state::SCUState::CartType::DRAM32Mbit;
        state.cartData.resize(4_MiB);
        cart->DumpRAM(std::span<uint8, 4_MiB>(state.cartData.begin(), 4_MiB));
        break;
    }
    case cart::CartType::DRAM48Mbit: //
    {
        const cart::DRAM48MbitCartridge *cart = m_cartSlot.GetCartridge().As<cart::CartType::DRAM48Mbit>();
        assert(cart != nullptr);
        state.cartType = state::SCUState::CartType::DRAM48Mbit;
        state.cartData.resize(6_MiB);
        cart->DumpRAM(std::span<uint8, 6_MiB>(state.cartData.begin(), 6_MiB));
        break;
    }
    case cart::CartType::ROM: //
    {
        const cart::ROMCartridge *cart = m_cartSlot.GetCartridge().As<cart::CartType::ROM>();
        assert(cart != nullptr);
        static constexpr size_t size = cart::kROMCartSize;
        state.cartType = state::SCUState::CartType::ROM;
        state.cartData.resize(size);
        cart->DumpROM(std::span<uint8, size>(state.cartData.begin(), size));
        break;
    }
    }

    state.intrMask = m_intrMask.u32;
    state.intrStatus = m_intrStatus.u32;
    state.abusIntrsPendingAck = m_abusIntrsPendingAck;
    state.pendingIntrLevel = m_pendingIntrLevel;
    state.pendingIntrIndex = m_pendingIntrIndex;

    state.timer0Compare = m_timer0Compare;
    state.timer0Counter = m_timer0Counter;
    state.timer1Reload = m_timer1Reload;
    state.timer1Mode = m_timer1Mode;
    state.timer1Triggered = m_timer1Triggered;
    state.timerEnable = m_timerEnable;

    state.wramSizeSelect = m_WRAMSizeSelect;
}

bool SCU::ValidateState(const state::SCUState &state) const {
    for (size_t i = 0; i < 3; i++) {
        if (!m_dmaChannels[i].ValidateState(state.dma[i])) {
            return false;
        }
    }
    if (!m_dsp.ValidateState(state.dsp)) {
        return false;
    }

    switch (state.cartType) {
    case state::SCUState::CartType::DRAM8Mbit:
        if (state.cartData.size() != 1_MiB) {
            return false;
        }
        break;
    case state::SCUState::CartType::DRAM32Mbit:
        if (state.cartData.size() != 4_MiB) {
            return false;
        }
        break;
    default: break;
    }

    return true;
}

void SCU::LoadState(const state::SCUState &state) {
    for (size_t i = 0; i < 3; i++) {
        m_dmaChannels[i].LoadState(state.dma[i]);
    }
    m_dsp.LoadState(state.dsp);

    switch (state.cartType) {
    case state::SCUState::CartType::DRAM8Mbit: //
    {
        auto *cart = m_cartSlot.InsertCartridge<cart::DRAM8MbitCartridge>();
        cart->LoadRAM(std::span<const uint8, 1_MiB>(state.cartData.begin(), 1_MiB));
        break;
    }
    case state::SCUState::CartType::DRAM32Mbit: //
    {
        auto *cart = m_cartSlot.InsertCartridge<cart::DRAM32MbitCartridge>();
        cart->LoadRAM(std::span<const uint8, 4_MiB>(state.cartData.begin(), 4_MiB));
        break;
    }
    case state::SCUState::CartType::DRAM48Mbit: //
    {
        auto *cart = m_cartSlot.InsertCartridge<cart::DRAM48MbitCartridge>();
        cart->LoadRAM(std::span<const uint8, 6_MiB>(state.cartData.begin(), 6_MiB));
        break;
    }
    case state::SCUState::CartType::ROM: //
    {
        auto *cart = m_cartSlot.InsertCartridge<cart::ROMCartridge>();
        cart->LoadROM(std::span<const uint8, 4_MiB>(state.cartData.begin(), 4_MiB));
        break;
    }
    default: break;
    }

    m_intrMask.u32 = state.intrMask & 0x0000BFFF;
    m_intrStatus.u32 = state.intrStatus & 0xFFFFBFFF;
    m_abusIntrsPendingAck = state.abusIntrsPendingAck;
    m_pendingIntrLevel = state.pendingIntrLevel & 0xF;
    m_pendingIntrIndex = state.pendingIntrIndex & 0x1F;

    m_timer0Compare = state.timer0Compare;
    m_timer0Counter = state.timer0Counter;
    m_timer1Reload = state.timer1Reload;
    m_timer1Triggered = state.timer1Triggered;
    m_timer1Mode = state.timer1Mode;
    m_timerEnable = state.timerEnable;

    m_WRAMSizeSelect = state.wramSizeSelect;

    RecalcDMAChannel();
}

void SCU::OnTimer1Event(core::EventContext &eventContext, void *userContext) {
    auto &scu = *static_cast<SCU *>(userContext);
    scu.TickTimer1();
}

template <mem_primitive T, bool peek>
T SCU::ReadCartridge(uint32 address) {
    if constexpr (std::is_same_v<T, uint32>) {
        // 32-bit reads are split into two 16-bit reads
        uint32 value = ReadCartridge<uint16, peek>(address + 0) << 16u;
        value |= ReadCartridge<uint16, peek>(address + 2) << 0u;
        return value;
    } else if constexpr (std::is_same_v<T, uint16>) {
        if (address >= 0x4FF'FFFE) [[unlikely]] {
            // Return cartridge ID
            return 0xFF00 | m_cartSlot.GetID();
        } else {
            return m_cartSlot.ReadWord<peek>(address);
        }
    } else if constexpr (std::is_same_v<T, uint8>) {
        if (address >= 0x4FF'FFFE) [[unlikely]] {
            // Return cartridge ID
            if ((address & 1) == 0) {
                return 0xFF;
            } else {
                return m_cartSlot.GetID();
            }
        } else {
            return m_cartSlot.ReadByte<peek>(address);
        }
    }
    util::unreachable();
}

template <mem_primitive T, bool poke>
void SCU::WriteCartridge(uint32 address, T value) {
    if constexpr (std::is_same_v<T, uint32>) {
        // 32-bit writes are split into two 16-bit writes
        WriteCartridge<uint16, poke>(address + 0, value >> 16u);
        WriteCartridge<uint16, poke>(address + 2, value >> 0u);
    } else if constexpr (std::is_same_v<T, uint16>) {
        m_cartSlot.WriteWord<poke>(address, value);
    } else if constexpr (std::is_same_v<T, uint8>) {
        if (address == 0x210'0001) [[unlikely]] {
            // mednafen debug port
            m_cbDebugPortWrite(value);
            if constexpr (devlog::debug_enabled<grp::debug>) {
                if (value == '\n') {
                    devlog::debug<grp::debug>("{}", m_debugOutput);
                    m_debugOutput.clear();
                } else if (value != '\r') {
                    m_debugOutput.push_back(value);
                }
            }
        } else {
            m_cartSlot.WriteByte<poke>(address, value);
        }
    }
}

FORCE_INLINE void SCU::UpdateMasterInterruptLevel() {
    if (m_pendingIntrLevel > 0) {
        return;
    }

    const uint16 internalBits = m_intrStatus.internal & ~m_intrMask.internal;
    const uint16 externalBits = m_intrMask.ABus_ExtIntrs ? m_intrStatus.external : 0u;
    if (internalBits == 0 && externalBits == 0) {
        return;
    }

    const uint16 internalIndex = std::countr_zero(internalBits);
    const uint16 externalIndex = std::countr_zero(externalBits);

    static constexpr uint32 kInternalLevels[] = {0xF, 0xE, 0xD, 0xC, 0xB, 0xA, 0x9, 0x8, //
                                                 0x8, 0x6, 0x6, 0x5, 0x3, 0x2, 0x0, 0x0, //
                                                 0x0};
    static constexpr uint32 kExternalLevels[] = {0x7, 0x7, 0x7, 0x7, 0x4, 0x4, 0x4, 0x4, //
                                                 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, 0x1, //
                                                 0x0};
    const uint8 internalLevel = kInternalLevels[internalIndex];
    const uint8 externalLevel = kExternalLevels[externalIndex];
    devlog::trace<grp::intr>("Intr states:  {:04X} {:04X}", m_intrStatus.internal, m_intrStatus.external);
    devlog::trace<grp::intr>("Intr masks:   {:04X} {:04X} {:04X}", (uint16)m_intrMask.internal,
                             m_intrMask.ABus_ExtIntrs * 0xFFFF, m_abusIntrsPendingAck);
    devlog::trace<grp::intr>("Intr bits:    {:04X} {:04X}", internalBits, externalBits);
    devlog::trace<grp::intr>("Intr indices: {:X} {:X}", internalIndex, externalIndex);
    devlog::trace<grp::intr>("Intr levels:  {:X} {:X}", internalLevel, externalLevel);

    if (internalLevel >= externalLevel) {
        devlog::trace<grp::intr>("Raising internal interrupt {:X}, level {:X}", internalIndex, internalLevel);
        TraceRaiseInterrupt(m_tracer, internalIndex, internalLevel);

        m_pendingIntrLevel = internalLevel;
        m_pendingIntrIndex = internalIndex;
        m_intrStatus.internal &= ~(1u << internalIndex);

        m_cbExternalMasterInterrupt(internalLevel, internalIndex + 0x40);
    } else if (~m_abusIntrsPendingAck & (1u << externalIndex)) {
        devlog::trace<grp::intr>("Raising external interrupt {:X}, level {:X}", externalIndex, externalLevel);
        TraceRaiseInterrupt(m_tracer, externalIndex + 16, externalLevel);

        m_pendingIntrLevel = externalLevel;
        m_pendingIntrIndex = externalIndex + 16;
        m_intrStatus.external &= ~(1u << externalIndex);

        m_abusIntrsPendingAck |= 1u << externalIndex;
        m_cbExternalMasterInterrupt(externalLevel, externalIndex + 0x50);
    }
}

void SCU::DMAReadIndirectTransfer(uint8 level) {
    auto &ch = m_dmaChannels[level];
    const uint32 baseIndirectSrc = ch.currIndirectSrc;
    uint32 currIndirectSrc = baseIndirectSrc;
    auto read = [&] {
        const uint32 value = m_bus.Read<uint32>(currIndirectSrc);
        currIndirectSrc += ch.srcAddrInc;
        return value;
    };

    ch.currXferCount = ((read() - 1u) & 0xFFFFF) + 1u;
    ch.currDstAddr = read();
    ch.currSrcAddr = read();
    ch.currIndirectSrc = currIndirectSrc;
    ch.endIndirect = bit::test<31>(ch.currSrcAddr);
    ch.currSrcAddr &= 0x7FF'FFFF;
    ch.currDstAddr &= 0x7FF'FFFF;
    ch.currSrcAddrInc = ch.srcAddrInc;
    ch.currDstAddrInc = ch.dstAddrInc;

    devlog::trace<grp::dma_start>(
        "SCU DMA{}: Starting indirect transfer at {:08X} - {:06X} bytes from {:08X} (+{:02X}) to {:08X} (+{:02X}){}",
        level, baseIndirectSrc, ch.currXferCount, ch.currSrcAddr, ch.currSrcAddrInc, ch.currDstAddr, ch.currDstAddrInc,
        (ch.endIndirect ? " (final)" : ""));

    if (ch.currSrcAddr & 1) {
        devlog::debug<grp::dma>("SCU DMA{}: Unaligned indirect transfer read from {:08X}", level, ch.currSrcAddr);
    }
    if (ch.currDstAddr & 1) {
        devlog::debug<grp::dma>("SCU DMA{}: Unaligned indirect transfer write to {:08X}", level, ch.currDstAddr);
    }

    TraceDMA(m_tracer, level, ch.currSrcAddr, ch.currDstAddr, ch.currXferCount, ch.currSrcAddrInc, ch.currDstAddrInc,
             true, baseIndirectSrc);
}

void SCU::RunDMA() {
    // TODO: proper cycle counting
    // HACK: run *all* DMA transfers
    while (m_activeDMAChannelLevel < m_dmaChannels.size()) {
        const uint8 level = m_activeDMAChannelLevel;

        auto &ch = m_dmaChannels[level];

        assert(ch.active);

        // NOTE: These assignments as well as the first buffer read should be done at the start of every transfer.
        // This only works as is because we're running full transfers at once.
        // If at some point this code is refactored to run individual transfers, these values must be moved to the
        // DMAChannel struct and be set at the start of every transfer.
        //
        // These values are only needed for B-Bus writes because they're completely illogical.
        uint32 buf = 0x00000000;
        uint32 bufPos = ch.currSrcAddr & 3u;
        uint32 currDstAddr = ch.currDstAddr;
        uint32 currDstOffset = ch.currDstAddr & 3u;
        uint32 initialDstAlignment = currDstOffset;
        uint32 xferLength = ch.currXferCount;

        const uint32 baseSrcAddr = ch.currSrcAddr;
        const uint32 baseDstAddr = ch.currDstAddr;

        buf = m_bus.Read<uint32>(ch.currSrcAddr & ~3u);
        devlog::trace<grp::dma>("SCU DMA{}: Read from {:08X} -> {:08X}", level, ch.currSrcAddr & ~3u, buf);

        auto doRead = [&](uint32 size) -> uint32 {
            bufPos += size;
            if (bufPos <= 4) {
                return buf >> ((~(bufPos - 1) & 3u) * 8u);
            }

            const uint32 prevBuf = buf;
            bufPos -= 4;
            ch.currSrcAddr += ch.currSrcAddrInc;
            ch.currSrcAddr &= 0x7FF'FFFF;

            buf = m_bus.Read<uint32>(ch.currSrcAddr & ~3u);
            devlog::trace<grp::dma>("SCU DMA{}: Read from {:08X} -> {:08X}", level, ch.currSrcAddr & ~3u, buf);

            uint32 value = buf >> ((~(bufPos - 1) & 3u) * 8u);
            if (bufPos < 4) {
                value |= prevBuf << (bufPos * 8u);
            }
            return value;
        };

        auto read8 = [&]() -> uint8 { return doRead(1); };
        auto read16 = [&]() -> uint16 { return doRead(2); };
        auto read32 = [&]() -> uint32 { return doRead(4); };

        const BusID srcBus = GetBusID(ch.currSrcAddr);
        const BusID dstBus = GetBusID(ch.currDstAddr);

        // Are you ready for the shitshow that is DMA transfers on the SCU? Let's begin!

        // Let's get the bad cases out of the way first
        if (srcBus == dstBus || srcBus == BusID::None || dstBus == BusID::None) [[unlikely]] {
            if (srcBus == dstBus) {
                devlog::trace<grp::dma>("SCU DMA{}: Invalid same-bus transfer; ignored", level);
            } else if (srcBus == BusID::None) {
                devlog::trace<grp::dma>("SCU DMA{}: Invalid source bus; transfer ignored", level);
            } else if (dstBus == BusID::None) {
                devlog::trace<grp::dma>("SCU DMA{}: Invalid destination bus; transfer ignored", level);
            }
            if (ch.indirect && !ch.endIndirect) {
                DMAReadIndirectTransfer(level);
            } else {
                ch.active = false;
                TriggerDMAIllegal();
                RecalcDMAChannel();
            }
            continue;
        }

        // Increment the write address
        auto incDst = [&] {
            if (currDstOffset >= 4u) {
                currDstOffset -= 4u;
                currDstAddr += ch.currDstAddrInc;
                currDstAddr &= 0x7FF'FFFF;
            }
        };

        // Now, let's handle the nicest cases first
        if (dstBus != BusID::BBus) {
            // Nicely-behaved straightforward writes to A-Bus and WRAM.
            // At least *something* in this chip works as intended!

            currDstAddr &= ~3u;

            // 8-bit -> 16/32-bit realignment
            if (ch.currXferCount >= 1 && (currDstOffset & 1)) {
                const uint32 addr = currDstAddr + currDstOffset;
                const uint8 value = read8();
                m_bus.Write<uint8>(addr, value);
                currDstOffset += 1;
                ch.currXferCount -= 1;
                devlog::trace<grp::dma>("SCU DMA{}: 8-bit write to {:08X} -> {:02X}", level, addr, value);
            }

            // 16-bit -> 32-bit realignment
            if (ch.currXferCount >= 2 && (currDstOffset & 2)) {
                incDst();
                const uint32 addr = (currDstAddr + currDstOffset) & ~1u;
                const uint16 value = read16();
                m_bus.Write<uint16>(addr, value);
                currDstOffset += 2;
                ch.currXferCount -= 2;
                devlog::trace<grp::dma>("SCU DMA{}: 16-bit write to {:08X} -> {:04X}", level, addr, value);
            }

            // 32-bit transfers -- the bulk of the DMA operation
            while (ch.currXferCount >= 4) {
                incDst();
                const uint32 addr = (currDstAddr + currDstOffset) & ~3u;
                const uint32 value = read32();
                m_bus.Write<uint32>(addr, value);
                currDstOffset += 4;
                ch.currXferCount -= 4;
                devlog::trace<grp::dma>("SCU DMA{}: 32-bit write to {:08X} -> {:08X}", level, addr, value);
            }

            // Final 16-bit transfer
            if (ch.currXferCount & 2) {
                incDst();
                const uint32 addr = (currDstAddr + currDstOffset) & ~1u;
                const uint16 value = read16();
                m_bus.Write<uint16>(addr, value);
                currDstOffset += 2;
                ch.currXferCount -= 2;
                devlog::trace<grp::dma>("SCU DMA{}: 16-bit write to {:08X} -> {:04X}", level, addr, value);
            }

            // Final 8-bit transfer
            if (ch.currXferCount & 1) {
                incDst();
                const uint32 addr = currDstAddr + currDstOffset;
                const uint8 value = read8();
                m_bus.Write<uint8>(addr, value);
                currDstOffset += 1;
                ch.currXferCount -= 1;
                devlog::trace<grp::dma>("SCU DMA{}: 8-bit write to {:08X} -> {:02X}", level, addr, value);
            }

            // Now that we've dealt with this perfectly logical implementation, let's take a look at the nonsensical
            // mess that is B-Bus writes.
        } else {
            // B-Bus writes are incredibly buggy. It's a miracle it even works on real hardware.
            // Only +2 increments produce useful write patterns. You *might* find other increments useful if they-re
            // word-aligned but not longword-aligned (write address & 3 == 2) -- at least those follow the proper
            // increment intervals. Every other increment is pretty much useless in practice.

            currDstAddr &= ~1u;

            // 8-bit -> 16/32-bit realignment
            if (ch.currXferCount >= 1 && (currDstOffset & 1)) {
                const uint32 addr = currDstAddr | currDstOffset;
                const uint8 value = read8();
                m_bus.Write<uint8>(addr, value);
                currDstOffset += 1;
                ch.currXferCount -= 1;
                devlog::trace<grp::dma>("SCU DMA{}: 8-bit write to {:08X} -> {:02X}", level, addr, value);

                // B-Bus shenanigans
                if (xferLength > 1 && ch.currDstAddrInc >= 4u && currDstOffset >= 4u) {
                    // Let's double-increment just for fun, but only if the increment is at least 4 for some reason
                    currDstAddr += ch.currDstAddrInc;
                    currDstAddr &= 0x7FF'FFFF;
                }
            }

            // 16-bit -> 32-bit realignment
            if (ch.currXferCount >= 2 && (currDstOffset & 2)) {
                incDst();
                uint32 addr = (currDstAddr | currDstOffset) & ~1u;

                // B-Bus shenanigans
                if (ch.currDstAddrInc >= 4u && initialDstAlignment == 1u && currDstOffset + 2u >= 4u) {
                    // Might as well modify the address too, because who wants predictability?
                    addr += ch.currDstAddrInc;
                }

                const uint16 value = read16();
                m_bus.Write<uint16>(addr, value);
                currDstOffset += 2;
                ch.currXferCount -= 2;
                devlog::trace<grp::dma>("SCU DMA{}: 16-bit write to {:08X} -> {:04X}", level, addr, value);

                // B-Bus shenanigans
                if (xferLength > 3 && initialDstAlignment == 1u && currDstOffset >= 4u) {
                    // Let's double-increment just for fun, but this time with the full increment value
                    currDstAddr += ch.currDstAddrInc;
                    currDstAddr &= 0x7FF'FFFF;
                }
            }

            // 32-bit -> 2x 16-bit transfer -- the bulk of the DMA operation
            // B-Bus is 16-bit but the SCU seems to attempt to handle this as a 32-bit write anyway
            while (ch.currXferCount >= 4) {
                incDst();

                const uint32 addr1 = (currDstAddr | currDstOffset) & ~1u;
                const uint32 value1 = read16();
                m_bus.Write<uint16>(addr1, value1);
                currDstAddr += ch.currDstAddrInc;
                currDstAddr &= 0x7FF'FFFF;
                devlog::trace<grp::dma>("SCU DMA{}: 16-bit write to {:08X} -> {:04X}", level, addr1, value1);

                const uint32 addr2 = (currDstAddr | currDstOffset) & ~1u;
                const uint32 value2 = read16();
                m_bus.Write<uint16>(addr2, value2);
                currDstOffset += 4;
                ch.currXferCount -= 4;
                devlog::trace<grp::dma>("SCU DMA{}: 16-bit write to {:08X} -> {:04X}", level, addr2, value2);

                // B-Bus shenanigans
                if (ch.currXferCount == 0) {
                    // Sure, let's go backwards, who cares? The transfer is over anyway. It's not like we could
                    // chain multiple transfers in a row and reuse the previous write address... oh, wait.
                    currDstAddr -= ch.currDstAddrInc;
                    currDstAddr &= 0x7FF'FFFF;
                }
            }

            // Final 16-bit transfer
            if (ch.currXferCount & 2) {
                incDst();
                const uint32 addr = (currDstAddr | currDstOffset) & ~1u;
                const uint16 value = read16();
                m_bus.Write<uint16>(addr, value);
                currDstOffset += 2;
                ch.currXferCount -= 2;
                devlog::trace<grp::dma>("SCU DMA{}: 16-bit write to {:08X} -> {:04X}", level, addr, value);

                // This is the only well-behaved case; no shenanigans here
            }

            // Final 8-bit transfer
            if (ch.currXferCount & 1) {
                incDst();
                uint32 addr = currDstAddr | currDstOffset;

                // B-Bus shenanigans
                if (xferLength > 1 && ch.currDstAddrInc >= 4u && (currDstOffset & 2u)) {
                    // Are you surprised yet?
                    addr += ch.currDstAddrInc;
                }

                const uint8 value = read8();
                m_bus.Write<uint8>(addr, value);
                currDstOffset += 1;
                ch.currXferCount -= 1;
                devlog::trace<grp::dma>("SCU DMA{}: 8-bit write to {:08X} -> {:02X}", level, addr, value);
            }
        }

        devlog::trace<grp::dma>("SCU DMA{}: Addresses incremented to {:08X}, {:08X}", level, ch.currSrcAddr,
                                currDstAddr);

        assert(ch.currXferCount == 0);

        if (ch.indirect && !ch.endIndirect) {
            DMAReadIndirectTransfer(level);
        } else {
            devlog::trace<grp::dma>("SCU DMA{}: Finished transfer", level);
            ch.active = false;
            if (ch.updateSrcAddr) {
                ch.srcAddr = (ch.currSrcAddr & ~3u) + bufPos;
            }
            if (ch.updateDstAddr) {
                if (ch.indirect) {
                    ch.dstAddr = ch.currIndirectSrc;
                } else if (dstBus == BusID::BBus) {
                    // Not even this is straightforward, believe it or not
                    ch.dstAddr = ((currDstAddr & ~3u) | (currDstOffset & 3u)) + (currDstOffset & ~3u);
                } else {
                    ch.dstAddr = (currDstAddr & ~3u) + currDstOffset;
                }
            }
            ch.currDstAddr = currDstAddr;
            TriggerDMAEnd(level);
            RecalcDMAChannel();
        }
    }
}

void SCU::RecalcDMAChannel() {
    m_activeDMAChannelLevel = m_dmaChannels.size();

    for (int level = 2; level >= 0; level--) {
        auto &ch = m_dmaChannels[level];

        if (!ch.enabled) {
            continue;
        }

        auto adjustZeroSizeXferCount = [&](uint32 xferCount) -> uint32 {
            if (xferCount != 0) {
                return xferCount;
            }
            if (level == 0) {
                return 0x100000u;
            } else {
                return 0x1000u;
            }
        };

        if (ch.start && !ch.active) {
            ch.start = false;
            ch.active = true;
            if (ch.indirect) {
                ch.currIndirectSrc = ch.dstAddr;
                DMAReadIndirectTransfer(level);

                const BusID srcBus = GetBusID(ch.currSrcAddr);
                const BusID dstBus = GetBusID(ch.currDstAddr);
                if (srcBus == dstBus || srcBus == BusID::None || dstBus == BusID::None) [[unlikely]] {
                    if (srcBus == dstBus) {
                        devlog::trace<grp::dma>("SCU DMA{}: Invalid same-bus transfer; ignored", level);
                    } else if (srcBus == BusID::None) {
                        devlog::trace<grp::dma>("SCU DMA{}: Invalid source bus; transfer ignored", level);
                    } else if (dstBus == BusID::None) {
                        devlog::trace<grp::dma>("SCU DMA{}: Invalid destination bus; transfer ignored", level);
                    }
                    ch.active = false;
                    TriggerDMAIllegal();
                    RecalcDMAChannel();
                    continue;
                }
            } else {
                ch.currSrcAddr = ch.srcAddr & 0x7FF'FFFF;
                ch.currDstAddr = ch.dstAddr & 0x7FF'FFFF;
                ch.currXferCount = adjustZeroSizeXferCount(ch.xferCount);
                ch.currSrcAddrInc = ch.srcAddrInc;
                ch.currDstAddrInc = ch.dstAddrInc;

                devlog::trace<grp::dma_start>(
                    "SCU DMA{}: Starting direct transfer of {:06X} bytes from {:08X} (+{:02X}) to {:08X} (+{:02X})",
                    level, ch.currXferCount, ch.currSrcAddr, ch.currSrcAddrInc, ch.currDstAddr, ch.currDstAddrInc);
                if (ch.currSrcAddr & 1) {
                    devlog::debug<grp::dma>("SCU DMA{}: Unaligned direct transfer read from {:08X}", level,
                                            ch.currSrcAddr);
                }
                if (ch.currDstAddr & 1) {
                    devlog::debug<grp::dma>("SCU DMA{}: Unaligned direct transfer write to {:08X}", level,
                                            ch.currDstAddr);
                }
                TraceDMA(m_tracer, level, ch.currSrcAddr, ch.currDstAddr, ch.currXferCount, ch.currSrcAddrInc,
                         ch.currDstAddrInc, false, 0);
            }
            m_activeDMAChannelLevel = level;
            break;
        }
    }
}

void SCU::TriggerDMATransfer(DMATrigger trigger) {
    for (int i = 0; i < m_dmaChannels.size(); ++i) {
        auto &ch = m_dmaChannels[i];
        if (ch.enabled && !ch.active && ch.trigger == trigger) {
            devlog::trace<grp::dma>("SCU DMA{}: Transfer triggered by {}", i, ToString(trigger));
            ch.start = true;
        }
    }
    RecalcDMAChannel();
    RunDMA(); // HACK: run all event-triggered DMA transfers immediately
}

FORCE_INLINE void SCU::TickTimer1() {
    if (m_timerEnable) {
        TriggerTimer1();
    }
    m_timer1Triggered = true;
}

template <mem_primitive T, bool peek>
FORCE_INLINE T SCU::ReadReg(uint32 address) {
    if constexpr (std::is_same_v<T, uint8>) {
        const uint32 value = ReadReg<uint32, peek>(address & ~3u);
        return value >> ((~address & 3u) * 8u);
    } else if constexpr (std::is_same_v<T, uint16>) {
        const uint32 value = ReadReg<uint32, peek>(address & ~3u);
        return value >> ((~address & 2u) * 8u);
    } else {
        switch (address) {
        case 0x00: // (DMA0RA) Level 0 DMA Read Address
        case 0x20: // (DMA1RA) Level 1 DMA Read Address
        case 0x40: // (DMA2RA) Level 2 DMA Read Address
            return m_dmaChannels[address >> 5u].srcAddr;

        case 0x04: // (DMA0WA) Level 0 DMA Write Address
        case 0x24: // (DMA1WA) Level 1 DMA Write Address
        case 0x44: // (DMA2WA) Level 2 DMA Write Address
            return m_dmaChannels[address >> 5u].dstAddr;

        case 0x08: // (DMA0CNT) Level 0 DMA Transfer Number
        case 0x28: // (DMA1CNT) Level 1 DMA Transfer Number
        case 0x48: // (DMA2CNT) Level 2 DMA Transfer Number
            return m_dmaChannels[address >> 5u].xferCount;

        case 0x0C: // (DMA0ADD) Level 0 DMA Increment (write-only)
        case 0x2C: // (DMA1ADD) Level 1 DMA Increment (write-only)
        case 0x4C: // (DMA2ADD) Level 2 DMA Increment (write-only)
            if constexpr (peek) {
                auto &ch = m_dmaChannels[address >> 5u];
                uint32 value = 0;
                bit::deposit_into<8>(value, ch.srcAddrInc / 4u);
                bit::deposit_into<0, 2>(value, ch.dstAddrInc == 0 ? 0 : std::countr_zero(ch.dstAddrInc));
                return value;
            } else {
                return 0;
            }
        case 0x10: // (DMA0EN) Level 0 DMA Enable (write-only)
        case 0x30: // (DMA1EN) Level 1 DMA Enable (write-only)
        case 0x50: // (DMA2EN) Level 2 DMA Enable (write-only)
            if constexpr (peek) {
                uint32 value = 0;
                bit::deposit_into<8>(value, m_dmaChannels[address >> 5u].enabled);
                return value;
            } else {
                return 0;
            }
        case 0x14: // (DMA0MODE) Level 0 DMA Mode (write-only)
        case 0x34: // (DMA1MODE) Level 1 DMA Mode (write-only)
        case 0x54: // (DMA2MODE) Level 2 DMA Mode (write-only)
            if constexpr (peek) {
                auto &ch = m_dmaChannels[address >> 5u];
                uint32 value = 0;
                bit::deposit_into<24>(value, ch.indirect);
                bit::deposit_into<16>(value, ch.updateSrcAddr);
                bit::deposit_into<8>(value, ch.updateDstAddr);
                bit::deposit_into<0, 2>(value, static_cast<uint8>(ch.trigger));
                return value;
            } else {
                return 0;
            }

        case 0x60: // (DMA_STOP) DMA Force Stop (write-only)
            return 0;
        case 0x7C: // (DMA_STATUS) DMA Status
        {
            uint32 value = 0;
            // bit::deposit_into<0>(value, m_dsp.dmaRun); // TODO: is this correct?
            // bit::deposit_into<1>(value, m_dsp.dmaStandby?);
            bit::deposit_into<4>(value, m_dmaChannels[0].active);
            // TODO: bit 5: DMA0 standby
            bit::deposit_into<8>(value, m_dmaChannels[1].active);
            // TODO: bit 9: DMA1 standby
            bit::deposit_into<12>(value, m_dmaChannels[2].active);
            // TODO: bit 13: DMA2 standby
            bit::deposit_into<16>(value,
                                  m_dmaChannels[0].active && (m_dmaChannels[1].active || m_dmaChannels[2].active));
            bit::deposit_into<17>(value, m_dmaChannels[1].active && m_dmaChannels[2].active);
            // TODO: bit 20: DMA accessing A-Bus
            // TODO: bit 21: DMA accessing B-Bus
            // TODO: bit 22: DMA accessing DSP-Bus
            return value;
        }

        case 0x80: // (DSP_PPAF) DSP Program Control Port
        {
            uint32 value = 0;
            bit::deposit_into<0, 7>(value, m_dsp.PC);
            bit::deposit_into<16>(value, m_dsp.programExecuting && !m_dsp.programPaused);
            bit::deposit_into<18>(value, m_dsp.programEnded);
            bit::deposit_into<19>(value, m_dsp.overflow);
            bit::deposit_into<20>(value, m_dsp.carry);
            bit::deposit_into<21>(value, m_dsp.zero);
            bit::deposit_into<22>(value, m_dsp.sign);
            bit::deposit_into<23>(value, m_dsp.dmaRun);
            if constexpr (!peek) {
                m_dsp.overflow = false;
                m_dsp.programEnded = false;
            }
            return value;
        }
        case 0x84: // (DSP_PPD) DSP Program RAM Data Port (write-only)
            if constexpr (peek) {
                return m_dsp.ReadProgram();
            } else {
                return 0;
            }
        case 0x88: // (DSP_PDA) DSP Data RAM Address Port (write-only)
            if constexpr (peek) {
                return m_dsp.dataAddress;
            } else {
                return 0;
            }
        case 0x8C: // (DSP_PDD) DSP Data RAM Data Port
            return m_dsp.ReadData<peek>();

        case 0x90: // (T0C) Timer 0 Compare (write-only)
            if constexpr (peek) {
                return ReadTimer0Compare();
            } else {
                return 0;
            }
        case 0x94: // (T1S) Timer 1 Set Data (write-only)
            if constexpr (peek) {
                return ReadTimer1Reload();
            } else {
                return 0;
            }
        case 0x98: // (T1MD) Timer 1 Mode (write-only)
            if constexpr (peek) {
                uint32 value = 0;
                bit::deposit_into<0>(value, m_timerEnable);
                bit::deposit_into<8>(value, m_timer1Mode);
                return value;
            } else {
                return 0;
            }

        case 0xA0: // (IMS) Interrupt Mask
            return m_intrMask.u32;
        case 0xA4: // (IST) Interrupt Status
            return m_intrStatus.u32;
        case 0xA8: // (AIACK) A-Bus Interrupt Acknowledge (write-only)
            return 0;

        case 0xB0: // (ASR0) A-Bus Set (part 1) (write-only)
            if constexpr (peek) {
                // ignored for now
                return 0;
            } else {
                return 0;
            }
        case 0xB4: // (ASR1) A-Bus Set (part 2) (write-only)
            if constexpr (peek) {
                // ignored for now
                return 0;
            } else {
                return 0;
            }
        case 0xB8: // (AREF) A-Bus Refresh (write-only)
            if constexpr (peek) {
                // ignored for now
                return 0;
            } else {
                return 0;
            }

        case 0xC4: // (RSEL) SCU SDRAM Select
            return m_WRAMSizeSelect;
        case 0xC8: // (VER) SCU Version
            return 0x4;

        default: //
            if constexpr (!peek) {
                devlog::debug<grp::regs>("unhandled {}-bit SCU register read from {:02X}", sizeof(T) * 8, address);
            }
            return 0;
        }
    }
}

template <bool poke>
FORCE_INLINE void SCU::WriteRegByte(uint32 address, uint8 value) {
    switch (address) {
    case 0x00: // (DMA0RA) Level 0 DMA Read Address (bits 24-31)
    case 0x20: // (DMA1RA) Level 1 DMA Read Address (bits 24-31)
    case 0x40: // (DMA2RA) Level 2 DMA Read Address (bits 24-31)
        bit::deposit_into<24, 26>(m_dmaChannels[address >> 5u].srcAddr, value);
        break;

    case 0x01: // (DMA0RA) Level 0 DMA Read Address (bits 16-23)
    case 0x21: // (DMA1RA) Level 1 DMA Read Address (bits 16-23)
    case 0x41: // (DMA2RA) Level 2 DMA Read Address (bits 16-23)
        bit::deposit_into<16, 23>(m_dmaChannels[address >> 5u].srcAddr, value);
        break;

    case 0x02: // (DMA0RA) Level 0 DMA Read Address (bits 8-15)
    case 0x22: // (DMA1RA) Level 1 DMA Read Address (bits 8-15)
    case 0x42: // (DMA2RA) Level 2 DMA Read Address (bits 8-15)
        bit::deposit_into<8, 15>(m_dmaChannels[address >> 5u].srcAddr, value);
        break;

    case 0x03: // (DMA0RA) Level 0 DMA Read Address (bits 0-7)
    case 0x23: // (DMA1RA) Level 1 DMA Read Address (bits 0-7)
    case 0x43: // (DMA2RA) Level 2 DMA Read Address (bits 0-7)
        bit::deposit_into<0, 7>(m_dmaChannels[address >> 5u].srcAddr, value);
        break;

    case 0x04: // (DMA0WA) Level 0 DMA Write Address (bits 24-31)
    case 0x24: // (DMA1WA) Level 1 DMA Write Address (bits 24-31)
    case 0x44: // (DMA2WA) Level 2 DMA Write Address (bits 24-31)
        bit::deposit_into<24, 26>(m_dmaChannels[address >> 5u].dstAddr, value);
        break;

    case 0x05: // (DMA0WA) Level 0 DMA Write Address (bits 16-23)
    case 0x25: // (DMA1WA) Level 1 DMA Write Address (bits 16-23)
    case 0x45: // (DMA2WA) Level 2 DMA Write Address (bits 16-23)
        bit::deposit_into<16, 23>(m_dmaChannels[address >> 5u].dstAddr, value);
        break;

    case 0x06: // (DMA0WA) Level 0 DMA Write Address (bits 8-15)
    case 0x26: // (DMA1WA) Level 1 DMA Write Address (bits 8-15)
    case 0x46: // (DMA2WA) Level 2 DMA Write Address (bits 8-15)
        bit::deposit_into<8, 15>(m_dmaChannels[address >> 5u].dstAddr, value);
        break;

    case 0x07: // (DMA0WA) Level 0 DMA Write Address (bits 0-7)
    case 0x27: // (DMA1WA) Level 1 DMA Write Address (bits 0-7)
    case 0x47: // (DMA2WA) Level 2 DMA Write Address (bits 0-7)
        bit::deposit_into<0, 7>(m_dmaChannels[address >> 5u].dstAddr, value);
        break;

    case 0x08: // (DMA0CNT) Level 0 DMA Transfer Number (bits 24-31)
    case 0x28: // (DMA1CNT) Level 1 DMA Transfer Number (bits 24-31)
    case 0x48: // (DMA2CNT) Level 2 DMA Transfer Number (bits 24-31)
        // Nothing to write
        break;

    case 0x09: // (DMA0CNT) Level 0 DMA Transfer Number (bits 16-23)
        bit::deposit_into<16, 19>(m_dmaChannels[0].xferCount, value);
        break;
    case 0x29: // (DMA1CNT) Level 1 DMA Transfer Number (bits 16-23)
    case 0x49: // (DMA2CNT) Level 2 DMA Transfer Number (bits 16-23)
        // Nothing to write
        break;

    case 0x0A: // (DMA0CNT) Level 0 DMA Transfer Number (bits 8-15)
        bit::deposit_into<8, 15>(m_dmaChannels[0].xferCount, value);
        break;
    case 0x2A: // (DMA1CNT) Level 1 DMA Transfer Number (bits 8-15)
    case 0x4A: // (DMA2CNT) Level 2 DMA Transfer Number (bits 8-15)
        bit::deposit_into<8, 11>(m_dmaChannels[address >> 5u].xferCount, value);
        break;

    case 0x0B: // (DMA0CNT) Level 0 DMA Transfer Number (bits 0-7)
        bit::deposit_into<0, 7>(m_dmaChannels[0].xferCount, value);
        break;
    case 0x2B: // (DMA1CNT) Level 1 DMA Transfer Number (bits 0-7)
    case 0x4B: // (DMA2CNT) Level 2 DMA Transfer Number (bits 0-7)
        bit::deposit_into<0, 7>(m_dmaChannels[address >> 5u].xferCount, value);
        break;

    case 0x0C: // (DMA0ADD) Level 0 DMA Increment (bits 24-31)
    case 0x2C: // (DMA1ADD) Level 1 DMA Increment (bits 24-31)
    case 0x4C: // (DMA2ADD) Level 2 DMA Increment (bits 24-31)
        // Nothing to write
        break;
    case 0x0D: // (DMA0ADD) Level 0 DMA Increment (bits 16-23)
    case 0x2D: // (DMA1ADD) Level 1 DMA Increment (bits 16-23)
    case 0x4D: // (DMA2ADD) Level 2 DMA Increment (bits 16-23)
        m_dmaChannels[address >> 5u].srcAddrInc = bit::extract<0>(value) * 4u;
        break;
    case 0x0E: // (DMA0ADD) Level 0 DMA Increment (bits 8-15)
    case 0x2E: // (DMA1ADD) Level 1 DMA Increment (bits 8-15)
    case 0x4E: // (DMA2ADD) Level 2 DMA Increment (bits 8-15)
        // Nothing to write
        break;
    case 0x0F: // (DMA0ADD) Level 0 DMA Increment (bits 0-7)
    case 0x2F: // (DMA1ADD) Level 1 DMA Increment (bits 0-7)
    case 0x4F: // (DMA2ADD) Level 2 DMA Increment (bits 0-7)
        m_dmaChannels[address >> 5u].dstAddrInc = (1u << bit::extract<0, 2>(value)) & ~1u;
        break;

    case 0x10: // (DMA0EN) Level 0 DMA Enable (bits 24-31)
    case 0x30: // (DMA1EN) Level 1 DMA Enable (bits 24-31)
    case 0x50: // (DMA2EN) Level 2 DMA Enable (bits 24-31)
        // Nothing to write
        break;

    case 0x11: // (DMA0EN) Level 0 DMA Enable (bits 16-23)
    case 0x31: // (DMA1EN) Level 1 DMA Enable (bits 16-23)
    case 0x51: // (DMA2EN) Level 2 DMA Enable (bits 16-23)
        // Nothing to write
        break;

    case 0x12: // (DMA0EN) Level 0 DMA Enable (bits 8-15)
    case 0x32: // (DMA1EN) Level 1 DMA Enable (bits 8-15)
    case 0x52: // (DMA2EN) Level 2 DMA Enable (bits 8-15)
    {
        const uint32 index = address >> 5u;
        auto &ch = m_dmaChannels[index];
        ch.enabled = bit::test<0>(value);
        if constexpr (!poke) {
            if (ch.enabled) {
                devlog::trace<grp::dma>("DMA{} enabled - {:08X} (+{:02X}){} -> {:08X} (+{:02X}){} ({}), {}", index,
                                        ch.srcAddr, ch.srcAddrInc, (ch.updateSrcAddr ? "!" : ""), ch.dstAddr,
                                        ch.dstAddrInc, (ch.updateDstAddr ? "!" : ""),
                                        (ch.indirect ? "indirect" : "direct"), ToString(ch.trigger));
            }
        }
        break;
    }

    case 0x13: // (DMA0EN) Level 0 DMA Enable (bits 0-7)
    case 0x33: // (DMA1EN) Level 1 DMA Enable (bits 0-7)
    case 0x53: // (DMA2EN) Level 2 DMA Enable (bits 0-7)
    {
        const uint32 index = address >> 5u;
        auto &ch = m_dmaChannels[index];
        if constexpr (!poke) {
            if (ch.enabled && ch.trigger == DMATrigger::Immediate && bit::test<0>(value)) {
                if (ch.active) {
                    devlog::trace<grp::dma>("DMA{} triggering immediate transfer while another transfer is in progress",
                                            index);
                    // Finish previous transfer
                    RunDMA();
                }
                devlog::trace<grp::dma>("SCU DMA{}: Transfer triggered immediately", index);
                ch.start = true;
                RecalcDMAChannel();
                RunDMA(); // HACK: run immediate DMA transfers immediately and instantly
            }
        }
        break;
    }

    case 0x14: // (DMA0MODE) Level 0 DMA Mode (bits 24-31)
    case 0x34: // (DMA1MODE) Level 1 DMA Mode (bits 24-31)
    case 0x54: // (DMA2MODE) Level 2 DMA Mode (bits 24-31)
        m_dmaChannels[address >> 5u].indirect = bit::test<0>(value);
        break;

    case 0x15: // (DMA0MODE) Level 0 DMA Mode (bits 16-23)
    case 0x35: // (DMA1MODE) Level 1 DMA Mode (bits 16-23)
    case 0x55: // (DMA2MODE) Level 2 DMA Mode (bits 16-23)
        m_dmaChannels[address >> 5u].updateSrcAddr = bit::test<0>(value);
        break;

    case 0x16: // (DMA0MODE) Level 0 DMA Mode (bits 8-15)
    case 0x36: // (DMA1MODE) Level 1 DMA Mode (bits 8-15)
    case 0x56: // (DMA2MODE) Level 2 DMA Mode (bits 8-15)
        m_dmaChannels[address >> 5u].updateDstAddr = bit::test<0>(value);
        break;

    case 0x17: // (DMA0MODE) Level 0 DMA Mode (bits 0-7)
    case 0x37: // (DMA1MODE) Level 1 DMA Mode (bits 0-7)
    case 0x57: // (DMA2MODE) Level 2 DMA Mode (bits 0-7)
        m_dmaChannels[address >> 5u].trigger = static_cast<DMATrigger>(bit::extract<0, 2>(value));
        break;

    case 0x60: // (DMA_STOP) DMA Force Stop (bits 24-31)
    case 0x61: // (DMA_STOP) DMA Force Stop (bits 16-23)
    case 0x62: // (DMA_STOP) DMA Force Stop (bits 8-15)
        // Nothing to write
        break;
    case 0x63: // (DMA_STOP) DMA Force Stop (bits 0-7)
        if constexpr (!poke) {
            if (bit::test<0>(value)) {
                for (auto &ch : m_dmaChannels) {
                    ch.active = false;
                }
                m_activeDMAChannelLevel = m_dmaChannels.size();
            }
        }
        break;

    case 0x7C: // (DMA_STATUS) DMA Status (read-only) (bits 16-31)
    case 0x7D: // (DMA_STATUS) DMA Status (read-only) (bits 16-31)
    case 0x7E: // (DMA_STATUS) DMA Status (read-only) (bits 0-15)
    case 0x7F: // (DMA_STATUS) DMA Status (read-only) (bits 0-15)
        break;

    case 0x80: // (DSP_PPAF) DSP Program Control Port (bits 24-31)
        // TODO: should this go through? should it be buffered? ignored?
        // if (bit::test<1>(value)) {
        //     m_dsp.programPaused = true;
        // } else if (bit::test<2>(value)) {
        //     m_dsp.programPaused = false;
        // }
        break;
    case 0x81: // (DSP_PPAF) DSP Program Control Port (bits 16-23)
        // TODO: should this go through? should it be buffered? ignored?
        // m_dsp.programExecuting = bit::test<0>(value);
        // m_dsp.programStep = bit::test<1>(value);
        break;
    case 0x82: // (DSP_PPAF) DSP Program Control Port (bits 8-15)
        // TODO: should this go through? should it be buffered? ignored?
        break;
    case 0x83: // (DSP_PPAF) DSP Program Control Port (bits 0-7)
        // TODO: should this go through? should it be buffered? ignored?
        // if (bit::test<7>(value at 0x82)) {
        //     m_dsp.WritePC<poke>(bit::extract<0, 7>(value));
        // }
        break;

    case 0x84: // (DSP_PPD) DSP Program RAM Data Port (bits 24-31)
    case 0x85: // (DSP_PPD) DSP Program RAM Data Port (bits 16-23)
    case 0x86: // (DSP_PPD) DSP Program RAM Data Port (bits 8-15)
    case 0x87: // (DSP_PPD) DSP Program RAM Data Port (bits 0-7)
        // TODO: should this go through? should it be buffered? ignored?
        // m_dsp.WriteProgram<poke>(value);
        break;

    case 0x88: // (DSP_PDA) DSP Data RAM Address Port (bits 24-31)
    case 0x89: // (DSP_PDA) DSP Data RAM Address Port (bits 16-23)
    case 0x8A: // (DSP_PDA) DSP Data RAM Address Port (bits 8-15)
        // Nothing to write
        break;
    case 0x8B: // (DSP_PDA) DSP Data RAM Address Port (bits 0-7)
        m_dsp.dataAddress = value;
        break;

    case 0x8C: // (DSP_PDD) DSP Data RAM Data Port (bits 24-31)
    case 0x8D: // (DSP_PDD) DSP Data RAM Data Port (bits 16-23)
    case 0x8E: // (DSP_PDD) DSP Data RAM Data Port (bits 8-15)
    case 0x8F: // (DSP_PDD) DSP Data RAM Data Port (bits 0-7)
        // TODO: should this go through? should it be buffered? ignored?
        // m_dsp.WriteData<poke>(value);
        break;

    case 0x90: // (T0C) Timer 0 Compare (bits 24-31)
    case 0x91: // (T0C) Timer 0 Compare (bits 16-23)
        // Nothing to write
        break;
    case 0x92: // (T0C) Timer 0 Compare (bits 8-15)
        WriteTimer0CompareHi(value);
        break;
    case 0x93: // (T0C) Timer 0 Compare (bits 0-7)
        WriteTimer0CompareLo(value);
        break;

    case 0x94: // (T1S) Timer 1 Set Data (bits 24-31)
    case 0x95: // (T1S) Timer 1 Set Data (bits 16-23)
        // Nothing to write
        break;
    case 0x96: // (T1S) Timer 1 Set Data (bits 8-15)
        WriteTimer1ReloadHi(value);
        break;
    case 0x97: // (T1S) Timer 1 Set Data (bits 0-7)
        WriteTimer1ReloadLo(value);
        break;

    case 0x98: // (T1MD) Timer 1 Mode (bits 24-31)
    case 0x99: // (T1MD) Timer 1 Mode (bits 16-23)
        // Nothing to write
        break;
    case 0x9A: // (T1MD) Timer 1 Mode (bits 8-15)
        m_timer1Mode = bit::test<0>(value);
        break;
    case 0x9B: // (T1MD) Timer 1 Mode (bits 0-7)
        m_timerEnable = bit::test<0>(value);
        break;

    case 0xA0: break; // (IMS) Interrupt Mask (bits 24-31)
    case 0xA1: break; // (IMS) Interrupt Mask (bits 16-23)
    case 0xA2:        // (IMS) Interrupt Mask (bits 8-15)
        m_intrMask.u32 = (value << 8u) & 0x0000BF00;
        if constexpr (!poke) {
            UpdateMasterInterruptLevel();
        }
        break;
    case 0xA3: // (IMS) Interrupt Mask (bits 0-7)
        m_intrMask.u32 = (value << 0u) & 0x000000FF;
        if constexpr (!poke) {
            UpdateMasterInterruptLevel();
        }
        break;

    case 0xA4: m_intrStatus.u32 &= (value << 24u) | 0x00FFFFFF; break; // (IST) Interrupt Status (bits 24-31)
    case 0xA5: m_intrStatus.u32 &= (value << 16u) | 0xFF00FFFF; break; // (IST) Interrupt Status (bits 16-23)
    case 0xA6: m_intrStatus.u32 &= (value << 8u) | 0xFFFF00FF; break;  // (IST) Interrupt Status (bits 8-15)
    case 0xA7: m_intrStatus.u32 &= (value << 0u) | 0xFFFFFF00; break;  // (IST) Interrupt Status (bits 0-7)

    case 0xA8: break; // (AIACK) A-Bus Interrupt Acknowledge (bits 24-31)
    case 0xA9: break; // (AIACK) A-Bus Interrupt Acknowledge (bits 16-23)
    case 0xAA: break; // (AIACK) A-Bus Interrupt Acknowledge (bits 8-15)
    case 0xAB:        // (AIACK) A-Bus Interrupt Acknowledge (bits 0-7)
        if (bit::test<0>(value)) {
            m_abusIntrsPendingAck = 0;
        }
        if constexpr (!poke) {
            UpdateMasterInterruptLevel();
        }
        break;

    case 0xB0: // (ASR0) A-Bus Set (part 1, bits 24-31)
    case 0xB1: // (ASR0) A-Bus Set (part 1, bits 16-23)
    case 0xB2: // (ASR0) A-Bus Set (part 1, bits 8-15)
    case 0xB3: // (ASR0) A-Bus Set (part 1, bits 0-7)
        // ignored for now
        break;
    case 0xB4: // (ASR1) A-Bus Set (part 2, bits 24-31)
    case 0xB5: // (ASR1) A-Bus Set (part 2, bits 16-23)
    case 0xB6: // (ASR1) A-Bus Set (part 2, bits 8-15)
    case 0xB7: // (ASR1) A-Bus Set (part 2, bits 0-7)
        // ignored for now
        break;
    case 0xB8: // (AREF) A-Bus Refresh (bits 24-31)
    case 0xB9: // (AREF) A-Bus Refresh (bits 16-23)
    case 0xBA: // (AREF) A-Bus Refresh (bits 8-15)
    case 0xBB: // (AREF) A-Bus Refresh (bits 0-7)
        // ignored for now
        break;

    case 0xC8: // (VER) SCU Version (read-only, bits 24-31)
    case 0xC9: // (VER) SCU Version (read-only, bits 16-23)
    case 0xCA: // (VER) SCU Version (read-only, bits 8-15)
    case 0xCB: // (VER) SCU Version (read-only, bits 0-7)
        break;

    default:
        if constexpr (!poke) {
            devlog::debug<grp::regs>("unhandled 8-bit SCU register write to {:02X} = {:X}", address, value);
        }
        break;
    }
}

template <bool poke>
FORCE_INLINE void SCU::WriteRegWord(uint32 address, uint16 value) {
    switch (address) {
    case 0x00: // (DMA0RA) Level 0 DMA Read Address (bits 16-31)
    case 0x20: // (DMA1RA) Level 1 DMA Read Address (bits 16-31)
    case 0x40: // (DMA2RA) Level 2 DMA Read Address (bits 16-31)
        bit::deposit_into<16, 26>(m_dmaChannels[address >> 5u].srcAddr, value);
        break;

    case 0x02: // (DMA0RA) Level 0 DMA Read Address (bits 0-15)
    case 0x22: // (DMA1RA) Level 1 DMA Read Address (bits 0-15)
    case 0x42: // (DMA2RA) Level 2 DMA Read Address (bits 0-15)
        bit::deposit_into<0, 15>(m_dmaChannels[address >> 5u].srcAddr, value);
        break;

    case 0x04: // (DMA0WA) Level 0 DMA Write Address (bits 16-31)
    case 0x24: // (DMA1WA) Level 1 DMA Write Address (bits 16-31)
    case 0x44: // (DMA2WA) Level 2 DMA Write Address (bits 16-31)
        bit::deposit_into<16, 26>(m_dmaChannels[address >> 5u].dstAddr, value);
        break;

    case 0x06: // (DMA0WA) Level 0 DMA Write Address (bits 0-15)
    case 0x26: // (DMA1WA) Level 1 DMA Write Address (bits 0-15)
    case 0x46: // (DMA2WA) Level 2 DMA Write Address (bits 0-15)
        bit::deposit_into<0, 15>(m_dmaChannels[address >> 5u].dstAddr, value);
        break;

    case 0x08: // (DMA0CNT) Level 0 DMA Transfer Number (bits 16-31)
        bit::deposit_into<16, 19>(m_dmaChannels[0].xferCount, value);
        break;
    case 0x28: // (DMA1CNT) Level 1 DMA Transfer Number (bits 16-31)
    case 0x48: // (DMA2CNT) Level 2 DMA Transfer Number (bits 16-31)
        // Nothing to write
        break;

    case 0x0A: // (DMA0CNT) Level 0 DMA Transfer Number (bits 0-15)
        bit::deposit_into<0, 15>(m_dmaChannels[0].xferCount, value);
        break;
    case 0x2A: // (DMA1CNT) Level 1 DMA Transfer Number (bits 0-15)
    case 0x4A: // (DMA2CNT) Level 2 DMA Transfer Number (bits 0-15)
        bit::deposit_into<0, 11>(m_dmaChannels[address >> 5u].xferCount, value);
        break;

    case 0x0C: // (DMA0ADD) Level 0 DMA Increment (bits 16-31)
    case 0x2C: // (DMA1ADD) Level 1 DMA Increment (bits 16-31)
    case 0x4C: // (DMA2ADD) Level 2 DMA Increment (bits 16-31)
        // Nothing to write
        break;
    case 0x0E: // (DMA0ADD) Level 0 DMA Increment (bits 0-15)
    case 0x2E: // (DMA1ADD) Level 1 DMA Increment (bits 0-15)
    case 0x4E: // (DMA2ADD) Level 2 DMA Increment (bits 0-15)
    {
        auto &ch = m_dmaChannels[address >> 5u];
        ch.srcAddrInc = bit::extract<8>(value) * 4u;
        ch.dstAddrInc = (1u << bit::extract<0, 2>(value)) & ~1u;
        break;
    }

    case 0x10: // (DMA0EN) Level 0 DMA Enable (bits 16-31)
    case 0x30: // (DMA1EN) Level 1 DMA Enable (bits 16-31)
    case 0x50: // (DMA2EN) Level 2 DMA Enable (bits 16-31)
        // Nothing to write
        break;

    case 0x12: // (DMA0EN) Level 0 DMA Enable (bits 0-15)
    case 0x32: // (DMA1EN) Level 1 DMA Enable (bits 0-15)
    case 0x52: // (DMA2EN) Level 2 DMA Enable (bits 0-15)
    {
        const uint32 index = address >> 5u;
        auto &ch = m_dmaChannels[index];
        ch.enabled = bit::test<8>(value);
        if constexpr (!poke) {
            if (ch.enabled) {
                devlog::trace<grp::dma>("DMA{} enabled - {:08X} (+{:02X}){} -> {:08X} (+{:02X}){} ({}), {}", index,
                                        ch.srcAddr, ch.srcAddrInc, (ch.updateSrcAddr ? "!" : ""), ch.dstAddr,
                                        ch.dstAddrInc, (ch.updateDstAddr ? "!" : ""),
                                        (ch.indirect ? "indirect" : "direct"), ToString(ch.trigger));
            }
            if (ch.enabled && ch.trigger == DMATrigger::Immediate && bit::test<0>(value)) {
                if (ch.active) {
                    devlog::trace<grp::dma>("DMA{} triggering immediate transfer while another transfer is in progress",
                                            index);
                    // Finish previous transfer
                    RunDMA();
                }
                devlog::trace<grp::dma>("SCU DMA{}: Transfer triggered immediately", index);
                ch.start = true;
                RecalcDMAChannel();
                RunDMA(); // HACK: run immediate DMA transfers immediately and instantly
            }
        }
        break;
    }

    case 0x14: // (DMA0MODE) Level 0 DMA Mode (bits 16-31)
    case 0x34: // (DMA1MODE) Level 1 DMA Mode (bits 16-31)
    case 0x54: // (DMA2MODE) Level 2 DMA Mode (bits 16-31)
    {
        auto &ch = m_dmaChannels[address >> 5u];
        ch.indirect = bit::test<8>(value);
        ch.updateSrcAddr = bit::test<0>(value);
        break;
    }

    case 0x16: // (DMA0MODE) Level 0 DMA Mode (bits 0-15)
    case 0x36: // (DMA1MODE) Level 1 DMA Mode (bits 0-15)
    case 0x56: // (DMA2MODE) Level 2 DMA Mode (bits 0-15)
    {
        auto &ch = m_dmaChannels[address >> 5u];
        ch.updateDstAddr = bit::test<8>(value);
        ch.trigger = static_cast<DMATrigger>(bit::extract<0, 2>(value));
        break;
    }

    case 0x60: // (DMA_STOP) DMA Force Stop (bits 16-31)
        // Nothing to write
        break;
    case 0x62: // (DMA_STOP) DMA Force Stop (bits 0-15)
        if constexpr (!poke) {
            if (bit::test<0>(value)) {
                for (auto &ch : m_dmaChannels) {
                    ch.active = false;
                }
                m_activeDMAChannelLevel = m_dmaChannels.size();
            }
        }
        break;

    case 0x7C: // (DMA_STATUS) DMA Status (read-only) (bits 16-31)
    case 0x7E: // (DMA_STATUS) DMA Status (read-only) (bits 0-15)
        break;

    case 0x80: // (DSP_PPAF) DSP Program Control Port (bits 16-31)
        if (bit::test<9>(value)) {
            m_dsp.programPaused = true;
        } else if (bit::test<10>(value)) {
            m_dsp.programPaused = false;
        } else /*if (!m_dsp.programExecuting)*/ {
            m_dsp.programExecuting = bit::test<0>(value);
            m_dsp.programStep = bit::test<1>(value);
        }
        break;
    case 0x82: // (DSP_PPAF) DSP Program Control Port (bits 0-15)
        if (bit::test<15>(value)) {
            m_dsp.WritePC<poke>(bit::extract<0, 7>(value));
        }
        break;

    case 0x84: // (DSP_PPD) DSP Program RAM Data Port (bits 16-31)
    case 0x86: // (DSP_PPD) DSP Program RAM Data Port (bits 0-15)
        // TODO: should this go through? should it be buffered? ignored?
        // m_dsp.WriteProgram<poke>(value);
        break;

    case 0x88: // (DSP_PDA) DSP Data RAM Address Port (bits 16-31)
        // Nothing to write
        break;
    case 0x8A: // (DSP_PDA) DSP Data RAM Address Port (bits 0-15)
        m_dsp.dataAddress = bit::extract<0, 7>(value);
        break;

    case 0x8C: // (DSP_PDD) DSP Data RAM Data Port (bits 16-31)
    case 0x8E: // (DSP_PDD) DSP Data RAM Data Port (bits 0-15)
        // TODO: should this go through? should it be buffered? ignored?
        // m_dsp.WriteData<poke>(value);
        break;

    case 0x90: // (T0C) Timer 0 Compare (bits 16-31)
        // Nothing to write
        break;
    case 0x92: // (T0C) Timer 0 Compare (bits 0-15)
        WriteTimer0Compare(value);
        break;

    case 0x94: // (T1S) Timer 1 Set Data (bits 16-31)
        // Nothing to write
        break;
    case 0x96: // (T1S) Timer 1 Set Data (bits 0-15)
        WriteTimer1Reload(value);
        break;

    case 0x98: // (T1MD) Timer 1 Mode (bits 16-31)
        // Nothing to write
        break;
    case 0x9A: // (T1MD) Timer 1 Mode (bits 0-15)
        m_timerEnable = bit::test<0>(value);
        m_timer1Mode = bit::test<8>(value);
        break;

    case 0xA0: // (IMS) Interrupt Mask (bits 16-31)
        // Nothing to write
        break;
    case 0xA2: // (IMS) Interrupt Mask (bits 0-15)
        m_intrMask.u32 = value & 0xBFFF;
        if constexpr (!poke) {
            UpdateMasterInterruptLevel();
        }
        break;

    case 0xA4: // (IST) Interrupt Status (bits 16-31)
        if constexpr (poke) {
            m_intrStatus.u32 = value;
        } else {
            m_intrStatus.u32 &= value;
        }
        break;
    case 0xA6: // (IST) Interrupt Status (bits 0-15)
        if constexpr (poke) {
            m_intrStatus.u32 = value & 0xBFFF;
        } else {
            m_intrStatus.u32 &= value;
        }
        break;

    case 0xA8: // (AIACK) A-Bus Interrupt Acknowledge (bits 16-31)
        // Nothing to write
        break;
    case 0xAA: // (AIACK) A-Bus Interrupt Acknowledge (bits 0-15)
        if (bit::test<0>(value)) {
            m_abusIntrsPendingAck = 0;
        }
        if constexpr (!poke) {
            UpdateMasterInterruptLevel();
        }
        break;

    case 0xB0: // (ASR0) A-Bus Set (part 1) (bits 16-31)
    case 0xB2: // (ASR0) A-Bus Set (part 1) (bits 0-15)
        // ignored for now
        break;
    case 0xB4: // (ASR1) A-Bus Set (part 2) (bits 16-31)
    case 0xB6: // (ASR1) A-Bus Set (part 2) (bits 0-15)
        // ignored for now
        break;
    case 0xB8: // (AREF) A-Bus Refresh (bits 16-31)
    case 0xBA: // (AREF) A-Bus Refresh (bits 0-15)
        // ignored for now
        break;

    case 0xC4: // (RSEL) SCU SDRAM Select (bits 16-31)
        // Nothing to write
        break;
    case 0xC6: // (RSEL) SCU SDRAM Select (bits 0-15)
        WriteWRAMSizeSelect(bit::test<0>(value));
        break;

    case 0xC8: // (VER) SCU Version (read-only) (bits 16-31)
    case 0xCA: // (VER) SCU Version (read-only) (bits 0-15)
        break;

    default:
        if constexpr (!poke) {
            devlog::debug<grp::regs>("Unhandled 16-bit SCU register write to {:02X} = {:02X}", address, value);
        }
        break;
    }
}

template <bool poke>
FORCE_INLINE void SCU::WriteRegLong(uint32 address, uint32 value) {
    switch (address) {
    case 0x00: // (DMA0RA) Level 0 DMA Read Address
    case 0x20: // (DMA1RA) Level 1 DMA Read Address
    case 0x40: // (DMA2RA) Level 2 DMA Read Address
        m_dmaChannels[address >> 5u].srcAddr = bit::extract<0, 26>(value);
        break;

    case 0x04: // (DMA0WA) Level 0 DMA Write Address
    case 0x24: // (DMA1WA) Level 1 DMA Write Address
    case 0x44: // (DMA2WA) Level 2 DMA Write Address
        m_dmaChannels[address >> 5u].dstAddr = bit::extract<0, 26>(value);
        break;

    case 0x08: // (DMA0CNT) Level 0 DMA Transfer Number
        m_dmaChannels[0].xferCount = bit::extract<0, 19>(value);
        break;

    case 0x28: // (DMA1CNT) Level 1 DMA Transfer Number
    case 0x48: // (DMA2CNT) Level 2 DMA Transfer Number
        m_dmaChannels[address >> 5u].xferCount = bit::extract<0, 11>(value);
        break;

    case 0x0C: // (DMA0ADD) Level 0 DMA Increment
    case 0x2C: // (DMA1ADD) Level 1 DMA Increment
    case 0x4C: // (DMA2ADD) Level 2 DMA Increment
    {
        auto &ch = m_dmaChannels[address >> 5u];
        ch.srcAddrInc = bit::extract<8>(value) * 4u;
        ch.dstAddrInc = (1u << bit::extract<0, 2>(value)) & ~1u;
        break;
    }
    case 0x10: // (DMA0EN) Level 0 DMA Enable
    case 0x30: // (DMA1EN) Level 1 DMA Enable
    case 0x50: // (DMA2EN) Level 2 DMA Enable
    {
        const uint32 index = address >> 5u;
        auto &ch = m_dmaChannels[index];
        ch.enabled = bit::test<8>(value);
        if constexpr (!poke) {
            if (ch.enabled) {
                devlog::trace<grp::dma>("DMA{} enabled - {:08X} (+{:02X}){} -> {:08X} (+{:02X}){} ({}), {}", index,
                                        ch.srcAddr, ch.srcAddrInc, (ch.updateSrcAddr ? "!" : ""), ch.dstAddr,
                                        ch.dstAddrInc, (ch.updateDstAddr ? "!" : ""),
                                        (ch.indirect ? "indirect" : "direct"), ToString(ch.trigger));
            }
            if (ch.enabled && ch.trigger == DMATrigger::Immediate && bit::test<0>(value)) {
                if (ch.active) {
                    devlog::trace<grp::dma>("DMA{} triggering immediate transfer while another transfer is in progress",
                                            index);
                    // Finish previous transfer
                    RunDMA();
                }
                devlog::trace<grp::dma>("SCU DMA{}: Transfer triggered immediately", index);
                ch.start = true;
                RecalcDMAChannel();
                RunDMA(); // HACK: run immediate DMA transfers immediately and instantly
            }
        }
        break;
    }
    case 0x14: // (DMA0MODE) Level 0 DMA Mode
    case 0x34: // (DMA1MODE) Level 1 DMA Mode
    case 0x54: // (DMA2MODE) Level 2 DMA Mode
    {
        auto &ch = m_dmaChannels[address >> 5u];
        ch.indirect = bit::test<24>(value);
        ch.updateSrcAddr = bit::test<16>(value);
        ch.updateDstAddr = bit::test<8>(value);
        ch.trigger = static_cast<DMATrigger>(bit::extract<0, 2>(value));
        break;
    }

    case 0x60: // (DMA_STOP) DMA Force Stop
        if constexpr (!poke) {
            if (bit::test<0>(value)) {
                for (auto &ch : m_dmaChannels) {
                    ch.active = false;
                }
                m_activeDMAChannelLevel = m_dmaChannels.size();
            }
        }
        break;
    case 0x7C: // (DMA_STATUS) DMA Status (read-only)
        break;

    case 0x80: // (DSP_PPAF) DSP Program Control Port
        if (bit::test<15>(value)) {
            m_dsp.WritePC<poke>(bit::extract<0, 7>(value));
        }
        if (bit::test<25>(value)) {
            m_dsp.programPaused = true;
        } else if (bit::test<26>(value)) {
            m_dsp.programPaused = false;
        } else /*if (!m_dsp.programExecuting)*/ {
            m_dsp.programExecuting = bit::test<16>(value);
            m_dsp.programStep = bit::test<17>(value);
        }
        break;
    case 0x84: // (DSP_PPD) DSP Program RAM Data Port
        m_dsp.WriteProgram<poke>(value);
        break;
    case 0x88: // (DSP_PDA) DSP Data RAM Address Port
        m_dsp.dataAddress = bit::extract<0, 7>(value);
        break;
    case 0x8C: // (DSP_PDD) DSP Data RAM Data Port
        m_dsp.WriteData<poke>(value);
        break;

    case 0x90: // (T0C) Timer 0 Compare
        WriteTimer0Compare(value);
        break;
    case 0x94: // (T1S) Timer 1 Set Data
        WriteTimer1Reload(value);
        break;
    case 0x98: // (T1MD) Timer 1 Mode
        m_timerEnable = bit::test<0>(value);
        m_timer1Mode = bit::test<8>(value);
        break;

    case 0xA0: // (IMS) Interrupt Mask
        m_intrMask.u32 = value & 0x0000BFFF;
        if constexpr (!poke) {
            UpdateMasterInterruptLevel();
        }
        break;
    case 0xA4: // (IST) Interrupt Status
        if constexpr (poke) {
            m_intrStatus.u32 = value & 0xFFFFBFFF;
        } else {
            m_intrStatus.u32 &= value;
        }
        break;
    case 0xA8: // (AIACK) A-Bus Interrupt Acknowledge
        if (bit::test<0>(value)) {
            m_abusIntrsPendingAck = 0;
        }
        if constexpr (!poke) {
            UpdateMasterInterruptLevel();
        }
        break;

    case 0xB0: // (ASR0) A-Bus Set (part 1)
        // ignored for now
        break;
    case 0xB4: // (ASR1) A-Bus Set (part 2)
        // ignored for now
        break;
    case 0xB8: // (AREF) A-Bus Refresh
        // ignored for now
        break;

    case 0xC4: // (RSEL) SCU SDRAM Select
        WriteWRAMSizeSelect(bit::test<0>(value));
        break;
    case 0xC8: // (VER) SCU Version (read-only)
        break;

    default:
        if constexpr (!poke) {
            devlog::debug<grp::regs>("Unhandled 32-bit SCU register write to {:02X} = {:04X}", address, value);
        }
        break;
    }
}

// -----------------------------------------------------------------------------
// Probe implementation

SCU::Probe::Probe(SCU &scu)
    : m_scu(scu) {}

// Registers

bool SCU::Probe::GetWRAMSizeSelect() const {
    return m_scu.m_WRAMSizeSelect;
}

void SCU::Probe::SetWRAMSizeSelect(bool value) const {
    m_scu.WriteWRAMSizeSelect(value);
}

// Interrupts

InterruptMask &SCU::Probe::GetInterruptMask() {
    return m_scu.m_intrMask;
}

InterruptStatus &SCU::Probe::GetInterruptStatus() {
    return m_scu.m_intrStatus;
}

uint16 &SCU::Probe::GetABusInterruptsPendingAcknowledge() {
    return m_scu.m_abusIntrsPendingAck;
}

const InterruptMask &SCU::Probe::GetInterruptMask() const {
    return m_scu.m_intrMask;
}

const InterruptStatus &SCU::Probe::GetInterruptStatus() const {
    return m_scu.m_intrStatus;
}

const uint16 &SCU::Probe::GetABusInterruptsPendingAcknowledge() const {
    return m_scu.m_abusIntrsPendingAck;
}

uint8 SCU::Probe::GetPendingInterruptLevel() const {
    return m_scu.m_pendingIntrLevel;
}

uint8 SCU::Probe::GetPendingInterruptIndex() const {
    return m_scu.m_pendingIntrIndex;
}

// Timers

uint16 SCU::Probe::GetTimer0Counter() const {
    return m_scu.ReadTimer0Counter();
}

void SCU::Probe::SetTimer0Counter(uint16 value) {
    m_scu.WriteTimer0Counter(value);
}

uint16 SCU::Probe::GetTimer0Compare() const {
    return m_scu.ReadTimer0Compare();
}

void SCU::Probe::SetTimer0Compare(uint16 value) const {
    m_scu.WriteTimer0Compare(value);
}

uint16 SCU::Probe::GetTimer1Reload() const {
    return m_scu.ReadTimer1Reload();
}

void SCU::Probe::SetTimer1Reload(uint16 value) {
    m_scu.WriteTimer1Reload(value);
}

bool SCU::Probe::IsTimerEnabled() const {
    return m_scu.m_timerEnable;
}

void SCU::Probe::SetTimerEnabled(bool enabled) {
    m_scu.m_timerEnable = enabled;
}

bool SCU::Probe::GetTimer1Mode() const {
    return m_scu.m_timer1Mode;
}

void SCU::Probe::SetTimer1Mode(bool mode) {
    m_scu.m_timer1Mode = mode;
}

// DMA registers

// TODO: deduplicate code
// - xferCount is a problem, since it depends on the channel
//   - WriteRegLong is optimal; DO NOT CHANGE IT!

uint32 SCU::Probe::GetDMASourceAddress(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].srcAddr;
    } else {
        return 0;
    }
}

void SCU::Probe::SetDMASourceAddress(uint8 channel, uint32 value) {
    if (channel < 3) {
        m_scu.m_dmaChannels[channel].srcAddr = bit::extract<0, 26>(value);
    }
}

uint32 SCU::Probe::GetDMADestinationAddress(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].dstAddr;
    } else {
        return 0;
    }
}

void SCU::Probe::SetDMADestinationAddress(uint8 channel, uint32 value) {
    if (channel < 3) {
        m_scu.m_dmaChannels[channel].dstAddr = bit::extract<0, 26>(value);
    }
}

uint32 SCU::Probe::GetDMATransferCount(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].xferCount;
    } else {
        return 0;
    }
}

void SCU::Probe::SetDMATransferCount(uint8 channel, uint32 value) {
    if (channel == 0) {
        m_scu.m_dmaChannels[channel].xferCount = std::min(value, 0xFFFFFu);
    } else if (channel < 3) {
        m_scu.m_dmaChannels[channel].xferCount = std::min(value, 0xFFFu);
    }
}

uint32 SCU::Probe::GetDMASourceAddressIncrement(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].srcAddrInc;
    } else {
        return 0;
    }
}

void SCU::Probe::SetDMASourceAddressIncrement(uint8 channel, uint32 value) {
    if (channel < 3) {
        // Restrict increment to 0 or 4 bytes
        m_scu.m_dmaChannels[channel].srcAddrInc = value & 4;
    }
}

uint32 SCU::Probe::GetDMADestinationAddressIncrement(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].dstAddrInc;
    } else {
        return 0;
    }
}

void SCU::Probe::SetDMADestinationAddressIncrement(uint8 channel, uint32 value) {
    if (channel < 3) {
        // Restrict increment to 0, 2, 4, 8, 16, 32, 64 or 128 bytes
        m_scu.m_dmaChannels[channel].dstAddrInc = bit::next_power_of_two(value) & 0x7E;
    }
}

bool SCU::Probe::IsDMAUpdateSourceAddress(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].updateSrcAddr;
    } else {
        return false;
    }
}

void SCU::Probe::SetDMAUpdateSourceAddress(uint8 channel, bool value) const {
    if (channel < 3) {
        m_scu.m_dmaChannels[channel].updateSrcAddr = value;
    }
}

bool SCU::Probe::IsDMAUpdateDestinationAddress(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].updateDstAddr;
    } else {
        return false;
    }
}

void SCU::Probe::SetDMAUpdateDestinationAddress(uint8 channel, bool value) const {
    if (channel < 3) {
        m_scu.m_dmaChannels[channel].updateDstAddr = value;
    }
}

bool SCU::Probe::IsDMAEnabled(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].enabled;
    } else {
        return false;
    }
}

void SCU::Probe::SetDMAEnabled(uint8 channel, bool value) const {
    if (channel < 3) {
        m_scu.m_dmaChannels[channel].enabled = value;
    }
}

bool SCU::Probe::IsDMAIndirect(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].indirect;
    } else {
        return false;
    }
}

void SCU::Probe::SetDMAIndirect(uint8 channel, bool value) const {
    if (channel < 3) {
        m_scu.m_dmaChannels[channel].indirect = value;
    }
}

DMATrigger SCU::Probe::GetDMATrigger(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].trigger;
    } else {
        return DMATrigger::VBlankIN;
    }
}

void SCU::Probe::SetDMATrigger(uint8 channel, DMATrigger trigger) {
    if (channel < 3) {
        m_scu.m_dmaChannels[channel].trigger = static_cast<DMATrigger>(bit::extract<0, 2>(static_cast<uint8>(trigger)));
    }
}

// DMA state

bool SCU::Probe::IsDMATransferActive(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].active;
    } else {
        return false;
    }
}

uint32 SCU::Probe::GetCurrentDMASourceAddress(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].currSrcAddr;
    } else {
        return 0;
    }
}

uint32 SCU::Probe::GetCurrentDMADestinationAddress(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].currDstAddr;
    } else {
        return 0;
    }
}

uint32 SCU::Probe::GetCurrentDMATransferCount(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].currXferCount;
    } else {
        return 0;
    }
}

uint32 SCU::Probe::GetCurrentDMASourceAddressIncrement(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].currSrcAddrInc;
    } else {
        return 0;
    }
}

uint32 SCU::Probe::GetCurrentDMADestinationAddressIncrement(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].currDstAddrInc;
    } else {
        return 0;
    }
}

uint32 SCU::Probe::GetCurrentDMAIndirectSourceAddress(uint8 channel) const {
    if (channel < 3) {
        return m_scu.m_dmaChannels[channel].currIndirectSrc;
    } else {
        return 0;
    }
}

} // namespace ymir::scu
