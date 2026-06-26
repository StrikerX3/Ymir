#include <ymir/hw/smpc/rtc.hpp>

#include <ymir/util/dev_log.hpp>
#include <ymir/util/unreachable.hpp>

#include <ymir/sys/clocks.hpp>

namespace ymir::smpc::rtc {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "RTC";
    };

} // namespace grp

RTC::RTC(core::Configuration::RTC &config)
    : m_config(config) {

    config.mode.Observe(m_mode);

    m_offset = 0;
    m_timestamp = 0;

    Reset(true);
}

void RTC::Reset(bool hard) {
    if (hard) {
        using enum core::config::rtc::HardResetStrategy;
        switch (m_config.virtHardResetStrategy) {
        case SyncToHost: m_timestamp = util::datetime::to_timestamp(util::datetime::host()); break;
        case ResetToFixedTime: m_timestamp = m_config.virtHardResetTimestamp; break;
        case Preserve: break;
        }
    }

    m_sysClockCount = 0;
}

void RTC::UpdateSysClock(uint64 sysClock) {
    const uint64 clockDelta = sysClock - m_sysClockCount;
    const uint64 seconds = clockDelta * m_sysClockRateNum / m_sysClockRateDen;
    if (seconds > 0) [[unlikely]] {
        m_sysClockCount += seconds * m_sysClockRateDen / m_sysClockRateNum;
        m_timestamp += seconds;
    }
}

util::datetime::DateTime RTC::GetDateTime() const {
    switch (m_mode) {
    case core::config::rtc::Mode::Host: return util::datetime::host(m_offset);
    case core::config::rtc::Mode::Virtual: return util::datetime::from_timestamp(m_timestamp);
    }
    util::unreachable();
}

void RTC::SetDateTime(const util::datetime::DateTime &dateTime) {
    switch (m_mode) {
    case core::config::rtc::Mode::Host:
        m_offset = util::datetime::delta_to_host(dateTime);
        devlog::debug<grp::base>("Setting host time offset to {} seconds", m_offset);
        break;
    case core::config::rtc::Mode::Virtual:
        m_timestamp = util::datetime::to_timestamp(dateTime);
        devlog::debug<grp::base>("Setting absolute timestamp to {} seconds", m_timestamp);
        break;
    }
}

void RTC::LoadPersistentData(const PersistentSMPCData::RTC &data) {
    m_offset = data.offset;
    m_timestamp = data.timestamp;
}

void RTC::SavePersistentData(PersistentSMPCData::RTC &data) const {
    data.offset = m_offset;
    data.timestamp = m_timestamp;
}

void RTC::UpdateClockRatios(const sys::ClockRatios &clockRatios) {
    m_sysClockRateNum = clockRatios.RTCNum;
    m_sysClockRateDen = clockRatios.RTCDen;
}

void RTC::SaveState(savestate::SMPCSaveState &state) const {
    state.rtcTimestamp = m_timestamp;
    state.rtcSysClockCount = m_sysClockCount;
}

bool RTC::ValidateState(const savestate::SMPCSaveState &state) const {
    return true;
}

void RTC::LoadState(const savestate::SMPCSaveState &state) {
    m_timestamp = state.rtcTimestamp;
    m_sysClockCount = state.rtcSysClockCount;
}

} // namespace ymir::smpc::rtc
