#pragma once

/**
@file
@brief The facade of the Ymir emulator core.
Defines the `ymir::Saturn` type, the primary object used for emulating a Sega Saturn system.

See @ref index for instructions on how to use the emulator.
*/

#include <ymir/core/configuration.hpp>
#include <ymir/core/hash.hpp>
#include <ymir/core/scheduler.hpp>

#include <ymir/state/state.hpp>

#include <ymir/debug/debug_break.hpp>

#include "memory.hpp"
#include "system.hpp"
#include "system_features.hpp"

#include <ymir/hw/cart/cart.hpp>
#include <ymir/hw/cart/cart_slot.hpp>
#include <ymir/hw/cdblock/cdblock.hpp>
#include <ymir/hw/scsp/scsp.hpp>
#include <ymir/hw/scu/scu.hpp>
#include <ymir/hw/sh2/sh2.hpp>
#include <ymir/hw/smpc/smpc.hpp>
#include <ymir/hw/vdp/vdp.hpp>

#include <ymir/media/disc.hpp>

#include <memory>

namespace ymir {

/// @brief Represents an emulated Sega Saturn system.
///
/// This is the entrypoint of the emulator core. Every operation must be done through this object or any of its members.
///
/// See @ref index for details on how to use the emulator.
struct Saturn {
    /// @brief Creates a new Sega Saturn emulator reset to factory state.
    ///
    /// The emulator comes with no disc, no peripherals, no cartridge, and a basic IPL ROM that puts the master SH-2
    /// into an infinite do-nothing loop.
    Saturn();

    /// @brief Performs a soft or hard reset of the system.
    /// @param[in] hard `true` to do a hard reset, `false` for a soft reset
    void Reset(bool hard);

    /// @brief Erases SMPC settings and does a hard reset.
    void FactoryReset();

    /// @brief Retrieves the current video standard (NTSC or PAL) used by the system.
    /// @return the video standard in use
    [[nodiscard]] core::config::sys::VideoStandard GetVideoStandard() const noexcept {
        return configuration.system.videoStandard;
    }

    /// @brief Changes the video standard (NTSC or PAL) to the specified mode.
    /// @param[in] videoStandard the new video standard to use
    void SetVideoStandard(core::config::sys::VideoStandard videoStandard) {
        configuration.system.videoStandard = videoStandard;
    }

    /// @brief Retrieves the current clock speed mode (320 or 352) in use by the system.
    /// @return the current clock speed mode
    [[nodiscard]] sys::ClockSpeed GetClockSpeed() const noexcept;

    /// @brief Changes the clock speed (320 or 352) to the specified mode.
    /// @param[in] clockSpeed the new clock speed mode to use
    void SetClockSpeed(sys::ClockSpeed clockSpeed);

    /// @brief Retrieves the current clock ratios in use by the system based on the current video standard and clock
    /// speed.
    /// @return the clock ratios in use
    [[nodiscard]] const sys::ClockRatios &GetClockRatios() const noexcept;

    /// @brief Loads the specified IPL ROM image.
    /// @param[in] ipl the contents of the IPL ROM image
    void LoadIPL(std::span<uint8, sys::kIPLSize> ipl);

    /// @brief Loads the specified internal backup memory image.
    ///
    /// `error` will contain the filesystem error if the image failed to load.
    ///
    /// @param[in] path the path of the internal backup memory image to load
    /// @param[out] error receives the filesystem error in case the image fails to load
    void LoadInternalBackupMemoryImage(std::filesystem::path path, std::error_code &error);

    /// @brief Retrieves the IPL ROM hash code.
    /// @return the hash code of the currently loaded IPL ROM image
    [[nodiscard]] XXH128Hash GetIPLHash() const noexcept;

    /// @brief Retrieves the game disc image hash code.
    /// @return the hash code of the currently loaded game disc image
    [[nodiscard]] XXH128Hash GetDiscHash() const noexcept;

