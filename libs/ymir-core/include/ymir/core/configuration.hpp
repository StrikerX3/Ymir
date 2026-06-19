#pragma once

/**
@file
@brief Defines `ymir::core::Configuration` for configuring the emulator core.
*/

#include "configuration_defs.hpp"

#include <ymir/util/date_time.hpp>
#include <ymir/util/observable.hpp>
#include <ymir/util/ratio.hpp>

#include <ymir/core/types.hpp>

#include <vector>

namespace ymir::core {

inline constexpr RatioU32 kMinSH2ClockRatio = RatioU32::FromPercentage(25u);
inline constexpr RatioU32 kMaxSH2ClockRatio = RatioU32::FromPercentage(10000u);

inline constexpr RatioU32 ClampSH2ClockRatio(RatioU32 ratio) {
    return std::clamp(ratio, kMinSH2ClockRatio, kMaxSH2ClockRatio);
}

/// @brief Emulator core configuration.
///
/// Thread-safety
/// -------------
/// Unless otherwise noted:
/// - Simple (primitive) types can be safely modified from any thread.
/// - Complex types (such as containers and observables) cannot be safely modified from any thread.
///
/// If you plan to run the emulator core in a dedicated thread, make sure to modify non-thread-safe values exclusively
/// on that thread. You may add observers to observable values (both functions and value references), but be aware that
/// the functions will also run on the emulator thread.
struct Configuration {
    /// @brief System configuration.
    struct System {
        /// @brief Automatically change SMPC area code based on compatible regions from loaded discs.
        bool autodetectRegion = true;

        /// @brief Preferred region order when autodetecting area codes.
        ///
        /// If none of these regions is supported by the disc, the first region listed on the disc is used.
        util::Observable<std::vector<config::sys::Region>> preferredRegionOrder =
            std::vector<config::sys::Region>{config::sys::Region::NorthAmerica, config::sys::Region::Japan};

        /// @brief Specifies the video standard for the system, which affects video timings and clock rates.
        util::Observable<config::sys::VideoStandard> videoStandard = config::sys::VideoStandard::NTSC;

        /// @brief Enables debug tracing.
        ///
        /// When enabled, the emulator executes an alternative code path with all debug functions enabled, incurring a
        /// noticeable performance penalty.
        util::Observable<bool> debugTracing = false;

        /// @brief Enables SH-2 cache emulation.
        ///
        /// Most games work fine without this. Enable it to improve accuracy and compatibility with specific games.
        ///
        /// Enabling this option incurs a small performance penalty and purges all SH-2 caches.
        util::Observable<bool> emulateSH2Cache = false;

        /// @brief SH-2 clock factor ratio.
        ///
        /// Adjusts the cycle rate of the SH-2 CPUs, which may reduce internal slowdowns and lag in CPU-heavy games.
        /// Decreasing the factor will improve emulation performance but may cause slowdowns.
        ///
        /// Changing it either way from 100% may lower compatibility with some games.
        ///
        /// The ratio is clamped to the range [25%..10000%]. The SH2 starts to choke on interrupts if it runs too
        /// slowly and, while going faster technically is feasible, a 2.8 GHz SH2 is already too much to emulate, let
        /// alone two of them.
        util::Observable<RatioU32, ClampSH2ClockRatio> sh2ClockFactor = RatioU32::FromPercentage(100u);
    } system;

    /// @brief RTC configuration
    struct RTC {
        /// @brief The RTC emulation mode.
        ///
        /// This value is thread-safe.
        util::Observable<config::rtc::Mode> mode = config::rtc::Mode::Host;

        /// @brief The virtual RTC hard reset strategy.
        config::rtc::HardResetStrategy virtHardResetStrategy = config::rtc::HardResetStrategy::Preserve;

        /// @brief The virtual RTC hard reset timestamp.
        sint64 virtHardResetTimestamp = util::datetime::to_timestamp(
            util::datetime::DateTime{.year = 1994, .month = 1, .day = 1, .hour = 0, .minute = 0, .second = 0});
    } rtc;

    /// @brief VDP1, VDP2 and video rendering configuration.
    struct Video {
        // TODO: renderer backend options

        /// @brief Runs the VDP1 renderer in a dedicated thread.
        util::Observable<bool> threadedVDP1 = true;

        /// @brief Runs the VDP2 renderer in a dedicated thread.
        util::Observable<bool> threadedVDP2 = true;

        /// @brief Runs the VDP2 deinterlacer in a dedicated thread, if the VDP2 renderer is running in a thread.
        util::Observable<bool> threadedDeinterlacer = true;
    } video;

    /// @brief SCSP and audio rendering configuration.
    struct Audio {
        /// @brief Sample interpolation method.
        ///
        /// The Sega Saturn uses linear interpolation.
        ///
        /// This value is thread-safe.
        util::Observable<config::audio::SampleInterpolationMode> interpolation =
            config::audio::SampleInterpolationMode::Linear;

        /// @brief Runs the SCSP and MC68EC000 CPU in a dedicated thread.
        ///
        /// Currently unimplemented.
        util::Observable<bool> threadedSCSP = false;
    } audio;

    /// @brief CD Block configuration.
    struct CDBlock {
        /// @brief Read speed factor for high-speed mode.
        ///
        /// Accepted values range from 2 to 200.
        /// The default is 2, matching the real Saturn CD drive's speed.
        ///
        /// This value is thread-safe.
        util::Observable<uint8> readSpeedFactor = 2;

        /// @brief Use CD block low-level emulation.
        ///
        /// Requires the CD block ROM to be loaded.
        ///
        /// Causes a hard reset when changed.
        util::Observable<bool> useLLE = false;
    } cdblock;

    /// @brief Notifies all observers registered with all observables.
    ///
    /// This is useful if you wish to apply the default values instead of replacing them with a configuration system.
    void NotifyObservers();
};

} // namespace ymir::core
