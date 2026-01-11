#pragma once

#include "scsp_defs.hpp"
#include "scsp_dsp.hpp"
#include "scsp_midi_defs.hpp"
#include "scsp_slot.hpp"
#include "scsp_timer.hpp"

#include "scsp_callbacks.hpp"
#include "scsp_internal_callbacks.hpp"

#include <ymir/core/configuration.hpp>
#include <ymir/core/scheduler.hpp>
#include <ymir/sys/bus.hpp>
#include <ymir/sys/clocks.hpp>

#include <ymir/state/state_scsp.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <ymir/debug/scsp_tracer_base.hpp>

#include <ymir/hw/cdblock/cdblock_internal_callbacks.hpp>
#include <ymir/sys/system_internal_callbacks.hpp>

#include <ymir/hw/m68k/m68k.hpp>
#include <ymir/hw/m68k/m68k_defs.hpp>

#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/inline.hpp>

#include <algorithm>
#include <array>
#include <iosfwd>
#include <queue>
#include <span>

namespace ymir::scsp {

enum class SCSPAccessType { SCU, M68kCode, M68kData, DMA, Debug };

template <SCSPAccessType accessType>
struct SCSPAccessTypeInfo;

template <>
struct SCSPAccessTypeInfo<SCSPAccessType::SCU> {
    static constexpr auto name = "SCU";
};

template <>
struct SCSPAccessTypeInfo<SCSPAccessType::M68kCode> {
    static constexpr auto name = "M68K code";
};

template <>
struct SCSPAccessTypeInfo<SCSPAccessType::M68kData> {
    static constexpr auto name = "M68K data";
};

template <>
struct SCSPAccessTypeInfo<SCSPAccessType::DMA> {
    static constexpr auto name = "DMA";
};

template <>
struct SCSPAccessTypeInfo<SCSPAccessType::Debug> {
    static constexpr auto name = "debug";
};

template <SCSPAccessType accessType>
constexpr const char *accessTypeName = SCSPAccessTypeInfo<accessType>::name;

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   regs
    //     kyonex
    //   dma
    //   midi

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "SCSP";
    };

    struct regs : public base {
        static constexpr std::string_view name = "SCSP-Regs";
    };

    struct kyonex : public regs {
        static constexpr bool enabled = false;
        static constexpr devlog::Level level = devlog::level::trace;
        static constexpr std::string_view name = "SCSP-KYONEX";
    };

    struct dma : public base {
        static constexpr std::string_view name = "SCSP-DMA";
    };

    struct midi : public base {
        static constexpr std::string_view name = "SCSP-MIDI";
    };

} // namespace grp

class SCSP {
public:
    SCSP(core::Scheduler &scheduler, core::Configuration::Audio &config);

    void Reset(bool hard);

    void SetSendMidiOutputCallback(CBSendMidiOutputMessage callback) {
        m_cbSendMidiOutputMessage = callback;
    }

    void SetSampleCallback(CBOutputSample callback) {
        m_cbOutputSample = callback;
    }

    void MapCallbacks(CBTriggerSoundRequestInterrupt callback) {
        m_cbTriggerSoundRequestInterrupt = callback;
    }

    void MapMemory(sys::SH2Bus &bus);

    void UpdateClockRatios(const sys::ClockRatios &clockRatios);

    /// @brief Configures the MC68EC000 CPU clock speed shift.
    /// 0 means normal speed, 1 is double, 2 is quadruple, etc.
    /// @param clockShift the shift to apply to the clock speed of the MC68EC000, between 0 to 4.
    void SetCPUClockShift(uint64 clockShift) {
        m_m68kClockShift = std::min<uint64>(clockShift, 4);
    }

    // Configures the SCSP step granularity in powers of two.
    // The value must be between 0 and 5, with:
    //   0 = step 32 slots (one sample) at a time (least granular, default)
    //   5 = step 1 slot at a time (most granular)
    // Values greater than 5 are clamped down to 5.
    void SetStepGranularity(uint32 granularity);

    [[nodiscard]] uint32 GetStepGranularity() const noexcept {
        return 5u - m_stepGranularity;
    }

    // Enables or disables debug tracing
    void SetDebugTracing(bool enable);

    [[nodiscard]] bool IsDebugTracingEnabled() const noexcept {
        return m_debugTracing;
    }

    // Feeds CDDA data into the buffer and returns how many thirds of the buffer are used
    uint32 ReceiveCDDA(std::span<uint8, 2352> data);

    // push scheduled message onto MIDI input queue
    void ReceiveMidiInput(MidiMessage &msg);

    void DumpWRAM(std::ostream &out) const;

    void DumpDSP_MPRO(std::ostream &out) const;
    void DumpDSP_TEMP(std::ostream &out) const;
    void DumpDSP_MEMS(std::ostream &out) const;
    void DumpDSP_COEF(std::ostream &out) const;
    void DumpDSP_MADRS(std::ostream &out) const;
    void DumpDSP_MIXS(std::ostream &out) const;
    void DumpDSP_EFREG(std::ostream &out) const;
    void DumpDSP_EXTS(std::ostream &out) const;
    void DumpDSPRegs(std::ostream &out) const;

    void SetCPUEnabled(bool enabled);

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SCSPState &state) const;
    [[nodiscard]] bool ValidateState(const state::SCSPState &state) const;
    void LoadState(const state::SCSPState &state);