    /// @brief Inserts a cartridge into the cartridge slot.
    /// @tparam T the cartridge type, which must be a specialization of `ymir::cart::BaseCartridge`
    /// @tparam ...Args the types of the arguments to pass to the cartridge constructor
    /// @param[in] ...args the arguments to pass to the cartridge constructor
    /// @return a pointer to the concrete instance of the cartridge
    template <typename T, typename... Args>
        requires std::derived_from<T, cart::BaseCartridge>
    T *InsertCartridge(Args &&...args) {
        return SCU.InsertCartridge<T>(std::forward<Args>(args)...);
    }

    /// @brief Removes the cartridge from the cartridge slot.
    void RemoveCartridge() {
        SCU.RemoveCartridge();
    }

    /// @brief Returns a reference to the inserted cartridge.
    /// If no cartridge is inserted, returns a reference to `ymir::cart::NoCartridge`.
    ///
    /// @return A reference to the inserted cartridge
    [[nodiscard]] cart::BaseCartridge &GetCartridge() {
        return SCU.GetCartridge();
    }

    /// @brief Loads a disc into the CD drive.
    /// @param[in] disc the disc to be moved
    void LoadDisc(media::Disc &&disc);

    /// @brief Ejects the disc from the CD drive.
    void EjectDisc();

    /// @brief Opens the CD drive tray.
    void OpenTray();

    /// @brief Closes the CD drive tray.
    void CloseTray();

    /// @brief Determines if the CD drive tray is open or closed.
    /// @return `true` if the tray is open, `false` if closed
    [[nodiscard]] bool IsTrayOpen() const noexcept;

    /// @brief Switches the SMPC area code to the preferred region.
    void UsePreferredRegion();

    /// @brief Switches the SMPC area code to the region that best matches the given area codes, respecting the
    /// preferred region order defined in the configuration.
    /// @param[in] areaCodes the area code bitmask to base the selection on
    void AutodetectRegion(media::AreaCode areaCodes);

    /// @brief Enables or disables debug tracing on hot paths.
    ///
    /// Debug tracing is required for certain debugging features to work, such as breakpoints, watchpoints, and
    /// instruction and memory traces.
    ///
    /// Enabling this option incurs a noticeable performance penalty. It is disabled by default to ensure optimal
    /// performance when those features are not needed.
    ///
    /// Disabling debug tracing also detaches all tracers from all components.
    ///
    /// @param[in] enable whether to enable or disable debug tracing
    void EnableDebugTracing(bool enable);

    /// @brief Determines if debug tracing is enabled.
    /// @return the debug tracing state
    [[nodiscard]] bool IsDebugTracingEnabled() const noexcept {
        return m_systemFeatures.enableDebugTracing;
    }

    /// @brief Enables or disables SH-2 cache emulation.
    ///
    /// Most games work fine without this. Enable it to improve accuracy and compatibility with specific games.
    ///
    /// Enabling this option incurs a small performance penalty and purges all SH-2 caches.
    ///
    /// @param[in] enable whether to enable or disable SH-2 cache emulation
    void EnableSH2CacheEmulation(bool enable) {
        configuration.system.emulateSH2Cache = enable;
    }

    /// @brief Determines if SH-2 cache emulation is enabled.
    /// @return the SH-2 cache emulation state
    [[nodiscard]] bool IsSH2CacheEmulationEnabled() const noexcept {
        return configuration.system.emulateSH2Cache;
    }

    /// @brief Runs the emulator until the end of the current frame using the current settings.
    ///
    /// The implementation of the function depends on the following parameters:
    /// - **Debug tracing**: configured with `EnableDebugTracing(bool)`
    /// - **SH-2 cache emulation**: configured with `EnableSH2CacheEmulation(bool)`
    void RunFrame() {
        (this->*m_runFrameFn)();
    }

    /// @brief Runs a single master SH-2 instruction using the current settings.
    ///
    /// The implementation of the function depends on the following parameters:
    /// - **Debug tracing**: configured with `EnableDebugTracing(bool)`
    /// - **SH-2 cache emulation**: configured with `EnableSH2CacheEmulation(bool)`
    /// @return the number of cycles executed
    uint64 StepMasterSH2() {
        return (this->*m_stepMSH2Fn)();
    }

