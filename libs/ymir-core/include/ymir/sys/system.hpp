#pragma once

#include "clocks.hpp"

#include "system_internal_callbacks.hpp"

#include <ymir/savestate/savestate_system.hpp>

#include <numeric>
#include <vector>

namespace ymir::sys {

struct System {
    core::config::sys::VideoStandard videoStandard = core::config::sys::VideoStandard::NTSC;
    ClockSpeed clockSpeed = ClockSpeed::_320;
    uint32 sh2ClockFactor = 100;

    const ClockRatios &GetClockRatios() const {
        return m_activeClockRatios;
    }

    void UpdateClockRatios() {
        const bool clock352 = clockSpeed == ClockSpeed::_352;
        const bool pal = videoStandard == core::config::sys::VideoStandard::PAL;
        const ClockRatios &baseRatios = kClockRatios[(pal << 1) | (clock352 << 0)];

        m_activeClockRatios = baseRatios;
        if (sh2ClockFactor != 100) {
            const uint64 gcd = std::gcd(100ull, sh2ClockFactor);
            const uint64 numFactor = 100ull / gcd;
            const uint64 denFactor = sh2ClockFactor / gcd;

            m_activeClockRatios.SCSPNum *= numFactor;
            m_activeClockRatios.SCSPDen *= denFactor;

            m_activeClockRatios.CDBlockNum *= numFactor;
            m_activeClockRatios.CDBlockDen *= denFactor;

            m_activeClockRatios.SMPCNum *= numFactor;
            m_activeClockRatios.SMPCDen *= denFactor;

            m_activeClockRatios.RTCNum *= numFactor;
            m_activeClockRatios.RTCDen *= denFactor;
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
