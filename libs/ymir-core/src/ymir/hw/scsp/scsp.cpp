#include <ymir/hw/scsp/scsp.hpp>

#include <ymir/sys/clocks.hpp>

#include <algorithm>
#include <limits>
#include <ostream>

using namespace ymir::m68k;

namespace ymir::scsp {

// -----------------------------------------------------------------------------
// Debugger

template <bool debug>
FORCE_INLINE static void TraceSlotSample(debug::ISCSPTracer *tracer, uint32 index, sint16 output) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->SlotSample(index, output);
        }
    }
}

template <bool debug>
FORCE_INLINE static void TraceKeyOnExecute(debug::ISCSPTracer *tracer, uint32 slotsMask) {
    if constexpr (debug) {
        if (tracer) {
            return tracer->KeyOnExecute(slotsMask);
        }
    }
}

// -----------------------------------------------------------------------------
// Implementation

SCSP::SCSP(core::Scheduler &scheduler, core::Configuration::Audio &config)
    : m_m68k(*this)
    , m_scheduler(scheduler)
    , m_dsp(m_WRAM.data()) {

    // Replicate interpolation mode to avoid an extra dereference in the hot path
    config.interpolation.Observe(m_interpMode);
    config.threadedSCSP.Observe([&](bool value) { EnableThreading(value); });

    m_sampleTickEvent = m_scheduler.RegisterEvent(core::events::SCSPSample, this, OnSampleTickEvent<false>);

    for (uint32 i = 0; i < 32; i++) {
        m_slots[i].index = i;
    }

    Reset(true);
}

void SCSP::Reset(bool hard) {
    m_WRAM.fill(0);

    m_midiInputBuffer.fill(0);
    m_midiInputReadPos = 0;
    m_midiInputWritePos = 0;
    m_midiInputOverflow = false;

    m_midiOutputSize = 0;
    m_expectedOutputPacketSize = 0;

    m_nextMidiTime = 0;

    m_cddaBuffer.fill(0);
    m_cddaReadPos = 0;
    m_cddaWritePos = 0;
    m_cddaReady = false;

    m_m68k.Reset(true);
    m_m68kSpilloverCycles = 0;
    m_m68kEnabled = false;

    m_m68kCycles = 0;
    m_sampleCounter = 0;

    m_lfsr = 1;

    m_currSlot = 0;

    m_out.fill(0);

    if (hard) {
        m_scheduler.ScheduleFromNow(m_sampleTickEvent, kCyclesPerSample);
    }

    for (auto &slot : m_slots) {
        slot.Reset();
    }

    m_kyonex = false;
    m_kyonexExecute = false;

    m_masterVolume = 0;
    m_mem4MB = false;
    m_dac18Bits = false;

    m_monitorSlotCall = 0;

    for (auto &timer : m_timers) {
        timer.Reset();
    }

    m_scuEnabledInterrupts = 0;
    m_scuPendingInterrupts = 0;
    m_m68kEnabledInterrupts = 0;
    m_m68kPendingInterrupts = 0;
    m_m68kInterruptLevels.fill(0);

    m_dmaExec = false;
    m_dmaXferToMem = false;
    m_dmaGate = false;
    m_dmaMemAddress = 0;
    m_dmaRegAddress = 0;
    m_dmaXferLength = 0;

    m_soundStack.fill(0);
    m_soundStackIndex = 0;

    m_dsp.Reset();
}

void SCSP::MapMemory(sys::SH2Bus &bus) {
    static constexpr auto cast = [](void *ctx) -> SCSP & { return *static_cast<SCSP *>(ctx); };

    // WRAM
    bus.MapArray(0x5A0'0000, 0x5A7'FFFF, m_WRAM, true);

    // Unused hole
    bus.MapBoth(
        0x5A8'0000, 0x5AF'FFFF, this,                          //
        [](uint32 address, void *ctx) -> uint8 { return 0; },  //
        [](uint32 address, void *ctx) -> uint16 { return 0; }, //
        [](uint32 address, void *ctx) -> uint32 { return 0; }, //
        [](uint32 address, uint8 value, void *ctx) {},         //
        [](uint32 address, uint16 value, void *ctx) {},        //
        [](uint32 address, uint32 value, void *ctx) {});

    // Registers
    bus.MapNormal(
        0x5B0'0000, 0x5BF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).ReadReg<uint8>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).ReadReg<uint16>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).ReadReg<uint16>(address + 0) << 16u;
            value |= cast(ctx).ReadReg<uint16>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) { cast(ctx).WriteReg<uint8>(address, value); },
        [](uint32 address, uint16 value, void *ctx) { cast(ctx).WriteReg<uint16>(address, value); },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).WriteReg<uint16>(address + 0, value >> 16u);
            cast(ctx).WriteReg<uint16>(address + 2, value >> 0u);
        });

    bus.MapSideEffectFree(
        0x5B0'0000, 0x5BF'FFFF, this,
        [](uint32 address, void *ctx) -> uint8 { return cast(ctx).ReadReg<uint8, SCSPAccessType::Debug>(address); },
        [](uint32 address, void *ctx) -> uint16 { return cast(ctx).ReadReg<uint16, SCSPAccessType::Debug>(address); },
        [](uint32 address, void *ctx) -> uint32 {
            uint32 value = cast(ctx).ReadReg<uint16, SCSPAccessType::Debug>(address + 0) << 16u;
            value |= cast(ctx).ReadReg<uint16, SCSPAccessType::Debug>(address + 2) << 0u;
            return value;
        },
        [](uint32 address, uint8 value, void *ctx) {
            cast(ctx).WriteReg<uint8, SCSPAccessType::Debug>(address, value);
        },
        [](uint32 address, uint16 value, void *ctx) {
            cast(ctx).WriteReg<uint16, SCSPAccessType::Debug>(address, value);
        },
        [](uint32 address, uint32 value, void *ctx) {
            cast(ctx).WriteReg<uint16, SCSPAccessType::Debug>(address + 0, value >> 16u);
            cast(ctx).WriteReg<uint16, SCSPAccessType::Debug>(address + 2, value >> 0u);
        });

    bus.SetAccessCycles(0x5A0'0000, 0x5BF'FFFF, 48, 48);
}

