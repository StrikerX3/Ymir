#pragma once

/**
@file
@brief Emulator core configuration definitions.
*/

namespace ymir::core::config {

namespace sys {
    /// @brief System regions
    enum class Region {
        Japan = 0x1,        ///< (J) Domestic NTSC - Japan
        AsiaNTSC = 0x2,     ///< (T) Asia NTSC - Asia Region (Taiwan, Philippines, South Korea)
        NorthAmerica = 0x4, ///< (U) North America NTSC - North America (US, Canada), Latin America (Brazil only)
        EuropePAL = 0xC,    ///< (E) PAL - Europe, Southeast Asia (China, Middle East), Latin America

        CentralSouthAmericaNTSC = 0x5, ///< (B) (obsolete -> U) Central/South America NTSC
        Korea = 0x6,                   ///< (K) (obsolete -> T) South Korea
        AsiaPAL = 0xA,                 ///< (A) (obsolete -> E) Asia PAL
        CentralSouthAmericaPAL = 0xD,  ///< (L) (obsolete -> E) Central/South America PAL
    };

    enum class VideoStandard { NTSC, PAL };
} // namespace sys

namespace rtc {
    /// @brief RTC emulation modes
    enum class Mode {
        /// @brief Syncs RTC to host clock. Uses an offset to adjust time.
        Host,

        /// @brief Emulates RTC time, syncing to the main bus clock.
        ///
        /// Behavior on hard reset/power on can be configured to one of:
        /// - Resync to host clock
        /// - Resync to a fixed time point (for deterministic behavior)
        /// - Preserve current time
        Virtual,
    };

    /// @brief Emulated RTC behavior on hard reset.
    enum class HardResetStrategy {
        /// @brief Sync emulated RTC to host clock.
        SyncToHost,

        /// @brief Reset emulated RTC to a fixed timestamp. Useful for TAS since it has deterministic behavior.
        ResetToFixedTime,

        /// @brief Preserve current RTC timestamp.
        Preserve,
    };
} // namespace rtc

namespace hw_vdp {
    /// @brief VDP1 VRAM write synchronization intervals.
    enum class VDP1VRAMSyncInterval {
        Command, //< Synchronizes VRAM writes before running each VDP1 command
        Draw,    //< Synchronizes VRAM writes at the start of a VDP1 draw sequence
        Swap,    //< Synchronizes VRAM writes on VDP1 framebuffer swap
    };

    /// @brief VDP2 VRAM write synchronization intervals.
    enum class VDP2VRAMSyncInterval {
        Scanline, //< Synchronizes VRAM writes after processing each VDP2 scanline
        Frame,    //< Synchronizes VRAM writes at the end of a VDP2 frame
    };
} // namespace hw_vdp

namespace audio {
    /// @brief Sample interpolation modes.
    enum class SampleInterpolationMode {
        /// @brief Reuses the last sample until the next sample is read.
        ///
        /// Harshest option. Introduces a lot of aliasing.
        NearestNeighbor,

        /// @brief Interpolates linearly between two consecutive samples.
        ///
        /// Cleaner, with little aliasing. Used by the real SCSP.
        Linear
    };
} // namespace audio

} // namespace ymir::core::config
