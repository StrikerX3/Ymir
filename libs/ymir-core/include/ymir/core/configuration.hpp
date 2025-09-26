#pragma once

/**
@file
@brief Defines `ymir::core::Configuration` for configuring the emulator core.
*/

#include "configuration_defs.hpp"

#include <ymir/sys/clocks.hpp>

#include <ymir/util/date_time.hpp>
#include <ymir/util/observable.hpp>

#include <ymir/core/types.hpp>

#include <vector>

namespace ymir::core {

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

        /// @brief Enables SH-2 cache emulation.
        ///
        /// Most games work fine without this. Enable it to improve accuracy and compatibility with specific games.
        ///
        /// Enabling this option incurs a small performance penalty and purges all SH-2 caches.
        util::Observable<bool> emulateSH2Cache = false;
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

        /// @brief Runs the VDP2 renderer in a dedicated thread.
        util::Observable<bool> threadedVDP = true;

        /// @brief Runs the VDP2 deinterlacer in a dedicated thread, if the VDP2 renderer is running in a thread.
        util::Observable<bool> threadedDeinterlacer = true;

        /// @brief Render VDP1 in the dedicated VDP2 rendering thread if that is enabled.
        /// Lowers compatibility in exchange for performance.
        /// Some games stop working when this option is enabled.
        util::Observable<bool> includeVDP1InRenderThread = false;
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