void SCSP::UpdateClockRatios(const sys::ClockRatios &clockRatios) {
    m_scheduler.SetEventCountFactor(m_sampleTickEvent, clockRatios.SCSPNum, clockRatios.SCSPDen);
}

void SCSP::SetDebugTracing(bool enable) {
    if (m_debugTracing != enable) {
        m_debugTracing = enable;
        if (enable) {
            UpdateStepFunction();
        }
    }
}

uint32 SCSP::ReceiveCDDA(std::span<uint8, 2352> data) {
    std::copy_n(data.begin(), 2352, m_cddaBuffer.begin() + m_cddaWritePos);
    m_cddaWritePos = (m_cddaWritePos + 2352) % m_cddaBuffer.size();
    sint32 len = static_cast<sint32>(m_cddaWritePos) - m_cddaReadPos;
    if (len < 0) {
        len += m_cddaBuffer.size();
    }
    if (len >= 2352 * 4) {
        m_cddaReady = true;
    }
    return len * 3 / m_cddaBuffer.size();
}

void SCSP::ReceiveMidiInput(MidiMessage &msg) {
    // if we reset, and this is the first message received, ignore delta time & play now
    if (m_nextMidiTime != 0) {
        m_nextMidiTime += (uint64)(msg.deltaTime * kAudioFreq);
    }

    // if scheduled time falls behind real time, compensate
    if (m_nextMidiTime < m_sampleCounter) {
        devlog::debug<grp::midi>("Scheduled time fell behind real time");
        m_nextMidiTime = m_sampleCounter + kMidiAheadTime;
    }

    // if scheduled time is too far ahead of real time, compensate
    if (m_nextMidiTime >= m_sampleCounter + kMaxMidiScheduleTime) {
        devlog::debug<grp::midi>("Scheduled time ahead of real time");
        m_nextMidiTime = m_sampleCounter + kMidiAheadTime;
    }

    devlog::trace<grp::midi>("Received midi payload, scheduled for {} (bytes: {})", m_nextMidiTime, msg.payload.size());

    m_midiInputQueue.push(QueuedMidiMessage(m_nextMidiTime, std::move(msg.payload)));
}

void SCSP::ProcessMidiInputQueue() {
    // TODO: I believe MIDI stuff is *supposed* to trigger interrupts...
    // however there are no commercial games relying on this behavior, so it should be fine for now.

    while (!m_midiInputQueue.empty()) {
        auto &msg = m_midiInputQueue.front();
        if (msg.scheduleTime > m_sampleCounter) {
            break;
        }

        // TODO: is there any way to clear overflow beyond a reset?
        if (!m_midiInputOverflow) {
            devlog::trace<grp::midi>("Adding MIDI message to buffer at {} (bytes: {})", m_sampleCounter,
                                     msg.payload.size());

            for (const auto data : msg.payload) {
                m_midiInputBuffer[m_midiInputWritePos] = data;
                m_midiInputWritePos = (m_midiInputWritePos + 1) % m_midiInputBuffer.size();

                if (m_midiInputWritePos == m_midiInputReadPos) {
                    m_midiInputOverflow = true;
                    devlog::error<grp::midi>("MIDI buffer overflowed");
                    break;
                }
            }
        }

        m_midiInputQueue.pop();
    }
}

void SCSP::FlushMidiOutput(bool endPacket) {
    m_cbSendMidiOutputMessage(std::span<uint8>(m_midiOutputBuffer).subspan(0, m_midiOutputSize));
    m_midiOutputSize = 0;
    if (endPacket) {
        m_expectedOutputPacketSize = 0;
    }
}

void SCSP::DumpWRAM(std::ostream &out) const {
    out.write((const char *)m_WRAM.data(), m_WRAM.size());
}