    /// @brief Runs a single slave SH-2 instruction using the current settings if it is enabled.
    ///
    /// The implementation of the function depends on the following parameters:
    /// - **Debug tracing**: configured with `EnableDebugTracing(bool)`
    /// - **SH-2 cache emulation**: configured with `EnableSH2CacheEmulation(bool)`
    /// @return the number of cycles executed, zero if the slave SH-2 is disabled
    uint64 StepSlaveSH2() {
        return (this->*m_stepSSH2Fn)();
    }

    /// @brief Detaches all debug tracers from all components.
    void DetachAllTracers() {
        masterSH2.UseTracer(nullptr);
        slaveSH2.UseTracer(nullptr);
        SCU.UseTracer(nullptr);
        SCSP.UseTracer(nullptr);
        CDBlock.UseTracer(nullptr);
    }

    // -------------------------------------------------------------------------
    // Configuration

    /// @brief Contains the emulator core configuration.
    core::Configuration configuration;

    // -------------------------------------------------------------------------
    // Save states

    /// @brief Saves the complete system state into the given state object.
    /// @param[out] state the state object to store into
    void SaveState(state::State &state) const;

    /// @brief Loads a complete system state from the given state object.
    ///
    /// Requires the IPL ROM and disc hashes to match. Additional filtering and validations are performed by components
    /// to ensure the state is consistent and valid.
    ///
    /// @param[in] state the state object to load from
    /// @return `true` if the state was loaded successfully
    [[nodiscard]] bool LoadState(const state::State &state);

    // -------------------------------------------------------------------------
    // Debugger

    /// @brief Sets the debug break callback to be invoked when the debug break signal is raised.
    /// @param[in] callback the debug break callback
    void SetDebugBreakRaisedCallback(debug::CBDebugBreakRaised callback) {
        m_debugBreakMgr.SetDebugBreakRaisedCallback(callback);
    }

private:
    /// @brief Runs the emulator until the end of the current frame.
    /// @tparam debug whether to use debug tracing
    /// @tparam enableSH2Cache whether to emulate SH-2 caches
    template <bool debug, bool enableSH2Cache>
    void RunFrameImpl();

    /// @brief Runs the emulator until the next scheduled event.
    /// @tparam debug whether to use debug tracing
    /// @tparam enableSH2Cache whether to emulate SH-2 caches
    /// @return true if execution should continue, false to suspend
    template <bool debug, bool enableSH2Cache>
    bool Run();

    /// @brief Runs a single master SH-2 instruction.
    /// @tparam debug whether to use debug tracing
    /// @tparam enableSH2Cache whether to emulate SH-2 caches
    /// @return the number of cycles executed
    template <bool debug, bool enableSH2Cache>
    uint64 StepMasterSH2Impl();

    /// @brief Runs a single slave SH-2 instruction if the CPU is enabled.
    /// @tparam debug whether to use debug tracing
    /// @tparam enableSH2Cache whether to emulate SH-2 caches
    /// @return the number of cycles executed, zero if the slave SH-2 is disabled
    template <bool debug, bool enableSH2Cache>
    uint64 StepSlaveSH2Impl();

    /// @brief The type of the `RunFrameImpl()` implementation to use from `RunFrame()`.
    using RunFrameFn = void (Saturn::*)();

    /// @brief The current `RunFrameImpl()` implementation in use.
    ///
    /// Depends on debug tracing and SH-2 cache emulation settings.
    RunFrameFn m_runFrameFn;

    /// @brief The type of the `StepMasterSH2Impl()` implementation to use from `StepMasterSH2()`.
    using StepSH2Fn = uint64 (Saturn::*)();

    /// @brief The current `StepMasterSH2Impl()` implementation in use.
    ///
    /// Depends on debug tracing and SH-2 cache emulation settings.
    StepSH2Fn m_stepMSH2Fn;

    /// @brief The current `StepSlaveSH2Impl()` implementation in use.
    ///
    /// Depends on debug tracing and SH-2 cache emulation settings.
    StepSH2Fn m_stepSSH2Fn;

