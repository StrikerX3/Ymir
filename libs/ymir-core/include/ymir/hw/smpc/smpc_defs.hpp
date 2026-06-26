#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>
#include <ymir/util/date_time.hpp>

namespace ymir::smpc {

/// @brief Persistent SMPC data.
/// Includes:
/// - Internal SMPC STE register, indicating a factory reset or dead battery
/// - Internal SMPC SMEM registers, containing system and user settings
/// - RTC time offset, applied to the host clock when using RTC in host mode
/// - RTC timestamp, applied to the virtual clock when using RTC in virtual mode
struct PersistentSMPCData {
    std::array<uint8, 4> SMEM{};
    bool STE = false;

    struct RTC {
        sint64 offset = 0;
        sint64 timestamp = util::datetime::to_timestamp(util::datetime::kDefaultDateTime);
    } rtc;
};

/// @brief Type of callback function invoked when any of the persistent SMPC settings is changed.
/// Must be registered by frontend applications to persist SMPC data to disk.
using CBPersistSMPCData = util::OptionalCallback<void(const PersistentSMPCData &data)>;

} // namespace ymir::smpc