void SCSP::DumpDSP_MPRO(std::ostream &out) const {
    for (DSPInstr instr : m_dsp.program) {
        uint64 value = bit::big_endian_swap(instr.u64);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCSP::DumpDSP_TEMP(std::ostream &out) const {
    for (uint32 value : m_dsp.tempMem) {
        value = bit::big_endian_swap(value);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCSP::DumpDSP_MEMS(std::ostream &out) const {
    for (uint32 value : m_dsp.soundMem) {
        value = bit::big_endian_swap(value);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCSP::DumpDSP_COEF(std::ostream &out) const {
    for (uint16 value : m_dsp.coeffs) {
        value = bit::big_endian_swap(value);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCSP::DumpDSP_MADRS(std::ostream &out) const {
    for (uint16 value : m_dsp.addrs) {
        value = bit::big_endian_swap(value);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCSP::DumpDSP_MIXS(std::ostream &out) const {
    for (uint32 value : m_dsp.mixStack) {
        value = bit::big_endian_swap(value);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCSP::DumpDSP_EFREG(std::ostream &out) const {
    for (uint16 value : m_dsp.effectOut) {
        value = bit::big_endian_swap(value);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCSP::DumpDSP_EXTS(std::ostream &out) const {
    for (uint16 value : m_dsp.audioInOut) {
        value = bit::big_endian_swap(value);
        out.write((const char *)&value, sizeof(value));
    }
}

void SCSP::DumpDSPRegs(std::ostream &out) const {
    m_dsp.DumpRegs(out);
}

void SCSP::SetCPUEnabled(bool enabled) {
    if (m_m68kEnabled != enabled) {
        devlog::info<grp::base>("MC68EC00 processor {}", (enabled ? "enabled" : "disabled"));
        if (enabled) {
            m_m68k.Reset(true); // false? does it matter?
            m_m68kSpilloverCycles = 0;
        }
        m_m68kEnabled = enabled;
    }
}

void SCSP::SaveState(state::SCSPState &state) const {
    state.WRAM = m_WRAM;
    state.cddaBuffer = m_cddaBuffer;
    state.cddaReadPos = m_cddaReadPos;
    state.cddaWritePos = m_cddaWritePos;
    state.cddaReady = m_cddaReady;

    m_m68k.SaveState(state.m68k);
    state.m68kSpilloverCycles = m_m68kSpilloverCycles;
    state.m68kEnabled = m_m68kEnabled;

    for (size_t i = 0; i < 32; i++) {
        m_slots[i].SaveState(state.slots[i]);
    }

    state.KYONEX = m_kyonex;
    state.KYONEXExec = m_kyonexExecute;

    state.MVOL = m_masterVolume;
    state.DAC18B = m_dac18Bits;
    state.MEM4MB = m_mem4MB;
    state.MSLC = m_monitorSlotCall;

    for (size_t i = 0; i < 3; i++) {
        m_timers[i].SaveState(state.timers[i]);
    }

    state.MCIEB = m_scuEnabledInterrupts;
    state.MCIPD = m_scuPendingInterrupts;
    state.SCIEB = m_m68kEnabledInterrupts;
    state.SCIPD = m_m68kPendingInterrupts;
    state.SCILV = m_m68kInterruptLevels;
    state.reuseSCILV = false;

    state.DEXE = m_dmaExec;
    state.DDIR = m_dmaXferToMem;
    state.DGATE = m_dmaGate;
    state.DMEA = m_dmaMemAddress;
    state.DRGA = m_dmaRegAddress;
    state.DTLG = m_dmaXferLength;

    state.SOUS = m_soundStack;
    state.soundStackIndex = m_soundStackIndex;

    m_dsp.SaveState(state.dsp);

    state.m68kCycles = m_m68kCycles;
    state.sampleCounter = m_sampleCounter;

    state.lfsr = m_lfsr;

    state.currSlot = m_currSlot;

    state.out = m_out;

    std::copy(m_midiInputBuffer.begin(), m_midiInputBuffer.end(), state.midiInputBuffer.begin());
    state.midiInputReadPos = m_midiInputReadPos;
    state.midiInputWritePos = m_midiInputWritePos;
    state.midiInputOverflow = m_midiInputOverflow;

    std::copy(m_midiOutputBuffer.begin(), m_midiOutputBuffer.end(), state.midiOutputBuffer.begin());
    state.midiOutputSize = m_midiOutputSize;
    state.expectedOutputPacketSize = m_expectedOutputPacketSize;
}

bool SCSP::ValidateState(const state::SCSPState &state) const {
    if (state.cddaReadPos >= m_cddaBuffer.size()) {
        return false;
    }
    if (state.cddaWritePos >= m_cddaBuffer.size()) {
        return false;
    }
    if (state.soundStackIndex >= m_soundStack.size()) {
        return false;
    }
    for (size_t i = 0; i < 32; i++) {
        if (!m_slots[i].ValidateState(state.slots[i])) {
            return false;
        }
    }
    for (size_t i = 0; i < 3; i++) {
        if (!m_timers[i].ValidateState(state.timers[i])) {
            return false;
        }
    }
    if (state.currSlot > 31) {
        return false;
    }
    if (!m_dsp.ValidateState(state.dsp)) {
        return false;
    }
    if (state.midiInputReadPos >= m_midiInputBuffer.size()) {
        return false;
    }
    if (state.midiInputWritePos >= m_midiInputBuffer.size()) {
        return false;
    }
    if (state.midiOutputSize >= m_midiOutputBuffer.size()) {
        return false;
    }
    if (state.expectedOutputPacketSize >= m_midiOutputBuffer.size() || state.expectedOutputPacketSize < -1) {
        return false;
    }

    return true;
}

void SCSP::LoadState(const state::SCSPState &state) {
    m_WRAM = state.WRAM;
    m_cddaBuffer = state.cddaBuffer;
    m_cddaReadPos = state.cddaReadPos % m_cddaBuffer.size();
    m_cddaWritePos = state.cddaWritePos % m_cddaBuffer.size();
    m_cddaReady = state.cddaReady;

    m_m68k.LoadState(state.m68k);
    m_m68kSpilloverCycles = state.m68kSpilloverCycles;
    m_m68kEnabled = state.m68kEnabled;

    for (size_t i = 0; i < 32; i++) {
        m_slots[i].LoadState(state.slots[i]);
    }

    m_kyonex = state.KYONEX;
    m_kyonexExecute = state.KYONEXExec;

    m_masterVolume = state.MVOL & 0xF;
    m_dac18Bits = state.DAC18B;
    m_mem4MB = state.MEM4MB;
    m_monitorSlotCall = state.MSLC & 0x1F;

    for (size_t i = 0; i < 3; i++) {
        m_timers[i].LoadState(state.timers[i]);
    }

    m_scuEnabledInterrupts = state.MCIEB & 0x7FF;
    m_scuPendingInterrupts = state.MCIPD & 0x7FF;
    m_m68kEnabledInterrupts = state.SCIEB & 0x7FF;
    m_m68kPendingInterrupts = state.SCIPD & 0x7FF;
    if (!state.reuseSCILV) {
        m_m68kInterruptLevels = state.SCILV;
    }

    m_dmaExec = state.DEXE;
    m_dmaXferToMem = state.DDIR;
    m_dmaGate = state.DGATE;
    m_dmaMemAddress = state.DMEA & 0xFFFFE;
    m_dmaRegAddress = state.DRGA & 0xFFE;
    m_dmaXferLength = state.DTLG & 0xFFE;

    m_soundStack = state.SOUS;
    m_soundStackIndex = state.soundStackIndex % m_soundStack.size();

    m_dsp.LoadState(state.dsp);

    m_m68kCycles = state.m68kCycles;
    m_sampleCounter = state.sampleCounter;

    m_lfsr = state.lfsr;

    m_currSlot = state.currSlot;

    m_out = state.out;

    std::copy(state.midiInputBuffer.begin(), state.midiInputBuffer.end(), m_midiInputBuffer.begin());
    m_midiInputReadPos = state.midiInputReadPos;
    m_midiInputWritePos = state.midiInputWritePos;
    m_midiInputOverflow = state.midiInputOverflow;

    std::copy(state.midiOutputBuffer.begin(), state.midiOutputBuffer.end(), m_midiOutputBuffer.begin());
    m_midiOutputSize = state.midiOutputSize;
    m_expectedOutputPacketSize = state.expectedOutputPacketSize;

    // Realign the tick event if the save state was using a more granular slot step
    if (m_stepGranularity <= 5u && (m_currSlot & ((1u << m_stepGranularity) - 1u)) != 0) {
        UpdateStepFunction();
    }
}

template <uint32 stepShift, bool debug>
void SCSP::OnSlotTickEvent(core::EventContext &eventContext, void *userContext) {
    auto &scsp = *static_cast<SCSP *>(userContext);
    scsp.TickSlots<stepShift, debug>();
    eventContext.Reschedule(kCyclesPerSlot << stepShift);
}

template <bool debug>
void SCSP::OnSampleTickEvent(core::EventContext &eventContext, void *userContext) {
    auto &scsp = *static_cast<SCSP *>(userContext);
    scsp.TickSample<debug>();
    eventContext.Reschedule(kCyclesPerSample);
}

template <uint32 newStepShift, bool debug>
void SCSP::OnTransitionalTickEvent(core::EventContext &eventContext, void *userContext) {
    static_assert(newStepShift <= 5u, "newStepShift must be at most 5 (32 slots)");

    static constexpr uint32 kSlotIndexMask = (1u << newStepShift) - 1u;

    // Check if the slot counter is aligned
    auto &scsp = *static_cast<SCSP *>(userContext);
    if ((scsp.m_currSlot & kSlotIndexMask) == 0) {
        // Aligned; switch to the bigger tick event
        if constexpr (newStepShift == 5u) {
            scsp.m_scheduler.SetEventCallback(scsp.m_sampleTickEvent, &scsp, OnSampleTickEvent<debug>);
            eventContext.Reschedule(kCyclesPerSample);
        } else {
            scsp.m_scheduler.SetEventCallback(scsp.m_sampleTickEvent, &scsp, OnSlotTickEvent<newStepShift, debug>);
            eventContext.Reschedule(kCyclesPerSlot << newStepShift);
        }
    } else {
        // Not yet aligned; continue ticking slots one by one
        scsp.TickSlots<0, debug>();
        eventContext.Reschedule(kCyclesPerSlot);
    }
}

void SCSP::SetStepGranularity(uint32 granularity) {
    granularity = 5u - std::min(granularity, 5u);
    if (m_stepGranularity != granularity) {
        // Switch callbacks
        if (granularity < m_stepGranularity) {
            // Going from larger to smaller steps is relatively easy. The tricky bit is rescheduling the event.
            // It may be pushed into the past, in which case the event will trigger multiple times to catch up.
            const uint64 target = m_scheduler.GetScheduleTarget(m_sampleTickEvent);
            const uint64 newTarget = target - (kCyclesPerSlot << m_stepGranularity) + (kCyclesPerSlot << granularity);
            m_scheduler.ScheduleAt(m_sampleTickEvent, newTarget);

            core::Scheduler::EventCallback callback;
            switch (granularity) {
            case 0u: callback = GetSlotTickEvent<0u>(); break;
            case 1u: callback = (m_currSlot & 0x1) ? GetTransitionalTickEvent<1u>() : GetSlotTickEvent<1u>(); break;
            case 2u: callback = (m_currSlot & 0x3) ? GetTransitionalTickEvent<2u>() : GetSlotTickEvent<2u>(); break;
            case 3u: callback = (m_currSlot & 0x7) ? GetTransitionalTickEvent<3u>() : GetSlotTickEvent<3u>(); break;
            case 4u: callback = (m_currSlot & 0xF) ? GetTransitionalTickEvent<4u>() : GetSlotTickEvent<4u>(); break;
            default: util::unreachable();
            }
            m_scheduler.SetEventCallback(m_sampleTickEvent, this, callback);
        } else {
            // Going from smaller to larger steps requires the slot counter to be realigned.
            // Luckily, we don't need to reschedule it.
            switch (granularity) {
            case 1u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<1u>()); break;
            case 2u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<2u>()); break;
            case 3u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<3u>()); break;
            case 4u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<4u>()); break;
            case 5u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<5u>()); break;
            default: util::unreachable();
            }
        }
        m_stepGranularity = granularity;
    }
}

void SCSP::UpdateStepFunction() {
    switch (m_stepGranularity) {
    case 0u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetSlotTickEvent<0u>()); break;
    case 1u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<1u>()); break;
    case 2u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<2u>()); break;
    case 3u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<3u>()); break;
    case 4u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<4u>()); break;
    case 5u: m_scheduler.SetEventCallback(m_sampleTickEvent, this, GetTransitionalTickEvent<5u>()); break;
    default: util::unreachable();
    }
}

void SCSP::EnableThreading(bool enable) {
    if (enable) {
        // TODO: implement
        devlog::debug<grp::base>("Threaded SCSP is unimplemented");
    } else {
        devlog::debug<grp::base>("Running SCSP on emulator thread");
    }
}

FORCE_INLINE void SCSP::SetInterrupt(uint16 intr, bool level) {
    m_m68kPendingInterrupts &= ~(1 << intr);
    m_m68kPendingInterrupts |= level << intr;

    const uint16 prev = m_scuPendingInterrupts;
    m_scuPendingInterrupts &= ~(1 << intr);
    m_scuPendingInterrupts |= level << intr;

    if ((prev ^ m_scuPendingInterrupts) & m_scuEnabledInterrupts & (1 << intr)) {
        UpdateSCUInterrupts();
    }
}

void SCSP::UpdateM68KInterrupts() {
    const uint16 baseMask = m_m68kPendingInterrupts & m_m68kEnabledInterrupts;
    uint8 mask = baseMask | ((baseMask & ~0xFF) ? 0x80 : 0x00);
    // NOTE: interrupts 7-10 share the same level

    // Check all active interrupts in parallel and select maximum level among them.
    //
    // The naive approach is to iterate over all 8 interrupts, skip unselected interrupts, and update the level if the
    // configured interrupt level is greater than the one found so far. This takes 8 iterations, each one doing several
    // operations (including comparisons and bit manipulation).
    //
    // Luckily, we can use bit manipulation to our advantage. Due to the way the SCILV registers are naturally arranged,
    // we have 3 values containing the bits at positions 2, 1 and 0 of each interrupt. We can make all 8 comparisons in
    // parallel in only 3 iterations by taking advantage of bitwise operations.
    //
    // Suppose we want to pick the largest of four binary numbers: (x) 101, (y) 011, (z) 110, (w) 001.
    // First, let's rearrange the bits to group bits from each position into one word each:
    //          xyzw
    //   bit 2  1010
    //   bit 1  0110
    //   bit 0  1101
    //
    // Now we can take advantage of the fact that a binary digit can only be 0 or 1, meaning that if any bit is 1 in the
    // most significant bit of a number, we know that any other number that has a digit 0 in the same position cannot be
    // larger. We can then exclude these numbers from the search with a simple bitwise AND.
    //
    // Let's go through one iteration of the algorithm. We've already set up the table in a convenient manner to allow
    // parallel calculations. Let's create a bitmask to indicate which numbers we're still checking:
    //
    //   mask   1111
    //
    // We can AND the mask with the most significant bit we're checking to see if any number has that bit set:
    //
    //   bit2   mask
    //   1010 & 1111 = 1010
    //
    // The result is non-zero, meaning that we have at least one number with this bit set. Conveniently, the numbers
    // that don't have the bit set also happen to have cleared their corresponding bits in the result.
    // Because the result is non-zero, the output bit for position 2 is set to 1.
    //
    //   output = 1..
    //
    // We still have two bits to go. Copy the result to the mask and continue checking the next most significant bit:
    //
    //   bit1   mask
    //   0110 & 1010 = 0010
    //
    // Once again, the result is 1; write that to the output at position 1, update the mask and check the final bit:
    //
    //   output = 11.
    //
    //   bit0   mask
    //   1101 & 0010 = 0000
    //
    // Now the result is zero, so the final output bit is 0.
    //
    //   output = 110
    //
    // This indeed matches the biggest number out of the four: (z) 110.

    uint8 level = 0;
    if (m_m68kInterruptLevels[2] & mask) {
        level |= 4;
        mask &= m_m68kInterruptLevels[2];
    }
    if (m_m68kInterruptLevels[1] & mask) {
        level |= 2;
        mask &= m_m68kInterruptLevels[1];
    }
    if (m_m68kInterruptLevels[0] & mask) {
        level |= 1;
    }

    m_m68k.SetExternalInterruptLevel(level);
}

void SCSP::ExecuteDMA() {
    while (m_dmaExec) {
        if (m_dmaXferToMem) {
            const uint16 value = ReadReg<uint16>(m_dmaRegAddress);
            devlog::trace<grp::dma>("Register {:03X} -> Memory {:06X} = {:04X}", m_dmaRegAddress, m_dmaMemAddress,
                                    value);
            WriteWRAM<uint16>(m_dmaMemAddress, m_dmaGate ? 0u : value);
        } else {
            const uint16 value = ReadWRAM<uint16>(m_dmaMemAddress);
            devlog::trace<grp::dma>("Memory {:06X} -> Register {:03X} = {:04X}", m_dmaMemAddress, m_dmaRegAddress,
                                    value);
            WriteReg<uint16>(m_dmaRegAddress, m_dmaGate ? 0u : value);
        }
        m_dmaMemAddress = (m_dmaMemAddress + sizeof(uint16)) & 0x7FFFE;
        m_dmaRegAddress = (m_dmaRegAddress + sizeof(uint16)) & 0xFFE;
        m_dmaXferLength -= sizeof(uint16);
        if (m_dmaXferLength == 0) {
            m_dmaExec = false;
            SetInterrupt(kIntrDMATransferEnd, true);

            // Send interrupt signals
            UpdateM68KInterrupts();
            UpdateSCUInterrupts();
        }
    }
}

template <uint32 stepShift, bool debug>
FORCE_INLINE void SCSP::TickSlots() {
    RunM68K(kM68KCyclesPerSlot << stepShift);
    ProcessMidiInputQueue();
    StepSlots<stepShift, debug>();
}

template <bool debug>
FORCE_INLINE void SCSP::TickSample() {
    RunM68K(kM68KCyclesPerSample);
    ProcessMidiInputQueue();
    StepSample<debug>();
}

FORCE_INLINE void SCSP::RunM68K(uint64 cycles) {
    if (m_m68kEnabled) {
        uint64 cy = m_m68kSpilloverCycles;
        while (cy < cycles) {
            cy += m_m68k.Step();
        }
        m_m68kSpilloverCycles = cy - cycles;
    }
}

template <uint32 stepShift, bool debug>
FORCE_INLINE void SCSP::StepSlots() {
    if constexpr (stepShift == 5u) {
        ProcessSlots<debug>(m_currSlot);
        ++m_currSlot;
    } else {
        static constexpr uint32 kNumSlots = 1u << stepShift;
        static constexpr uint32 kSlotMask = kNumSlots - 1u;
        assert((m_currSlot & kSlotMask) == 0);
        for (uint32 i = 0; i < kNumSlots; ++i) {
            ProcessSlots<debug>(m_currSlot + i);
        }
        m_currSlot += kNumSlots;
    }

    if (m_currSlot == 32) {
        m_currSlot = 0;
        IncrementSampleCounter();
    }
}

template <bool debug>
FORCE_INLINE void SCSP::StepSample() {
    assert(m_currSlot == 0);
    for (uint32 i = 0; i < 32; ++i) {
        ProcessSlots<debug>(i);
    }
    IncrementSampleCounter();
}

FORCE_INLINE void SCSP::UpdateTimers() {
    for (int i = 0; i < 3; i++) {
        auto &timer = m_timers[i];
        const bool trigger = (m_sampleCounter & timer.incrementMask) == 0;
        if (trigger && timer.Tick()) {
            SetInterrupt(kIntrTimerA + i, true);
        }
    }
}

template <bool debug>
FORCE_INLINE void SCSP::ProcessSlots(uint32 i) {
    const uint32 op1SlotIndex = i;
    const uint32 op2SlotIndex = (i - 1u) & 31;
    const uint32 op3SlotIndex = (i - 2u) & 31;
    const uint32 op4SlotIndex = (i - 3u) & 31;
    const uint32 op5SlotIndex = (i - 4u) & 31;
    const uint32 op6SlotIndex = (i - 5u) & 31;
    const uint32 op7SlotIndex = (i - 6u) & 31;

    auto &op1Slot = m_slots[op1SlotIndex];
    auto &op2Slot = m_slots[op2SlotIndex];
    auto &op3Slot = m_slots[op3SlotIndex];
    auto &op4Slot = m_slots[op4SlotIndex];
    auto &op5Slot = m_slots[op5SlotIndex];
    auto &op6Slot = m_slots[op6SlotIndex];
    auto &op7Slot = m_slots[op7SlotIndex];

    // Cycles 0,1
    SlotProcessStep7_1(op7Slot);
    m_dsp.Step();

    // Cycles 2,3
    if (op7Slot.inputMixingLevel > 0) {
        const sint32 mixsOutput = (op7Slot.output << 4) >> (op7Slot.inputMixingLevel ^ 7);
        m_dsp.MIXSSlotWrite(op7Slot.inputSelect, mixsOutput);
    } else {
        m_dsp.MIXSSlotZero(op7Slot.inputSelect);
    }

    SlotProcessStep2_2(op2Slot);
    SlotProcessStep3_2(op3Slot);
    SlotProcessStep4_2(op4Slot);
    m_dsp.Step();

    // Cycles 4,5
    SlotProcessStep2_3(op2Slot);
    m_dsp.Step();

    // Cycles 6,7
    SlotProcessStep1_4<debug>(op1Slot);
    SlotProcessStep2_4(op2Slot);
    SlotProcessStep3_4(op3Slot);
    SlotProcessStep4_4(op4Slot);
    SlotProcessStep5_4(op5Slot);
    SlotProcessStep6_4(op6Slot);
    m_dsp.Step();

    // Accumulate direct send output
    AddOutput(op7Slot.output, op7Slot.directSendLevel, op7Slot.directPan);

    TraceSlotSample<debug>(m_tracer, op7SlotIndex, op7Slot.output);

    if (op7SlotIndex < 16) {
        // Accumulate EFREG into final output
        AddOutput(m_dsp.effectOut[op7SlotIndex], op7Slot.effectSendLevel, op7Slot.effectPan);
    } else if (op7SlotIndex < 18) {
        // Accumulate EXTS into final output
        AddOutput(m_dsp.audioInOut[op7SlotIndex & 1], op7Slot.effectSendLevel, op7Slot.effectPan);
    } else if (op7SlotIndex == 31) {
        // Finish sample cycle

        // Master volume attenuates sound in steps of 3 dB, or 0.5 bits per step
        auto applyMasterVolume = [&](sint32 out) {
            if (m_masterVolume == 0) {
                return 0;
            }
            const uint32 masterVolume = m_masterVolume ^ 0xF;
            out <<= 8;
            out >>= masterVolume >> 1u;
            if (masterVolume & 1) {
                out -= out >> 2;
            }
            return out >> 8;
        };

        // Apply master volume
        m_out[0] = applyMasterVolume(m_out[0]);
        m_out[1] = applyMasterVolume(m_out[1]);

        // Clamp to signed 16-bit range
        static constexpr sint32 outMin = std::numeric_limits<sint16>::min();
        static constexpr sint32 outMax = std::numeric_limits<sint16>::max();
        m_out[0] = std::clamp<sint32>(m_out[0], outMin, outMax);
        m_out[1] = std::clamp<sint32>(m_out[1], outMin, outMax);

        // "Expand" to 18 bits if DAC18B is enabled
        if (m_dac18Bits) {
            m_out[0] = static_cast<uint32>(m_out[0]) << 2u;
            m_out[1] = static_cast<uint32>(m_out[1]) << 2u;
        }

        // Write to output and reset
        m_cbOutputSample(m_out[0], m_out[1]);
        m_out.fill(0);

        // Copy CDDA data to DSP EXTS (0=left, 1=right)
        if (m_cddaReady && m_cddaReadPos != m_cddaWritePos) {
            m_dsp.audioInOut[0] = util::ReadLE<uint16>(&m_cddaBuffer[m_cddaReadPos + 0]);
            m_dsp.audioInOut[1] = util::ReadLE<uint16>(&m_cddaBuffer[m_cddaReadPos + 2]);
            m_cddaReadPos = (m_cddaReadPos + 2 * sizeof(uint16)) % m_cddaBuffer.size();
        } else {
            // Buffer underrun
            m_dsp.audioInOut[0] = 0;
            m_dsp.audioInOut[1] = 0;
            m_cddaReady = false;
        }
    }

    m_soundStackIndex = (m_soundStackIndex + 1) & 63;
}

FORCE_INLINE void SCSP::IncrementSampleCounter() {
    ++m_sampleCounter;
    UpdateTimers();
    SetInterrupt(kIntrSample, true);
    UpdateM68KInterrupts();
}

FORCE_INLINE void SCSP::AddOutput(sint32 output, uint8 sendLevel, uint8 pan) {
    if (sendLevel == 0) { // = -infinity dB
        return;
    }

    // Introduce enough fractional bits to accurately compute the combined effects of SDL and PAN.
    // SDL shifts out up to 6 bits (=0x1).
    // PAN shifts out up to 7 bits (=0xE), or 6 then 8 bits (=0xD).
    // Maximum is 6 from SDL + 8 from PAN = 14 bits.
    output <<= 14;

    // Send level attenuates sound in steps of 6 dB, matching one bit per step
    output >>= sendLevel ^ 7u;

    // Pan attenuates sound in one of the channels in steps of 3 dB, or 0.5 bit per step
    const uint8 panAmount = pan & 0xF;
    sint32 panOut;
    if (panAmount == 0xF) { // = -infinity dB
        panOut = 0;
    } else {
        panOut = output >> (panAmount >> 1u);
        if (panAmount & 1) {
            panOut -= panOut >> 2;
        }
    }

    // Apply panning to one of the channels
    const bool panChanSel = (pan & 0x10) != 0;
    m_out[0] += (panChanSel ? output : panOut) >> 14;
    m_out[1] += (panChanSel ? panOut : output) >> 14;
}

template <bool debug>
FORCE_INLINE void SCSP::SlotProcessStep1_4(Slot &slot) {
    m_lfsr = (m_lfsr >> 1u) | (((m_lfsr >> 5u) ^ m_lfsr) & 1u) << 16u;

    if (slot.index == 0) {
        m_kyonexExecute = m_kyonex;
        m_kyonex = false;
    }
    if (m_kyonexExecute && slot.TriggerKey()) {
        static constexpr const char *loopNames[] = {"->|", ">->", "<-<", ">-<"};
        devlog::trace<grp::kyonex>(
            "Slot {:02d} key {} {:2d}-bit addr={:05X} loop={:04X}-{:04X} {} OCT={:02d} FNS={:03X} KRS={:X} "
            "EG {:d} {:02d} {:02d} {:02d} {:02d} DL={:03X} EGHOLD={} LPSLNK={} mod X={:02X} Y={:02X} lv={:X} "
            "ALFOWS={:X} ALFOS={:X}",
            slot.index, (slot.keyOnBit ? " ON" : "OFF"), (slot.pcm8Bit ? 8 : 16), slot.startAddress,
            slot.loopStartAddress, slot.loopEndAddress, loopNames[static_cast<uint32>(slot.loopControl)], slot.octave,
            slot.freqNumSwitch, slot.keyRateScaling, static_cast<uint8>(slot.egState), slot.attackRate, slot.decay1Rate,
            slot.decay2Rate, slot.releaseRate, slot.decayLevel, static_cast<uint8>(slot.egHold),
            static_cast<uint8>(slot.loopStartLink), slot.modXSelect, slot.modYSelect, slot.modLevel,
            static_cast<uint8>(slot.ampLFOWaveform), slot.ampLFOSens);
    }
    if constexpr (debug) {
        if (slot.index == 31 && m_kyonexExecute) {
            uint32 kyonbMask = 0;
            for (uint32 i = 0; i < 32; ++i) {
                kyonbMask |= m_slots[i].keyOnBit << i;
            }
            TraceKeyOnExecute<debug>(m_tracer, kyonbMask);
        }
    }

    if constexpr (devlog::debug_enabled<grp::kyonex>) {
        if (slot.index == 31 && m_kyonexExecute) {
            static char out[32];
            for (auto &slot : m_slots) {
                out[slot.index] = slot.keyOnBit ? '+' : '_';
            }
            devlog::debug<grp::kyonex>("{}", std::string_view(out, 32));
        }
    }

    // Pitch LFO waveform tables
    static constexpr auto sawTable = [] {
        std::array<sint8, 256> arr{};
        for (uint32 i = 0; i < 256; i++) {
            arr[i] = static_cast<sint8>(i) & ~1;
        }
        return arr;
    }();
    static constexpr auto squareTable = [] {
        std::array<sint8, 256> arr{};
        for (uint32 i = 0; i < 256; i++) {
            arr[i] = i < 128 ? 126 : -128;
        }
        return arr;
    }();
    static constexpr auto triangleTable = [] {
        std::array<sint8, 256> arr{};
        for (uint32 i = 0; i < 128; i++) {
            const uint8 il = i - 64;
            const uint8 ir = 255 - i - 64;
            arr[il] = arr[ir] = i * 2 - 128;
        }
        return arr;
    }();

    // Compute pitch LFO
    sint32 pitchLFO = 0;
    if (slot.pitchLFOSens != 0) {
        using enum Slot::Waveform;
        switch (slot.pitchLFOWaveform) {
        case Saw: pitchLFO = sawTable[slot.lfoStep]; break;
        case Square: pitchLFO = squareTable[slot.lfoStep]; break;
        case Triangle: pitchLFO = triangleTable[slot.lfoStep]; break;
        case Noise: pitchLFO = static_cast<sint8>((m_lfsr ^ 0x80) & ~1); break;
        }
        pitchLFO >>= 7 - static_cast<sint32>(slot.pitchLFOSens);
        pitchLFO *= slot.freqNumSwitch >> 4u; // NOTE: FNS already has ^ 0x400
        pitchLFO >>= 6;
    }

    slot.IncrementLFO();
    slot.IncrementPhase(pitchLFO);
}

FORCE_INLINE void SCSP::SlotProcessStep2_2(Slot &slot) {
    if (slot.modLevel >= 5) {
        slot.modXSample = m_soundStack[(m_soundStackIndex - 1 + slot.modXSelect) & 63];
    }
}

FORCE_INLINE void SCSP::SlotProcessStep2_3(Slot &slot) {
    if (slot.modLevel >= 5) {
        slot.modYSample = m_soundStack[(m_soundStackIndex - 1 + slot.modYSelect) & 63];
    }
}

FORCE_INLINE void SCSP::SlotProcessStep2_4(Slot &slot) {
    if (slot.soundSource == Slot::SoundSource::SoundRAM && !slot.active) {
        return;
    }

    slot.IncrementSampleCounter();
}

FORCE_INLINE void SCSP::SlotProcessStep3_2(Slot &slot) {
    if (slot.soundSource == Slot::SoundSource::SoundRAM && slot.active) {
        if (slot.modLevel >= 5) {
            const sint32 zd = (slot.modXSample + slot.modYSample) & 0x3FFFFE;
            slot.modulation = bit::sign_extend<16>((zd << 5) >> (16 - slot.modLevel));
        } else {
            slot.modulation = 0;
        }

        const uint16 currSmp = slot.reverse ? ~slot.currSample : slot.currSample;

        const sint32 thisSlotPhase = slot.reverse ? ~slot.currPhase : slot.currPhase;
        const sint32 thisSlotModPhase = ((thisSlotPhase >> 8) & 0x3F) + ((slot.modulation & 0x1F) << 1);

        const sint32 modInt = bit::sign_extend<11>(slot.modulation >> 5);

        const sint32 addrInc1 = bit::sign_extend<17>((currSmp + modInt + (thisSlotModPhase >> 6)) & slot.mask);

        if (slot.pcm8Bit) {
            const uint32 address1 = slot.startAddress + addrInc1 * sizeof(uint8);
            slot.sample1 = static_cast<sint8>(ReadWRAM<uint8>(address1)) << 8;
        } else {
            const uint32 address1 = (slot.startAddress & ~1) + addrInc1 * sizeof(uint16);
            slot.sample1 = static_cast<sint16>(ReadWRAM<uint16>(address1));
        }
    }
}

FORCE_INLINE void SCSP::SlotProcessStep3_4(Slot &slot) {
    if (slot.soundSource == Slot::SoundSource::SoundRAM && !slot.active) {
        return;
    }

    switch (slot.soundSource) {
    case Slot::SoundSource::SoundRAM: //
    {
        auto &nextSlot = m_slots[(slot.index + 1) & 0x1F];

        const uint16 currSmp = slot.reverse ? ~slot.currSample : slot.currSample;
        const uint16 nextSmp = currSmp + 1;

        const sint32 nextSlotPhase = nextSlot.reverse ? ~nextSlot.currPhase : nextSlot.currPhase;
        const sint32 nextSlotModPhase = ((nextSlotPhase >> 8) & 0x3F) + ((slot.modulation & 0x1F) << 1);

        const sint32 modInt = bit::sign_extend<11>(slot.modulation >> 5);

        const sint32 addrInc2 = bit::sign_extend<17>((nextSmp + modInt + (nextSlotModPhase >> 6)) & slot.mask);

        if (slot.pcm8Bit) {
            const uint32 address2 = slot.startAddress + addrInc2 * sizeof(uint8);
            slot.sample2 = static_cast<sint8>(ReadWRAM<uint8>(address2)) << 8;
        } else {
            const uint32 address2 = (slot.startAddress & ~1) + addrInc2 * sizeof(uint16);
            slot.sample2 = static_cast<sint16>(ReadWRAM<uint16>(address2));
        }
        break;
    }
    case Slot::SoundSource::Noise: slot.sample1 = (m_lfsr & 0xFFu) << 8u; break;
    case Slot::SoundSource::Silence: slot.sample1 = 0; break;
    case Slot::SoundSource::Unknown: slot.sample1 = 0; break; // TODO: what happens in this mode?
    }

    slot.sample1 ^= slot.sampleXOR;
    slot.sample2 ^= slot.sampleXOR;
}

FORCE_INLINE void SCSP::SlotProcessStep4_2(Slot &slot) {}

FORCE_INLINE void SCSP::SlotProcessStep4_4(Slot &slot) {
    if (slot.soundSource == Slot::SoundSource::SoundRAM) {
        switch (m_interpMode) {
        case core::config::audio::SampleInterpolationMode::NearestNeighbor: slot.output = slot.sample1; break;
        case core::config::audio::SampleInterpolationMode::Linear: //
        {
            const sint32 currPhase = slot.reverse ? ~slot.currPhase : slot.currPhase;
            const sint32 phase = ((currPhase >> 8) & 0x3F) + ((slot.modulation & 0x1F) << 1);
            slot.output = slot.sample1 + (((slot.sample2 - slot.sample1) * (phase & 0x3F)) >> 6);
            break;
        }
        }
    } else {
        slot.output = slot.sample1;
    }

    // Amplitude LFO waveform tables
    static constexpr auto sawTable = [] {
        std::array<uint8, 256> arr{};
        for (uint32 i = 0; i < 256; i++) {
            arr[i] = i & ~1;
        }
        return arr;
    }();
    static constexpr auto squareTable = [] {
        std::array<uint8, 256> arr{};
        for (uint32 i = 0; i < 256; i++) {
            arr[i] = i < 128 ? 0x00 : 0xFE;
        }
        return arr;
    }();
    static constexpr auto triangleTable = [] {
        std::array<uint8, 256> arr{};
        for (uint32 i = 0; i < 128; i++) {
            const uint8 il = i;
            const uint8 ir = 255 - i;
            arr[il] = arr[ir] = i * 2;
        }
        return arr;
    }();

    // Compute amplitude LFO
    slot.alfoOutput = 0u;
    if (slot.ampLFOSens != 0) {
        using enum Slot::Waveform;
        switch (slot.ampLFOWaveform) {
        case Saw: slot.alfoOutput = sawTable[slot.lfoStep]; break;
        case Square: slot.alfoOutput = squareTable[slot.lfoStep]; break;
        case Triangle: slot.alfoOutput = triangleTable[slot.lfoStep]; break;
        case Noise: slot.alfoOutput = static_cast<uint8>(m_lfsr) & ~1u; break;
        }
        slot.alfoOutput >>= 7u - slot.ampLFOSens;
    }

    // Advance envelope generator
    slot.IncrementEG(m_sampleCounter + 1);
}

FORCE_INLINE void SCSP::SlotProcessStep5_4(Slot &slot) {
    if (slot.soundSource == Slot::SoundSource::SoundRAM && !slot.active) {
        return;
    }
    const sint32 envLevel = slot.GetEGLevel();
    const sint32 totalLevel = slot.totalLevel << 2u;
    slot.finalLevel = std::min<sint32>(slot.alfoOutput + envLevel + totalLevel, 0x3FF);
}

FORCE_INLINE void SCSP::SlotProcessStep6_4(Slot &slot) {
    if (slot.soundSource == Slot::SoundSource::SoundRAM && !slot.active) {
        if (slot.GetEGLevel() < 0x3C0) {
            slot.output = slot.sampleXOR;
        } else {
            slot.output = 0;
        }
    } else if (!slot.soundDirect) {
        slot.output = (slot.output * ((slot.finalLevel & 0x3F) ^ 0x7F)) >> ((slot.finalLevel >> 6) + 7);
    }
}

FORCE_INLINE void SCSP::SlotProcessStep7_1(Slot &slot) {
    if (!slot.stackWriteInhibit) {
        const uint32 stackIndex = (m_soundStackIndex - 6u) & 63u;
        m_soundStack[stackIndex] = slot.output;
    }
}

ExceptionVector SCSP::AcknowledgeInterrupt(uint8 level) {
    return ExceptionVector::AutoVectorRequest;
}

// -----------------------------------------------------------------------------
// Probe implementation

SCSP::Probe::Probe(SCSP &scsp)
    : m_scsp(scsp) {}

} // namespace ymir::scsp