    /// @brief Updates pointers to the execution functions based on the current debug tracing and SH-2 cache emulation
    /// settings.
    void UpdateFunctionPointers();

    // -------------------------------------------------------------------------
    // Cycle counting
    // NOTE: Scheduler must be initialized before other components as they use it to register events

    /// @brief The event scheduler.
    core::Scheduler m_scheduler;

    // -------------------------------------------------------------------------
    // Internal configuration

    /// @brief The preferred system region order to be used when auto-configuring the SMPC area code.
    std::vector<media::AreaCode> m_preferredRegionOrder;

    /// @brief Updates the preferred region order list.
    ///
    /// Registered as an observer of `ymir::core::Configuration::system::preferredRegionOrder`.
    ///
    /// @param[in] regions the new preferred region order
    void UpdatePreferredRegionOrder(std::span<const core::config::sys::Region> regions);

    /// @brief Updates the SH-2 cache emulation setting and the `RunFrameFn()` pointer.
    /// @param[in] enabled whether to enable SH-2 cache emulation
    void UpdateSH2CacheEmulation(bool enabled);

    /// @brief Updates the video standard to emulate and adjusts clock ratios across the system's components.
    /// @param[in] videoStandard the new video standard
    void UpdateVideoStandard(core::config::sys::VideoStandard videoStandard);

    // -------------------------------------------------------------------------
    // Global components and parameters

    /// @brief Global system parameters.
    sys::System m_system;

    /// @brief Global system features.
    sys::SystemFeatures m_systemFeatures;

public:
    // -------------------------------------------------------------------------
    // Components

    sys::SystemMemory mem;    ///< IPL ROM, low and high WRAM, internal backup memory
    sys::SH2Bus mainBus;      ///< Primary system bus connecting SH-2s, SCU, IPL ROM and WRAMs
    sh2::SH2 masterSH2;       ///< Master SH-2
    sh2::SH2 slaveSH2;        ///< Slave SH-2
    bool slaveSH2Enabled;     ///< Slave SH-2 enable flag
    scu::SCU SCU;             ///< SCU and its DSP, and the cartridge slot
    vdp::VDP VDP;             ///< VDP1 and VDP2
    smpc::SMPC SMPC;          ///< SMPC and input devices
    scsp::SCSP SCSP;          ///< SCSP and its DSP, and MC68EC000 CPU
    cdblock::CDBlock CDBlock; ///< CD block and media

private:
    // -------------------------------------------------------------------------
    // Internal state

    uint64 m_msh2SpilloverCycles; ///< Master SH-2 execution cycles spilled over between executions
    uint64 m_ssh2SpilloverCycles; ///< Slave SH-2 execution cycles spilled over between executions

    // -------------------------------------------------------------------------
    // System operations (SMPC) - smpc::ISMPCOperations implementation

    /// @brief SMPC operations wrapper
    struct SMPCOperations : smpc::ISMPCOperations {
        SMPCOperations(Saturn &saturn);

        bool GetNMI() const final; ///< Retrieves the NMI line state
        void RaiseNMI() final;     ///< Raises the NMI line

        void EnableAndResetSlaveSH2() final; ///< Enables and reset the slave SH-2
        void DisableSlaveSH2() final;        ///< Disables the slave SH-2

        void EnableAndResetM68K() final; ///< Enables and resets the M68K CPU
        void DisableM68K() final;        ///< Disables the M68K CPU

        void SoftResetSystem() final;      ///< Soft resets the entire system
        void ClockChangeSoftReset() final; ///< Soft resets VDP, SCU and SCSP after a clock change

        sys::ClockSpeed GetClockSpeed() const final;          ///< Retrieves the current clock speed
        void SetClockSpeed(sys::ClockSpeed clockSpeed) final; ///< Changes the current clock speed

    private:
        Saturn &m_saturn;
    };

    SMPCOperations smpcOps{*this};

    // -------------------------------------------------------------------------
    // Debugger

    debug::DebugBreakManager m_debugBreakMgr;
};

} // namespace ymir
