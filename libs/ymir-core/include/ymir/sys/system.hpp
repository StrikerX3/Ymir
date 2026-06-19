#pragma once

#include "clocks.hpp"

#include "system_internal_callbacks.hpp"

#include <ymir/savestate/savestate_system.hpp>

#include <vector>

namespace ymir::sys {

struct System {
    core::config::sys::VideoStandard videoStandard = core::config::sys::VideoStandard::NTSC;
    ClockSpeed clockSpeed = ClockSpeed::_320;
    RatioU32 sh2ClockFactor = RatioU32::One();

    const ClockRatios &GetClockRatios() const {
        return m_activeClockRatios;
    }

    void UpdateClockRatios() {
        const bool clock352 = clockSpeed == ClockSpeed::_352;
        const bool pal = videoStandard == core::config::sys::VideoStandard::PAL;
        const ClockRatios &baseRatios = kClockRatios[(pal << 1) | (clock352 << 0)];

        m_activeClockRatios = baseRatios;
        if (sh2ClockFactor != RatioU32::One()) {
            const auto [num, den] = sh2ClockFactor.Pair();
            m_activeClockRatios.SCSPNum *= den;
            m_activeClockRatios.SCSPDen *= num;

            m_activeClockRatios.CDBlockNum *= den;
            m_activeClockRatios.CDBlockDen *= num;

            m_activeClockRatios.SMPCNum *= den;
            m_activeClockRatios.SMPCDen *= num;

            m_activeClockRatios.RTCNum *= den;
            m_activeClockRatios.RTCDen *= num;
        }

        for (auto &cb : m_clockSpeedChangeCallbacks) {
            cb(m_activeClockRatios);
        }
    }

    void AddClockSpeedChangeCallback(CBClockSpeedChange callback) {
        m_clockSpeedChangeCallbacks.push_back(callback);
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::SystemSaveState &state) const {
        state.videoStandard = videoStandard;
        state.clockSpeed = clockSpeed;
    }

    [[nodiscard]] bool ValidateState(const savestate::SystemSaveState &state) const {
        if (state.videoStandard != core::config::sys::VideoStandard::NTSC &&
            state.videoStandard != core::config::sys::VideoStandard::PAL) {
            return false;
        }
        if (state.clockSpeed != ClockSpeed::_320 && state.clockSpeed != ClockSpeed::_352) {
            return false;
        }
        return true;
    }

    void LoadState(const savestate::SystemSaveState &state) {
        videoStandard = state.videoStandard;
        clockSpeed = state.clockSpeed;

        UpdateClockRatios();
    }

private:
    ClockRatios m_activeClockRatios = kClockRatios[0];
    std::vector<CBClockSpeedChange> m_clockSpeedChangeCallbacks;
};

} // namespace ymir::sys