private:
    struct QueuedMidiMessage {
        uint64 scheduleTime;
        std::vector<uint8> payload;

        QueuedMidiMessage(uint64 scheduleTime, std::vector<uint8> &&payload)
            : scheduleTime(scheduleTime)
            , payload(std::move(payload)) {}
    };

    alignas(16) std::array<uint8, m68k::kM68KWRAMSize> m_WRAM;

    alignas(16) std::array<uint8, 2352 * 15> m_cddaBuffer;
    uint32 m_cddaReadPos;
    uint32 m_cddaWritePos;
    // set to true when there's enough audio data to be read by the SCSP
    // set to false when the CDDA buffer is empty
    bool m_cddaReady;

    m68k::MC68EC000 m_m68k;
    uint64 m_m68kSpilloverCycles;
    uint64 m_m68kClockShift = 0ull;
    bool m_m68kEnabled;

    core::Scheduler &m_scheduler;
    core::EventID m_sampleTickEvent;

    // Processes a slot tick.
    template <uint32 stepShift, bool debug>
    static void OnSlotTickEvent(core::EventContext &eventContext, void *userContext);

    // Processes a sample tick.
    template <bool debug>
    static void OnSampleTickEvent(core::EventContext &eventContext, void *userContext);

    // Transitional event used when switching from smaller to larger steps.
    // Processes slot by slot until the slot index aligns back to 0, then switches to sample tick events.
    template <uint32 newStepShift, bool debug>
    static void OnTransitionalTickEvent(core::EventContext &eventContext, void *userContext);

    // Retrieves a pointer to the transitional tick event processing function based on the current debug tracing mode.
    template <uint32 newStepShift>
    auto GetTransitionalTickEvent() {
        return m_debugTracing ? OnTransitionalTickEvent<newStepShift, true>
                              : OnTransitionalTickEvent<newStepShift, false>;
    }

    // Retrieves a pointer to the slot tick event processing function based on the current debug tracing mode.
    template <uint32 stepShift>
    auto GetSlotTickEvent() const {
        return m_debugTracing ? OnSlotTickEvent<stepShift, true> : OnSlotTickEvent<stepShift, false>;
    }

    // Retrieves a pointer to the sample tick event processing function based on the current debug tracing mode.
    auto GetSampleTickEvent() const {
        return m_debugTracing ? OnSampleTickEvent<true> : OnSampleTickEvent<false>;
    }

    // Updates the step function for processing samples.
    // Takes into account the current granularity step size and the debug tracing flag.
    void UpdateStepFunction();

    // The emulation step granularity expressed as a bit shift.
    // Note that this is inverted relative to the values given by the public interface (5 - value), therefore:
    //   5 = step 32 slots (one sample) at a time (least granular, default)
    //   0 = step 1 slot at a time (most granular)
    uint32 m_stepGranularity = 5u;

    // Whether debug tracing is enabled
    bool m_debugTracing = false;

    CBOutputSample m_cbOutputSample;
    CBTriggerSoundRequestInterrupt m_cbTriggerSoundRequestInterrupt;
    CBSendMidiOutputMessage m_cbSendMidiOutputMessage;

    std::queue<QueuedMidiMessage> m_midiInputQueue;
    uint64 m_nextMidiTime;

    std::array<uint8, kMidiBufferSize> m_midiInputBuffer;
    uint32 m_midiInputReadPos;
    uint32 m_midiInputWritePos;
    bool m_midiInputOverflow;

    std::array<uint8, kMidiBufferSize> m_midiOutputBuffer;
    uint32 m_midiOutputSize;
    sint32 m_expectedOutputPacketSize;

    // -------------------------------------------------------------------------
    // Threading

    void EnableThreading(bool enable);

    // -------------------------------------------------------------------------
    // Memory accessors (SCU-facing bus)
    // 16-bit reads, 8- or 16-bit writes

    template <mem_primitive T>
    FLATTEN T ReadWRAM(uint32 address) {
        static_assert(!std::is_same_v<T, uint32>, "Invalid SCSP WRAM read size");
        // TODO: handle memory size bit
        return util::ReadBE<T>(&m_WRAM[address & 0x7FFFF]);
    }

    template <mem_primitive T>
    FLATTEN void WriteWRAM(uint32 address, T value) {
        static_assert(!std::is_same_v<T, uint32>, "Invalid SCSP WRAM write size");
        // TODO: handle memory size bit
        util::WriteBE<T>(&m_WRAM[address & 0x7FFFF], value);
    }

    template <mem_primitive T>
    T ReadReg(uint32 address) {
        return ReadReg<T, SCSPAccessType::SCU>(address);
    }

    template <mem_primitive T>
    void WriteReg(uint32 address, T value) {
        WriteReg<T, SCSPAccessType::SCU>(address, value);
    }

    // -------------------------------------------------------------------------
    // MC68EC000-facing bus
    // 8- or 16-bit reads and writes

    friend class m68k::MC68EC000;

    template <mem_primitive T, bool instrFetch>
    T Read(uint32 address) {
        static constexpr SCSPAccessType accessType = instrFetch ? SCSPAccessType::M68kCode : SCSPAccessType::M68kData;

        if (util::AddressInRange<0x000000, 0x07FFFF>(address)) {
            // TODO: handle memory size bit
            return ReadWRAM<T>(address);
        } else if (util::AddressInRange<0x100000, 0x1FFFFF>(address)) {
            return ReadReg<T, accessType>(address & 0xFFF);
        } else {
            return 0;
        }
    }

    template <mem_primitive T>
    void Write(uint32 address, T value) {
        if (util::AddressInRange<0x000000, 0x07FFFF>(address)) {
            WriteWRAM<T>(address, value);
        } else if (util::AddressInRange<0x100000, 0x1FFFFF>(address)) {
            WriteReg<T, SCSPAccessType::M68kData>(address & 0xFFF, value);
        }
    }

    // -------------------------------------------------------------------------
    // Generic accessors
    // T is either uint8 or uint16, never uint32

    // Register accesses are handled by individual templated methods that use flags for each half of the 16-bit
    // value. 16-bit accesses have both flags set, while 8-bit writes only have the flag for the corresponding half
    // set (upper for even addresses, lower for odd addresses).
    //
    // These methods receive a 16-bit value containing either the full 16-bit value or the 8-bit value shifted into
    // the appropriate place so that all three cases are handled consistently and efficiently, including values that
    // span both halves:
    //   Access                    Contents of 16-bit value sent to accessor methods
    //   16-bit                    The entire value, unmodified
    //   8-bit on even addresses   8-bit value in bits 15-8
    //   8-bit on odd addresses    8-bit value in bits 7-0

    template <mem_primitive T, SCSPAccessType accessType>
    T ReadReg(uint32 address) {
        static_assert(!std::is_same_v<T, uint32>, "Invalid SCSP register read size");
        static constexpr bool is16 = std::is_same_v<T, uint16>;
        static constexpr bool peek = accessType == SCSPAccessType::Debug;

        address &= 0xFFF;

        if constexpr (!peek) {
            devlog::trace<grp::regs>("{}-bit SCSP register read via {} bus from {:03X}", sizeof(T) * 8,
                                     accessTypeName<accessType>, address);
        }

        using namespace util;

        auto shiftByte = [](uint16 value) {
            if constexpr (is16) {
                return value;
            } else {
                return value >> 8u;
            }
        };

        auto read16 = [=](uint16 value) {
            if constexpr (is16) {
                return value;
            } else if (address & 1) {
                return value >> 0u;
            } else {
                return value >> 8u;
            }
        };

        /**/ if (AddressInRange<0x000, 0x3FF>(address)) {
            // Slot registers
            const uint32 slotIndex = address >> 5;
            auto &slot = m_slots[slotIndex];
            const T value = slot.ReadReg<T>(address & 0x1F);
            return value;
        } else if (AddressInRange<0x600, 0x67F>(address)) {
            // SOUS
            const uint32 idx = (address >> 1u) & 0x3F;
            const uint16 stack = m_soundStack[idx];
            return read16(stack);
        } else if (AddressInRange<0x700, 0x77F>(address)) {
            // DSP COEF
            const uint16 coef = m_dsp.coeffs[(address >> 1u) & 0x3F] << 3u;
            return read16(coef);
        } else if (AddressInRange<0x780, 0x7BF>(address)) {
            // DSP MADRS
            return read16(m_dsp.addrs[(address >> 1u) & 0x1F]);
        } else if (AddressInRange<0x7C0, 0x7FF>(address)) {
            // TODO: what's supposed to be in here? nothing?
            return 0;
        } else if (AddressInRange<0x800, 0xBFF>(address)) {
            // DSP MPRO
            const uint32 index = (address >> 3u) & 0x7F;
            const uint32 subindex = ((address >> 1u) & 0x3) ^ 3;
            return read16(m_dsp.program[index].u16[subindex]);
        } else if (AddressInRange<0xC00, 0xDFF>(address)) {
            // DSP TEMP
            const uint32 offset = (address >> 1u) & 0x1;
            const uint32 index = (address >> 2u) & 0x7F;
            if (offset == 0) {
                return read16(bit::extract<0, 7>(m_dsp.tempMem[index]));
            } else {
                return read16(bit::extract<8, 23>(m_dsp.tempMem[index]));
            }
        } else if (AddressInRange<0xE00, 0xE7F>(address)) {
            // DSP MEMS
            const uint32 offset = (address >> 1u) & 0x1;
            const uint32 index = (address >> 2u) & 0x1F;
            if (offset == 0) {
                return read16(bit::extract<0, 7>(m_dsp.soundMem[index]));
            } else {
                return read16(bit::extract<8, 23>(m_dsp.soundMem[index]));
            }
        } else if (AddressInRange<0xE80, 0xEBF>(address)) {
            // DSP MIXS
            const uint32 offset = (address >> 1u) & 0x1;
            const uint32 index = m_dsp.GetMIXSIndex((address >> 2u) & 0xF) ^ 0x10;
            if (offset == 0) {
                return read16(bit::extract<0, 3>(m_dsp.mixStack[index]));
            } else {
                return read16(bit::extract<4, 19>(m_dsp.mixStack[index]));
            }
        } else if (AddressInRange<0xEC0, 0xEDF>(address)) {
            // DSP EFREG
            return read16(m_dsp.effectOut[(address >> 1u) & 0xF]);
        } else if (AddressInRange<0xEE0, 0xEE3>(address)) {
            // DSP EXTS
            return read16(m_dsp.audioInOut[(address >> 1u) & 0x1]);
        }

        // Common registers

        switch (address) {
        case 0x400: return shiftByte(ReadReg400<is16, true, peek>()); // MVOL, DAC18B, MEM4MB are write-only
        case 0x401: return ReadReg400<true, is16, peek>();            // Only VER is readable, and it is 0
        case 0x402: return shiftByte(ReadReg402<is16, true, peek>()); // RBP and RBL are write-only
        case 0x403: return ReadReg402<true, is16, peek>();            // RBP and RBL are write-only

        case 0x404: return shiftByte(ReadMIDIIn<is16, true, peek>());
        case 0x405: return ReadMIDIIn<true, is16, peek>();
        case 0x406: return 0; // MOBUF is write-only
        case 0x407: return 0; // MOBUF is write-only

        case 0x408: return shiftByte(ReadSlotStatus<is16, true, peek>());
        case 0x409: return ReadSlotStatus<true, is16, peek>();

        case 0x412: return shiftByte(ReadReg412<is16, true, peek>()); // DMEA is write-only
        case 0x413: return ReadReg412<true, is16, peek>();            // DMEA is write-only
        case 0x414: return shiftByte(ReadReg414<is16, true, peek>()); // DMEA and DRGA are write-only
        case 0x415: return ReadReg414<true, is16, peek>();            // DMEA and DRGA are write-only
        case 0x416: return shiftByte(ReadReg416<is16, true, peek>());
        case 0x417: return ReadReg416<true, is16, peek>();

        case 0x418: return shiftByte(ReadTimer<is16, true, peek>(0)); // Timers are write-only
        case 0x419: return ReadTimer<true, is16, peek>(0);            // Timers are write-only
        case 0x41A: return shiftByte(ReadTimer<is16, true, peek>(1)); // Timers are write-only
        case 0x41B: return ReadTimer<true, is16, peek>(1);            // Timers are write-only
        case 0x41C: return shiftByte(ReadTimer<is16, true, peek>(2)); // Timers are write-only
        case 0x41D: return ReadTimer<true, is16, peek>(2);            // Timers are write-only

        case 0x41E: return shiftByte(ReadSCIEB());
        case 0x41F: return ReadSCIEB();
        case 0x420: return shiftByte(ReadSCIPD());
        case 0x421: return ReadSCIPD();
        case 0x422: return 0; // SCIRE is write-only
        case 0x423: return 0; // SCIRE is write-only

        case 0x424: return shiftByte(ReadSCILV(0));
        case 0x425: return ReadSCILV(0);
        case 0x426: return shiftByte(ReadSCILV(1));
        case 0x427: return ReadSCILV(1);
        case 0x428: return shiftByte(ReadSCILV(2));
        case 0x429: return ReadSCILV(2);

        case 0x42A: return shiftByte(ReadMCIEB());
        case 0x42B: return ReadMCIEB();
        case 0x42C: return shiftByte(ReadMCIPD());
        case 0x42D: return ReadMCIPD();
        case 0x42E: return 0; // MCIRE is write-only
        case 0x42F: return 0; // MCIRE is write-only

        default:
            if constexpr (!peek) {
                devlog::trace<grp::regs>("Unhandled {}-bit SCSP register read via {} bus from {:03X}", sizeof(T) * 8,
                                         accessTypeName<accessType>, address);
            }
            break;
        }

        return 0;
    }

    template <mem_primitive T, SCSPAccessType accessType>
    void WriteReg(uint32 address, T value) {
        static_assert(!std::is_same_v<T, uint32>, "Invalid SCSP register write size");
        static constexpr bool is16 = std::is_same_v<T, uint16>;
        static constexpr bool poke = accessType == SCSPAccessType::Debug;

        address &= 0xFFF;

        if constexpr (!poke) {
            devlog::trace<grp::regs>("{}-bit SCSP register write via {} bus to {:03X} = {:X}", sizeof(T) * 8,
                                     accessTypeName<accessType>, address, value);
        }

        using namespace util;

        uint16 value16 = value;
        if constexpr (!is16) {
            if ((address & 1) == 0) {
                value16 <<= 8u;
            }
        }

        auto write16 = [=](auto &reg, uint16 value) {
            if constexpr (is16) {
                reg = value;
            } else if (address & 1) {
                reg = (reg & 0xFF00) | value;
            } else {
                reg = (reg & 0x00FF) | value;
            }
        };

        /**/ if (AddressInRange<0x000, 0x3FF>(address)) {
            // Slot registers
            const uint32 slotIndex = address >> 5;
            auto &slot = m_slots[slotIndex];
            slot.WriteReg<T>(address & 0x1F, value);
            if ((address & 0x1E) == 0x00 && bit::test<12>(value16)) {
                m_kyonex = true;
            }
            return;
        } else if (AddressInRange<0x600, 0x67F>(address)) {
            // SOUS
            const uint32 idx = (address >> 1u) & 0x3F;
            return write16(m_soundStack[idx], value16);
        } else if (AddressInRange<0x700, 0x77F>(address)) {
            // DSP COEF
            const uint32 idx = (address >> 1u) & 0x3F;
            uint16 coef = m_dsp.coeffs[idx] << 3u;
            write16(coef, value16);
            m_dsp.coeffs[idx] = coef >> 3u;
            return;
        } else if (AddressInRange<0x780, 0x7BF>(address)) {
            // DSP MADRS
            return write16(m_dsp.addrs[(address >> 1u) & 0x1F], value16);
        } else if (AddressInRange<0x7C0, 0x7FF>(address)) {
            // TODO: what's supposed to be in here? nothing?
            return;
        } else if (AddressInRange<0x800, 0xBFF>(address)) {
            // DSP MPRO
            const uint32 index = (address >> 3u) & 0x7F;
            const uint32 subindex = ((address >> 1u) & 0x3) ^ 3;
            write16(m_dsp.program[index].u16[subindex], value16);
            m_dsp.UpdateProgramLength(index);
            return;
        } else if (AddressInRange<0xC00, 0xDFF>(address)) {
            // DSP TEMP
            const uint32 offset = (address >> 1u) & 0x1;
            const uint32 index = (address >> 2u) & 0x7F;
            if (offset == 0) {
                uint16 tmpValue = bit::extract<0, 7>(m_dsp.tempMem[index]);
                write16(tmpValue, value16);
                bit::deposit_into<0, 7>(m_dsp.tempMem[index], tmpValue);
            } else {
                uint16 tmpValue = bit::extract<8, 23>(m_dsp.tempMem[index]);
                write16(tmpValue, value16);
                bit::deposit_into<8, 23>(m_dsp.tempMem[index], tmpValue);
                m_dsp.tempMem[index] = bit::sign_extend<24>(m_dsp.tempMem[index]);
            }
            return;
        } else if (AddressInRange<0xE00, 0xE7F>(address)) {
            // DSP MEMS
            const uint32 offset = (address >> 1u) & 0x1;
            const uint32 index = (address >> 2u) & 0x1F;
            if (offset == 0) {
                uint16 tmpValue = bit::extract<0, 7>(m_dsp.soundMem[index]);
                write16(tmpValue, value16);
                bit::deposit_into<0, 7>(m_dsp.soundMem[index], tmpValue);
            } else {
                uint16 tmpValue = bit::extract<8, 23>(m_dsp.soundMem[index]);
                write16(tmpValue, value16);
                bit::deposit_into<8, 23>(m_dsp.soundMem[index], tmpValue);
                m_dsp.soundMem[index] = bit::sign_extend<24>(m_dsp.soundMem[index]);
            }
            return;
        } else if (AddressInRange<0xE80, 0xEBF>(address)) {
            // DSP MIXS
            const uint32 offset = (address >> 1u) & 0x1;
            const uint32 index = m_dsp.GetMIXSIndex((address >> 2u) & 0xF) ^ 0x10;
            if (offset == 0) {
                uint16 tmpValue = bit::extract<0, 3>(m_dsp.mixStack[index]);
                write16(tmpValue, value16);
                bit::deposit_into<0, 3>(m_dsp.mixStack[index], tmpValue);
            } else {
                uint16 tmpValue = bit::extract<4, 19>(m_dsp.mixStack[index]);
                write16(tmpValue, value16);
                bit::deposit_into<4, 19>(m_dsp.mixStack[index], tmpValue);
            }
            return;
        } else if (AddressInRange<0xEC0, 0xEDF>(address)) {
            // DSP EFREG
            const uint32 index = (address >> 1u) & 0xF;
            return write16(m_dsp.effectOut[index], value16);
        } else if (AddressInRange<0xEE0, 0xEE3>(address)) {
            // DSP EXTS
            const uint32 index = (address >> 1u) & 0x1;
            return write16(m_dsp.audioInOut[index], value16);
        }

        // Common registers

        switch (address) {
        case 0x400: WriteReg400<is16, true>(value16); break;
        case 0x401: WriteReg400<true, is16>(value16); break;
        case 0x402: WriteReg402<is16, true>(value16); break;
        case 0x403: WriteReg402<true, is16>(value16); break;

        case 0x404: // MIDI In registers are read-only
        case 0x405: // MIDI In registers are read-only
        case 0x406: WriteMIDIOut<is16, true, poke>(value16); break;
        case 0x407: WriteMIDIOut<true, is16, poke>(value16); break;

        case 0x408: WriteSlotStatus<is16, true>(value16); break;
        case 0x409: WriteSlotStatus<true, is16>(value16); break;

        case 0x412: WriteReg412<is16, true>(value16); break;
        case 0x413: WriteReg412<true, is16>(value16); break;
        case 0x414: WriteReg414<is16, true>(value16); break;
        case 0x415: WriteReg414<true, is16>(value16); break;
        case 0x416: WriteReg416<is16, true, poke>(value16); break;
        case 0x417: WriteReg416<true, is16, poke>(value16); break;

        case 0x418: WriteTimer<is16, true>(0, value16); break;
        case 0x419: WriteTimer<true, is16>(0, value16); break;
        case 0x41A: WriteTimer<is16, true>(1, value16); break;
        case 0x41B: WriteTimer<true, is16>(1, value16); break;
        case 0x41C: WriteTimer<is16, true>(2, value16); break;
        case 0x41D: WriteTimer<true, is16>(2, value16); break;

        case 0x41E: WriteSCIEB<is16, true>(value16); break;
        case 0x41F: WriteSCIEB<true, is16>(value16); break;
        case 0x420: WriteSCIPD<is16, true>(value16); break;
        case 0x421: WriteSCIPD<true, is16>(value16); break;
        case 0x422: WriteSCIRE<is16, true>(value16); break;
        case 0x423: WriteSCIRE<true, is16>(value16); break;

        case 0x424: WriteSCILV<is16, true>(0, value16); break;
        case 0x425: WriteSCILV<true, is16>(0, value16); break;
        case 0x426: WriteSCILV<is16, true>(1, value16); break;
        case 0x427: WriteSCILV<true, is16>(1, value16); break;
        case 0x428: WriteSCILV<is16, true>(2, value16); break;
        case 0x429: WriteSCILV<true, is16>(2, value16); break;

        case 0x42A: WriteMCIEB<is16, true, poke>(value16); break;
        case 0x42B: WriteMCIEB<true, is16, poke>(value16); break;
        case 0x42C: WriteMCIPD<is16, true, poke>(value16); break;
        case 0x42D: WriteMCIPD<true, is16, poke>(value16); break;
        case 0x42E: WriteMCIRE<is16, true, poke>(value16); break;
        case 0x42F: WriteMCIRE<true, is16, poke>(value16); break;

        default:
            if constexpr (!poke) {
                devlog::trace<grp::regs>("Unhandled {}-bit SCSP register write via {} bus to {:03X} = {:X}",
                                         sizeof(T) * 8, accessTypeName<accessType>, address, value);
            }
            break;
        }
    }

    // --- Mixer Register ---
    // --- Sound Memory Configuration Register ---

    template <bool lowerByte, bool upperByte, bool peek>
    uint16 ReadReg400() const {
        if constexpr (!peek) {
            return 0;
        }

        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 3>(value, m_masterVolume);
            // bit::deposit_into<4, 7>(value, 0); // VER (version) field, always zero; added here for completeness
        }
        if constexpr (upperByte) {
            bit::deposit_into<8>(value, m_dac18Bits);
            bit::deposit_into<9>(value, m_mem4MB);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    void WriteReg400(uint16 value) {
        if constexpr (lowerByte) {
            m_masterVolume = bit::extract<0, 3>(value);
        }
        if constexpr (upperByte) {
            m_dac18Bits = bit::test<8>(value);
            m_mem4MB = bit::test<9>(value);
        }
    }

    // --- Slot Status Register ---

    template <bool lowerByte, bool upperByte, bool peek>
    uint16 ReadSlotStatus() const {
        uint16 value = 0;
        const Slot &slot = m_slots[m_monitorSlotCall];
        bit::deposit_into<0, 4>(value, slot.GetEGLevel() >> 5u);
        bit::deposit_into<5, 6>(value, static_cast<uint8>(slot.egState));
        bit::deposit_into<7, 10>(value, slot.currSample >> 12u);
        if constexpr (peek) {
            bit::deposit_into<11, 15>(value, m_monitorSlotCall);
        } else {
            devlog::trace<grp::regs>(
                "Monitor slot {:02d} read -> {:04X}  address={:05X} sample={:04X} egstate={} eglevel={:03X}",
                m_monitorSlotCall, value, slot.startAddress + slot.currSample, slot.currSample,
                static_cast<uint8>(slot.egState), slot.egLevel);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    void WriteSlotStatus(uint16 value) {
        if constexpr (upperByte) {
            m_monitorSlotCall = bit::extract<11, 15>(value);
        }
    }

    // --- MIDI Register ---

    void ProcessMidiInputQueue();
    void FlushMidiOutput(bool endPacket);

    template <bool lowerByte, bool upperByte, bool peek>
    uint16 ReadMIDIIn() {
        uint8 mibuf = m_midiInputBuffer[m_midiInputReadPos];
        bool mienp = m_midiInputReadPos == m_midiInputWritePos;
        bool mifull = ((m_midiInputWritePos + 1) % m_midiInputBuffer.size()) == m_midiInputReadPos;
        bool miovf = m_midiInputOverflow;

        uint16 value = 0;
        bit::deposit_into<0, 7>(value, mibuf);
        bit::deposit_into<8>(value, mienp);
        bit::deposit_into<9>(value, mifull);
        bit::deposit_into<10>(value, miovf);
        bit::deposit_into<11>(value, true); // output always empty for now

        if constexpr (!peek && lowerByte) {
            // advance read position
            if (!mienp) {
                m_midiInputReadPos = (m_midiInputReadPos + 1) % m_midiInputBuffer.size();
            } else {
                devlog::trace<grp::midi>("Invalid read from empty MIDI buffer");
            }
        }

        return value;
    }

    template <bool lowerByte, bool upperByte, bool poke>
    void WriteMIDIOut(uint16 value) {
        if constexpr (lowerByte && !poke) {
            // basically we need to try and gather individual bytes into a single packet of MIDI data
            // most MIDI messages only have one or two bytes of data
            // however, if it's a sysex, it can be an arbitrary size and is terminated by 0xF7

            uint8 byte = bit::extract<0, 7>(value);
            m_midiOutputBuffer[m_midiOutputSize++] = byte;

            if (m_expectedOutputPacketSize == -1) {
                // currently building a SysEx message (note that these can actually be broken up into multiple packets
                // afaik) flush if either we hit a terminator byte *or* buffer hits maximum size
                if (byte == 0xF7 || m_midiOutputSize == kMidiBufferSize) {
                    FlushMidiOutput(byte == 0xF7);
                }
            } else if (m_midiOutputBuffer.size() == m_expectedOutputPacketSize) {
                // finished building normal midi packet
                FlushMidiOutput(true);
            } else if (m_midiOutputSize == 1) {
                // starting new midi packet, check status byte to see what kind of packet we're building
                if ((byte >> 4) == 0b1000) {
                    // note off
                    m_expectedOutputPacketSize = 3;
                } else if ((byte >> 4) == 0b1001) {
                    // note on
                    m_expectedOutputPacketSize = 3;
                } else if ((byte >> 4) == 0b1010) {
                    // key pressure (aftertouch)
                    m_expectedOutputPacketSize = 3;
                } else if ((byte >> 4) == 0b1011) {
                    // control change
                    m_expectedOutputPacketSize = 3;
                } else if ((byte >> 4) == 0b1100) {
                    // program change
                    m_expectedOutputPacketSize = 2;
                } else if ((byte >> 4) == 0b1101) {
                    // channel pressure (aftertouch)
                    m_expectedOutputPacketSize = 2;
                } else if ((byte >> 4) == 0b1110) {
                    // pitch bend change
                    m_expectedOutputPacketSize = 3;
                } else if (byte == 0xF0) {
                    // sysex, arbitrary size (terminated by 0xF7)
                    m_expectedOutputPacketSize = -1;
                } else if (byte == 0xF1) {
                    // MIDI time code quarter frame
                    m_expectedOutputPacketSize = 2;
                } else if (byte == 0xF2) {
                    // song position pointer
                    m_expectedOutputPacketSize = 3;
                } else if (byte == 0xF3) {
                    // song select
                    m_expectedOutputPacketSize = 2;
                } else {
                    // everything else is one-byte, so just send as is
                    FlushMidiOutput(true);
                }
            }
        }
    }

    // --- Timer Register ---

    template <bool lowerByte, bool upperByte, bool peek>
    uint16 ReadTimer(uint32 index) const {
        if constexpr (!peek) {
            return 0;
        }

        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 7>(value, m_timers[index].ReadTIMx());
        }
        if constexpr (upperByte) {
            bit::deposit_into<8, 10>(value, m_timers[index].ReadTxCTL());
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    void WriteTimer(uint32 index, uint16 value) {
        if constexpr (lowerByte) {
            m_timers[index].WriteTIMx(bit::extract<0, 7>(value));
        }
        if constexpr (upperByte) {
            m_timers[index].WriteTxCTL(bit::extract<8, 10>(value));
        }
    }

    // --- Interrupt Control Register ---

    uint16 ReadSCIEB() const {
        return m_m68kEnabledInterrupts;
    }

    template <bool lowerByte, bool upperByte>
    void WriteSCIEB(uint16 value) {
        util::SplitWriteWord<lowerByte, upperByte, 0, 10>(m_m68kEnabledInterrupts, value);
        UpdateM68KInterrupts();
    }

    uint16 ReadSCIPD() const {
        return m_m68kPendingInterrupts;
    }

    template <bool lowerByte, bool upperByte>
    void WriteSCIPD(uint16 value) {
        if constexpr (lowerByte) {
            bit::deposit_into<5>(m_m68kPendingInterrupts, bit::extract<5>(value));
            UpdateM68KInterrupts();
        }
    }

    template <bool lowerByte, bool upperByte>
    void WriteSCIRE(uint16 value) {
        m_m68kPendingInterrupts &= ~value;
        UpdateM68KInterrupts();
    }

    // ---

    uint16 ReadMCIEB() const {
        return m_scuEnabledInterrupts;
    }

    template <bool lowerByte, bool upperByte, bool poke>
    void WriteMCIEB(uint16 value) {
        util::SplitWriteWord<lowerByte, upperByte, 0, 10>(m_scuEnabledInterrupts, value);
        if constexpr (!poke) {
            UpdateSCUInterrupts();
        }
    }

    uint16 ReadMCIPD() const {
        return m_scuPendingInterrupts;
    }

    template <bool lowerByte, bool upperByte, bool poke>
    void WriteMCIPD(uint16 value) {
        if constexpr (lowerByte) {
            bit::deposit_into<5>(m_scuPendingInterrupts, bit::extract<5>(value));
            if constexpr (!poke) {
                UpdateSCUInterrupts();
            }
        }
    }

    template <bool lowerByte, bool upperByte, bool poke>
    void WriteMCIRE(uint16 value) {
        m_scuPendingInterrupts &= ~value;
        if constexpr (!poke) {
            UpdateSCUInterrupts();
        }
    }

    // ---

    uint16 ReadSCILV(uint32 index) {
        return m_m68kInterruptLevels[index];
    }

    template <bool lowerByte, bool upperByte>
    void WriteSCILV(uint32 index, uint16 value) {
        if constexpr (lowerByte) {
            m_m68kInterruptLevels[index] = bit::extract<0, 7>(value);
            UpdateM68KInterrupts();
        }
    }

    // --- DMA Transfer Register ---

    template <bool lowerByte, bool upperByte, bool peek>
    uint16 ReadReg412() const {
        if constexpr (!peek) {
            return 0;
        }

        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<1, 7>(value, bit::extract<1, 7>(m_dmaMemAddress));
        }
        if constexpr (upperByte) {
            bit::deposit_into<8, 15>(value, bit::extract<8, 15>(m_dmaMemAddress));
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    void WriteReg412(uint16 value) {
        if constexpr (lowerByte) {
            bit::deposit_into<1, 7>(m_dmaMemAddress, bit::extract<1, 7>(value));
        }
        if constexpr (upperByte) {
            bit::deposit_into<8, 15>(m_dmaMemAddress, bit::extract<8, 15>(value));
        }
    }

    template <bool lowerByte, bool upperByte, bool peek>
    uint16 ReadReg414() const {
        if constexpr (!peek) {
            return 0;
        }

        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<1, 7>(value, bit::extract<1, 7>(m_dmaRegAddress));
        }
        if constexpr (upperByte) {
            bit::deposit_into<8, 11>(value, bit::extract<8, 11>(m_dmaRegAddress));
            bit::deposit_into<12, 15>(value, bit::extract<16, 19>(m_dmaMemAddress));
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    void WriteReg414(uint16 value) {
        if constexpr (lowerByte) {
            bit::deposit_into<1, 7>(m_dmaRegAddress, bit::extract<1, 7>(value));
        }
        if constexpr (upperByte) {
            bit::deposit_into<8, 11>(m_dmaRegAddress, bit::extract<8, 11>(value));
            bit::deposit_into<16, 19>(m_dmaMemAddress, bit::extract<12, 15>(value));
        }
    }

    template <bool lowerByte, bool upperByte, bool peek>
    uint16 ReadReg416() const {
        uint16 value = 0;
        if constexpr (lowerByte && peek) {
            bit::deposit_into<1, 7>(value, bit::extract<1, 7>(m_dmaXferLength));
        }
        if constexpr (upperByte) {
            if constexpr (peek) {
                bit::deposit_into<8, 11>(value, bit::extract<8, 11>(m_dmaXferLength));
            }
            bit::deposit_into<12>(value, m_dmaExec);
            bit::deposit_into<13>(value, m_dmaXferToMem);
            bit::deposit_into<14>(value, m_dmaGate);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte, bool poke>
    void WriteReg416(uint16 value) {
        if constexpr (lowerByte) {
            bit::deposit_into<1, 7>(m_dmaXferLength, bit::extract<1, 7>(value));
        }
        if constexpr (upperByte) {
            bit::deposit_into<8, 11>(m_dmaXferLength, bit::extract<8, 11>(value));
            m_dmaExec |= bit::test<12>(value);
            m_dmaXferToMem = bit::test<13>(value);
            m_dmaGate = bit::test<14>(value);
            if constexpr (!poke) {
                ExecuteDMA();
            }
        }
    }

    // --- DSP Registers ---

    template <bool lowerByte, bool upperByte, bool peek>
    uint16 ReadReg402() const {
        if constexpr (!peek) {
            return 0;
        }

        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 6>(value, m_dsp.ringBufferLeadAddress);
        }
        util::SplitReadWord<lowerByte, upperByte, 7, 8>(value, m_dsp.ringBufferLength);
        return value;
    }

    template <bool lowerByte, bool upperByte>
    void WriteReg402(uint16 value) {
        if constexpr (lowerByte) {
            m_dsp.ringBufferLeadAddress = bit::extract<0, 6>(value);
            m_dsp.UpdateRBP();
        }
        util::SplitWriteWord<lowerByte, upperByte, 7, 8>(m_dsp.ringBufferLength, value);
        m_dsp.UpdateRBL();
    }

    // -------------------------------------------------------------------------
    // Registers

    // --- Sound slots ---

    alignas(128) std::array<Slot, 32> m_slots;

    bool m_kyonex;        // (W) KYONEX - Key on execute
    bool m_kyonexExecute; // KYONEX currently being processed

    // --- Mixer Register ---

    uint32 m_masterVolume; // (W) MVOL - master volume adjustment after all audio processing
    bool m_dac18Bits;      // (W) DAC18B - outputs 18-bit instead of 16-bit data to DAC

    // --- Sound Memory Configuration Register ---

    bool m_mem4MB; // (W) MEM4MB - enables full 4 Mbit RAM access instead of 1 Mbit

    // --- Slot Status Register ---

    uint8 m_monitorSlotCall; // (W) MSLC - selects a slot to monitor the current sample offset from SA
                             // (R) CA - Call Address - the offset from SA of the current sample (in 4 KiB units)

    // --- MIDI Register ---

    // TODO

    // --- Timer Register ---

    alignas(16) std::array<Timer, 3> m_timers;

    // --- Interrupt Control Register ---

    uint16 m_scuEnabledInterrupts;  // (W) MCIEB
    uint16 m_scuPendingInterrupts;  // (W) MCIPD
    uint16 m_m68kEnabledInterrupts; // (W) SCIEB
    uint16 m_m68kPendingInterrupts; // (W) SCIPD

    std::array<uint8, 3> m_m68kInterruptLevels; // (W) SCILV0-2

    void SetInterrupt(uint16 intr, bool level);

    void UpdateM68KInterrupts();
    void UpdateSCUInterrupts() {
        m_cbTriggerSoundRequestInterrupt(m_scuPendingInterrupts & m_scuEnabledInterrupts);
    }

    // --- DMA Transfer Register ---

    bool m_dmaExec;         // (R/W) DEXE - DMA Execution
    bool m_dmaXferToMem;    // (R/W) DDIR - DMA Transfer Direction (0=mem->reg, 1=reg->mem)
    bool m_dmaGate;         // (R/W) DGATE - DMA Gate (0=mem/reg->dst, 1=zero->dest)
    uint32 m_dmaMemAddress; // (W) DMEA - DMA Memory Start Address
    uint16 m_dmaRegAddress; // (W) DRGA - DMA Register Start Address
    uint16 m_dmaXferLength; // (W) DTLG - DMA Transfer Length

    void ExecuteDMA();

    // --- Direct Sound Data Stack ---

    alignas(16) std::array<uint16, 64> m_soundStack; // SOUS - Sound Stack
    uint32 m_soundStackIndex;

    // --- DSP Registers ---

    DSP m_dsp;

    // -------------------------------------------------------------------------
    // Audio processing

    template <uint32 stepShift, bool debug>
    void TickSlots(); // Processes a single slot (16 SCSP cycles)
    template <bool debug>
    void TickSample(); // Processes a full sample (512 SCSP cycles)

    void RunM68K(uint64 cycles);
    void UpdateTimers();

    // Emulates one slot's worth of cycles.
    // This executes the 7 slot operations once, and 4 DSP program steps.
    template <uint32 stepShift, bool debug>
    void StepSlots();

    // Emulates an entire sample's worth of cycles.
    // This executes the 7 slot operations 32 times, and all 128 DSP program steps.
    // Requires the slot counter to be aligned to 0.
    template <bool debug>
    void StepSample();

    // Performs the 7 operation steps on slots from index i to i-6 (modulo 32).
    template <bool debug>
    void ProcessSlots(uint32 i);

    // Advances the sample counter by one.
    void IncrementSampleCounter();

    // Accumulates audio data into the final output using the given send level and panning parameters.
    void AddOutput(sint32 output, uint8 sendLevel, uint8 pan);

    // The SCSP performs 7 operations in parallel on 7 different slots from i to i-6:
    //   op1 (slot i-0): Phase generation and pitch LFO calculation
    //   op2 (slot i-1): X/Y modulation data read and address pointer calculation
    //   op3 (slot i-2): Waveform read
    //   op4 (slot i-3): Interpolation, envelope generator update and amplitude LFO calculation
    //   op5 (slot i-4): Amplitude LFO level calculation
    //   op6 (slot i-5): Total level calculation
    //   op7 (slot i-6): Sound stack write
    //
    // The SCSP as a whole runs on an 8-step cycle, performing different tasks at each step.
    // Every two cycles, the SCSP alternates between giving the DSP or the slots RAM access, in that order.
    // In other words, RAM access is granted to DSP on cycles 0,1,4,5 and to slots on cycles 2,3,6,7.
    //
    // For slot operations, cycle 0 is generally used to read registers and most operations happen on cycle 7.
    // Some operations need to be broken down into substeps in order to do multiple RAM reads or latch values.
    // The slot operation steps are implemented by the SlotProcessStepN_X functions below, where N is the operation
    // number and X is the substep. Substeps align with every two cycles of the 8-step cycle -- X=1 for cycles 0,1, X=2
    // for cycles 2,3, X=3 for cycles 4,5 and X=4 for cycles 6,7.
    //
    // The DSP runs in parallel with slot processing on a two-step cycle:
    //   0: Read/write registers and memory
    //   1: Execute one program step
    // So for each 8-step cycle, the DSP runs four program steps.
    //
    // Many sample cycle-based operations, including the final output sent to the DAC, are aligned to operation 7 when
    // it finishes processing slot 31.

#define TPL_DEBUG template <bool debug>

    TPL_DEBUG void SlotProcessStep1_4(Slot &slot); // Phase generation and pitch LFO calculation
    void SlotProcessStep2_2(Slot &slot);           // Phase latch
    void SlotProcessStep2_3(Slot &slot);           // X modulation data read
    void SlotProcessStep2_4(Slot &slot);           // Y modulation data read and address pointer calculation
    void SlotProcessStep3_2(Slot &slot);           // Waveform read (current sample)
    void SlotProcessStep3_4(Slot &slot);           // Waveform read (next sample)
    void SlotProcessStep4_2(Slot &slot);           // Current sample latch for interpolation
    void SlotProcessStep4_4(Slot &slot); // Interpolation, envelope generator update and amplitude LFO calculation
    void SlotProcessStep5_4(Slot &slot); // ALFO calculation
    void SlotProcessStep6_4(Slot &slot); // Total level calculation
    void SlotProcessStep7_1(Slot &slot); // Sound stack write

#undef TPL_DEBUG

    // The audio interpolation mode.
    // Linear is accurate to the hardware. Other options are offered as tweaks or enhancements.
    core::config::audio::SampleInterpolationMode m_interpMode = core::config::audio::SampleInterpolationMode::Linear;

    uint64 m_m68kCycles;    // MC68EC000 cycle counter
    uint64 m_sampleCounter; // Total number of samples

    uint32 m_lfsr; // Noise LFSR

    uint32 m_currSlot; // Current slot being processed

    std::array<sint32, 2> m_out;

    // -------------------------------------------------------------------------
    // Interrupt handling

    m68k::ExceptionVector AcknowledgeInterrupt(uint8 level);

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const cdblock::CBCDDASector CbCDDASector = util::MakeClassMemberRequiredCallback<&SCSP::ReceiveCDDA>(this);

    const sys::CBClockSpeedChange CbClockSpeedChange =
        util::MakeClassMemberRequiredCallback<&SCSP::UpdateClockRatios>(this);

public:
    // -------------------------------------------------------------------------
    // Debugger

    // Attaches the specified tracer to this component.
    // Pass nullptr to disable tracing.
    void UseTracer(debug::ISCSPTracer *tracer) {
        m_tracer = tracer;
    }

    class Probe {
    public:
        explicit Probe(SCSP &scsp);

        const std::array<Slot, 32> &GetSlots() const {
            return m_scsp.m_slots;
        }

        uint16 GetMCIEB() const {
            return m_scsp.m_scuEnabledInterrupts;
        }

        uint16 GetMCIPD() const {
            return m_scsp.m_scuPendingInterrupts;
        }

        uint16 GetSCIEB() const {
            return m_scsp.m_m68kEnabledInterrupts;
        }

        uint16 GetSCIPD() const {
            return m_scsp.m_m68kPendingInterrupts;
        }

        const std::array<uint8, 3> &GetSCILV() const {
            return m_scsp.m_m68kInterruptLevels;
        }

    private:
        SCSP &m_scsp;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    Probe m_probe{*this};
    debug::ISCSPTracer *m_tracer = nullptr;
};

} // namespace ymir::scsp
