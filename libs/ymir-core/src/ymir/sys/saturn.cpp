#include <ymir/sys/saturn.hpp>

#include <ymir/util/dev_log.hpp>

#include <bit>
#include <cassert>

namespace ymir {

namespace static_config {

    // Reduces timeslices to the minimum possible -- one MSH2 instruction at a time.
    // Maximizes component synchronization at a massive cost to performance.
    static constexpr bool max_timing_granularity = false;

    // Enables CD Block LLE emulation.
    // Currently experimental, non-working, and introduces a noticeable performance penalty.
    static constexpr bool use_cdblock_lle = true;

} // namespace static_config

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // system
    // bus

    struct system {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "System";
    };

    struct bus {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "Bus";
    };

} // namespace grp

Saturn::Saturn()
    : masterSH2(m_scheduler, mainBus, true, m_systemFeatures)
    , slaveSH2(m_scheduler, mainBus, false, m_systemFeatures)
    , SCU(m_scheduler, mainBus)
    , VDP(m_scheduler, configuration)
    , SMPC(m_scheduler, smpcOps, configuration.rtc)
    , SCSP(m_scheduler, configuration.audio)
    , CDBlock(m_scheduler, configuration.cdblock)
    , SH1(m_scheduler, SH1Bus)
    , CDDrive(m_scheduler) {

    mainBus.MapNormal(
        0x000'0000, 0x7FF'FFFF, nullptr,
        [](uint32 address, void *) -> uint8 {
            devlog::debug<grp::bus>("Unhandled 8-bit main bus read from {:07X}", address);
            return 0;
        },
        [](uint32 address, void *) -> uint16 {
            devlog::debug<grp::bus>("Unhandled 16-bit main bus read from {:07X}", address);
            return 0;
        },
        [](uint32 address, void *) -> uint32 {
            devlog::debug<grp::bus>("Unhandled 32-bit main bus read from {:07X}", address);
            return 0;
        },
        [](uint32 address, uint8 value, void *) {
            devlog::debug<grp::bus>("Unhandled 8-bit main bus write to {:07X} = {:02X}", address, value);
        },
        [](uint32 address, uint16 value, void *) {
            devlog::debug<grp::bus>("Unhandled 16-bit main bus write to {:07X} = {:04X}", address, value);
        },
        [](uint32 address, uint32 value, void *) {
            devlog::debug<grp::bus>("Unhandled 32-bit main bus write to {:07X} = {:07X}", address, value);
        });

    if constexpr (static_config::use_cdblock_lle) {
        SH1Bus.MapNormal(
            0x000'0000, 0xFFF'FFFF, nullptr,
            [](uint32 address, void *) -> uint8 {
                devlog::debug<grp::bus>("Unhandled 8-bit main bus read from {:07X}\n", address);
                return 0;
            },
            [](uint32 address, void *) -> uint16 {
                devlog::debug<grp::bus>("Unhandled 16-bit main bus read from {:07X}\n", address);
                return 0;
            },
            [](uint32 address, void *) -> uint32 {
                devlog::debug<grp::bus>("Unhandled 32-bit main bus read from {:07X}\n", address);
                return 0;
            },
            [](uint32 address, uint8 value, void *) {
                devlog::debug<grp::bus>("Unhandled 8-bit main bus write to {:07X} = {:02X}\n", address, value);
            },
            [](uint32 address, uint16 value, void *) {
                devlog::debug<grp::bus>("Unhandled 16-bit main bus write to {:07X} = {:04X}\n", address, value);
            },
            [](uint32 address, uint32 value, void *) {
                devlog::debug<grp::bus>("Unhandled 32-bit main bus write to {:07X} = {:08X}\n", address, value);
            });

        SH1Bus.MapArray(0x1000000, 0x1FFFFFF, CDBlockDRAM, true);
        SH1Bus.MapArray(0x9000000, 0x9FFFFFF, CDBlockDRAM, true);
    }

    masterSH2.MapCallbacks(SCU.CbAckExtIntr);
    // Slave SH2 IVECF# pin is not connected, so the external interrupt vector fetch callback shouldn't be mapped
    SCU.MapCallbacks(masterSH2.CbExtIntr, slaveSH2.CbExtIntr);
    VDP.MapCallbacks(SCU.CbHBlankStateChange, SCU.CbVBlankStateChange, SCU.CbTriggerSpriteDrawEnd,
                     SMPC.CbTriggerOptimizedINTBACKRead, SMPC.CbTriggerVBlankIN);
    SMPC.MapCallbacks(SCU.CbTriggerSystemManager, SCU.CbTriggerPad);
    SCSP.MapCallbacks(SCU.CbTriggerSoundRequest);
    if constexpr (static_config::use_cdblock_lle) {
        SH1.SetSCI0Callbacks(CDDrive.CbSerialRx, CDDrive.CbSerialTx);
        CDDrive.MapCallbacks(SH1.CbSetCOMSYNCn, SH1.CbSetCOMREQn, YGR.CbDiscChanged, SH1.CbCDBDataSector,
                             SCSP.CbCDDASector, YGR.CbSectorTransferDone);
        YGR.MapCallbacks(SH1.CbAssertIRQ6, SH1.CbAssertIRQ7, SH1.CbSetDREQ0n, SH1.CbSetDREQ1n, SCU.CbTriggerExtIntr0);
    } else {
        CDBlock.MapCallbacks(SCU.CbTriggerExtIntr0, SCSP.CbCDDASector);
    }

    m_system.AddClockSpeedChangeCallback(SCSP.CbClockSpeedChange);
    m_system.AddClockSpeedChangeCallback(SMPC.CbClockSpeedChange);
    if constexpr (static_config::use_cdblock_lle) {
        m_system.AddClockSpeedChangeCallback(CDDrive.CbClockSpeedChange);
    } else {
        m_system.AddClockSpeedChangeCallback(CDBlock.CbClockSpeedChange);
    }

    masterSH2.UseDebugBreakManager(&m_debugBreakMgr);
    slaveSH2.UseDebugBreakManager(&m_debugBreakMgr);

    mem.MapMemory(mainBus);
    masterSH2.MapMemory(mainBus);
    slaveSH2.MapMemory(mainBus);
    SCU.MapMemory(mainBus);
    VDP.MapMemory(mainBus);
    SMPC.MapMemory(mainBus);
    SCSP.MapMemory(mainBus);
    if constexpr (static_config::use_cdblock_lle) {
        YGR.MapMemory(mainBus, SH1Bus);
    } else {
        CDBlock.MapMemory(mainBus);
    }

    m_systemFeatures.enableDebugTracing = false;
    m_systemFeatures.emulateSH2Cache = false;
    UpdateFunctionPointers();

    configuration.system.preferredRegionOrder.Observe(
        [&](const std::vector<core::config::sys::Region> &regions) { UpdatePreferredRegionOrder(regions); });
    configuration.system.emulateSH2Cache.Observe([&](bool enabled) { UpdateSH2CacheEmulation(enabled); });
    configuration.system.videoStandard.Observe(
        [&](core::config::sys::VideoStandard videoStandard) { UpdateVideoStandard(videoStandard); });

    Reset(true);
}

void Saturn::Reset(bool hard) {
    m_system.clockSpeed = sys::ClockSpeed::_320;
    m_system.UpdateClockRatios();

    if (hard) {
        m_scheduler.Reset();
    }

    masterSH2.Reset(hard);
    slaveSH2.Reset(hard);
    slaveSH2Enabled = false;
    m_msh2SpilloverCycles = 0;
    m_ssh2SpilloverCycles = 0;
    m_sh1SpilloverCycles = 0;

    SCU.Reset(hard);
    VDP.Reset(hard);
    SMPC.Reset(hard);
    SCSP.Reset(hard);
    if constexpr (static_config::use_cdblock_lle) {
        SH1.Reset(hard);
        if (hard) {
            YGR.Reset();
        }
        CDDrive.Reset();
    } else {
        CDBlock.Reset(hard);
    }
}

void Saturn::FactoryReset() {
    SMPC.FactoryReset();
    Reset(true);
}

sys::ClockSpeed Saturn::GetClockSpeed() const noexcept {
    return m_system.clockSpeed;
}

void Saturn::SetClockSpeed(sys::ClockSpeed clockSpeed) {
    m_system.clockSpeed = clockSpeed;
    m_system.UpdateClockRatios();
}

const sys::ClockRatios &Saturn::GetClockRatios() const noexcept {
    return m_system.GetClockRatios();
}

void Saturn::LoadIPL(std::span<uint8, sys::kIPLSize> ipl) {
    mem.LoadIPL(ipl);
}

void Saturn::LoadCDBlockROM(std::span<uint8, sh1::kROMSize> rom) {
    SH1.LoadROM(rom);
}

void Saturn::LoadInternalBackupMemoryImage(std::filesystem::path path, std::error_code &error) {
    mem.LoadInternalBackupMemoryImage(path, error);
}

XXH128Hash Saturn::GetIPLHash() const noexcept {
    return mem.GetIPLHash();
}

XXH128Hash Saturn::GetDiscHash() const noexcept {
    if constexpr (static_config::use_cdblock_lle) {
        return CDDrive.GetDiscHash();
    } else {
        return CDBlock.GetDiscHash();
    }
}

void Saturn::LoadDisc(media::Disc &&disc) {
    // Configure area code based on compatible area codes from the disc
    AutodetectRegion(disc.header.compatAreaCode);
    if constexpr (static_config::use_cdblock_lle) {
        CDDrive.LoadDisc(std::move(disc));
    } else {
        CDBlock.LoadDisc(std::move(disc));
    }
}

void Saturn::EjectDisc() {
    if constexpr (static_config::use_cdblock_lle) {
        CDDrive.EjectDisc();
    } else {
        CDBlock.EjectDisc();
    }
}

void Saturn::OpenTray() {
    if constexpr (static_config::use_cdblock_lle) {
        CDDrive.OpenTray();
    } else {
        CDBlock.OpenTray();
    }
}

void Saturn::CloseTray() {
    if constexpr (static_config::use_cdblock_lle) {
        CDDrive.CloseTray();
    } else {
        CDBlock.CloseTray();
    }
}

bool Saturn::IsTrayOpen() const noexcept {
    if constexpr (static_config::use_cdblock_lle) {
        return CDDrive.IsTrayOpen();
    } else {
        return CDBlock.IsTrayOpen();
    }
}

void Saturn::UsePreferredRegion() {
    if (m_preferredRegionOrder.empty()) {
        return;
    }

    // Pick the first available preferred region
    const uint8 areaCode = std::countr_zero(static_cast<uint16>(m_preferredRegionOrder.front()));

    // Apply configuration and hard reset system if changed
    const uint8 currAreaCode = SMPC.GetAreaCode();
    SMPC.SetAreaCode(areaCode);
    if (areaCode != currAreaCode) {
        Reset(true);
    }
}

void Saturn::AutodetectRegion(media::AreaCode areaCodes) {
    if (!configuration.system.autodetectRegion) {
        return;
    }
    if (areaCodes == media::AreaCode::None) {
        return;
    }

    const uint8 currAreaCode = SMPC.GetAreaCode();

    // The area code enum is a bitmap where each bit corresponds to an SMPC area code
    const auto areaCodeVal = static_cast<uint16>(areaCodes);

    // Pick from the preferred list if possible or use the first one found
    uint8 selectedAreaCode = std::countr_zero<uint16>(areaCodeVal & -areaCodeVal);
    for (auto areaCode : m_preferredRegionOrder) {
        if (BitmaskEnum(areaCodes).AnyOf(areaCode)) {
            selectedAreaCode = std::countr_zero(static_cast<uint16>(areaCode));
            break;
        }
    }

    // Apply configuration and hard reset system if changed
    SMPC.SetAreaCode(selectedAreaCode);

    // Also change PAL/NTSC setting accordingly
    switch (selectedAreaCode) {
    case 0x1: [[fallthrough]];
    case 0x2: [[fallthrough]];
    case 0x4: [[fallthrough]];
    case 0x5: [[fallthrough]];
    case 0x6: SetVideoStandard(core::config::sys::VideoStandard::NTSC); break;

    case 0xA: [[fallthrough]];
    case 0xC: [[fallthrough]];
    case 0xD: SetVideoStandard(core::config::sys::VideoStandard::PAL); break;
    }

    if (currAreaCode != selectedAreaCode) {
        Reset(true);
    }
}

void Saturn::EnableDebugTracing(bool enable) {
    if (m_systemFeatures.enableDebugTracing && !enable) {
        DetachAllTracers();
    }
    m_systemFeatures.enableDebugTracing = enable;
    UpdateFunctionPointers();
    SCSP.SetDebugTracing(enable);
}

void Saturn::SaveState(state::State &state) const {
    m_scheduler.SaveState(state.scheduler);
    m_system.SaveState(state.system);
    mem.SaveState(state.system);
    state.system.slaveSH2Enabled = slaveSH2Enabled;
    state.msh2SpilloverCycles = m_msh2SpilloverCycles;
    state.ssh2SpilloverCycles = m_ssh2SpilloverCycles;
    masterSH2.SaveState(state.msh2);
    slaveSH2.SaveState(state.ssh2);
    SCU.SaveState(state.scu);
    SMPC.SaveState(state.smpc);
    VDP.SaveState(state.vdp);
    SCSP.SaveState(state.scsp);
    CDBlock.SaveState(state.cdblock);
}

bool Saturn::LoadState(const state::State &state) {
    if (!m_scheduler.ValidateState(state.scheduler)) {
        return false;
    }
    if (!m_system.ValidateState(state.system)) {
        return false;
    }
    if (!mem.ValidateState(state.system)) {
        return false;
    }
    if (!masterSH2.ValidateState(state.msh2)) {
        return false;
    }
    if (!slaveSH2.ValidateState(state.ssh2)) {
        return false;
    }
    if (!SCU.ValidateState(state.scu)) {
        return false;
    }
    if (!SMPC.ValidateState(state.smpc)) {
        return false;
    }
    if (!VDP.ValidateState(state.vdp)) {
        return false;
    }
    if (!SCSP.ValidateState(state.scsp)) {
        return false;
    }
    if (!CDBlock.ValidateState(state.cdblock)) {
        return false;
    }

    m_scheduler.LoadState(state.scheduler);
    m_system.LoadState(state.system);
    mem.LoadState(state.system);
    slaveSH2Enabled = state.system.slaveSH2Enabled;
    m_msh2SpilloverCycles = state.msh2SpilloverCycles;
    m_ssh2SpilloverCycles = state.ssh2SpilloverCycles;
    masterSH2.LoadState(state.msh2);
    slaveSH2.LoadState(state.ssh2);
    SCU.LoadState(state.scu);
    SMPC.LoadState(state.smpc);
    VDP.LoadState(state.vdp);
    SCSP.LoadState(state.scsp);
    CDBlock.LoadState(state.cdblock);

    return true;
}

void Saturn::DumpCDBlockDRAM(std::ostream &out) {
    out.write((const char *)CDBlockDRAM.data(), CDBlockDRAM.size());
}

// Run scenarios:
// [x] Run a full frame -- RunFrameImpl()
// [x] Run until next event -- Run()
// [ ] Run for a number of cycles
// [ ] Run until an event from a selection of events is triggered (or a frame is completed, whichever happens first)
//     [ ] On any schedulable event or a subset of them
//     [ ] A breakpoint or watchpoint is triggered
//     [ ] Slave SH-2 is enabled
//     [ ] M68K is enabled
//     [ ] SCU DSP starts running
//     [ ] If any debug tracers ask to suspend emulation (when supported)
// [x] Single-step master SH-2 -- StepMasterSH2()
// [x] Single-step slave SH-2 (if enabled) -- StepSlaveSH2()
// [ ] Single-step M68K (if enabled)
// [ ] Single-step SCU DSP (if running)
// [ ] Single-step SCSP DSP
// [ ] Single-step CD Block SH-1
// Note:
// - Step out/return can be implemented in terms of single-stepping and instruction tracing events

template <bool debug, bool enableSH2Cache>
void Saturn::RunFrameImpl() {
    // Use the last line phase as reference to give some leeway if we overshoot the target cycles
    while (VDP.InLastLinePhase()) {
        if (!Run<debug, enableSH2Cache>()) {
            return;
        }
    }
    while (!VDP.InLastLinePhase()) {
        if (!Run<debug, enableSH2Cache>()) {
            return;
        }
    }
}

template <bool debug, bool enableSH2Cache>
bool Saturn::Run() {
    static constexpr uint64 kSH2SyncMaxStep = 32;

    const uint64 cycles = static_config::max_timing_granularity ? 1 : std::max<sint64>(m_scheduler.RemainingCount(), 0);

    uint64 execCycles = m_msh2SpilloverCycles;
    m_msh2SpilloverCycles = 0;
    if (slaveSH2Enabled) {
        uint64 slaveCycles = m_ssh2SpilloverCycles;
        do {
            const uint64 prevExecCycles = execCycles;
            const uint64 targetCycles = std::min(execCycles + kSH2SyncMaxStep, cycles);
            execCycles = masterSH2.Advance<debug, enableSH2Cache>(targetCycles, execCycles);
            slaveCycles = slaveSH2.Advance<debug, enableSH2Cache>(execCycles, slaveCycles);
            SCU.Advance<debug>(execCycles - prevExecCycles);
            if constexpr (debug) {
                if (m_debugBreakMgr.IsDebugBreakRaised()) {
                    break;
                }
            }
        } while (execCycles < cycles);
        if constexpr (debug) {
            // If the SSH2 hits a breakpoint early, the cycle count may be shorter than the total executed cycles.
            if (slaveCycles > execCycles) {
                m_ssh2SpilloverCycles = slaveCycles - execCycles;
            } else {
                m_msh2SpilloverCycles = execCycles - slaveCycles;
            }
        } else {
            m_ssh2SpilloverCycles = slaveCycles - execCycles;
        }
    } else {
        do {
            const uint64 prevExecCycles = execCycles;
            const uint64 targetCycles = std::min(execCycles + kSH2SyncMaxStep, cycles);
            execCycles = masterSH2.Advance<debug, enableSH2Cache>(targetCycles, execCycles);
            SCU.Advance<debug>(execCycles - prevExecCycles);
            if constexpr (debug) {
                if (m_debugBreakMgr.IsDebugBreakRaised()) {
                    break;
                }
            }
        } while (execCycles < cycles);
    }
    VDP.Advance<debug>(execCycles);

    // SCSP+M68K and CD block are ticked by the scheduler

    if constexpr (static_config::use_cdblock_lle) {
        // Advance SH-1
        const auto &clockRatios = GetClockRatios();
        const uint64 sh1CycleCount = execCycles * clockRatios.CDBlockNum / clockRatios.CDBlockDen;
        if (sh1CycleCount > 0) {
            const uint64 sh1Cycles = SH1.Advance(sh1CycleCount, m_sh1SpilloverCycles);
            m_sh1SpilloverCycles = sh1Cycles - sh1CycleCount;
        }

        // CD drive is ticked by the scheduler
    }

    // TODO: advance SMPC
    /*m_smpcCycles += execCycles * 2464;
    const uint64 smpcCycleCount = m_smpcCycles / 17640;
    if (smpcCycleCount > 0) {
        m_smpcCycles -= smpcCycleCount * 17640;
        SMPC.Advance<debug>(smpcCycleCount);
    }*/

    m_scheduler.Advance(execCycles);

    if constexpr (debug) {
        if (m_debugBreakMgr.LowerDebugBreak()) {
            return false;
        }
    }

    return true;
}

template <bool debug, bool enableSH2Cache>
uint64 Saturn::StepMasterSH2Impl() {
    uint64 masterCycles = masterSH2.Step<debug, enableSH2Cache>();
    if (masterCycles >= m_msh2SpilloverCycles) {
        masterCycles -= m_msh2SpilloverCycles;
        m_msh2SpilloverCycles = 0;
        if (slaveSH2Enabled) {
            const uint64 slaveCycles = slaveSH2.Advance<debug, enableSH2Cache>(masterCycles, m_ssh2SpilloverCycles);
            m_ssh2SpilloverCycles = slaveCycles - masterCycles;
        }
        SCU.Advance<debug>(masterCycles);
        VDP.Advance<debug>(masterCycles);

        // SCSP+M68K and CD block are ticked by the scheduler

        if constexpr (static_config::use_cdblock_lle) {
            // Advance SH-1
            const auto &clockRatios = GetClockRatios();
            const uint64 sh1Cycles = masterCycles * clockRatios.CDBlockNum / clockRatios.CDBlockDen;
            if (sh1Cycles > 0) {
                const uint64 execCycles = SH1.Advance(sh1Cycles, m_sh1SpilloverCycles);
                m_sh1SpilloverCycles = execCycles - sh1Cycles;
            }

            // CD drive is ticked by the scheduler
        }

        // TODO: advance SMPC
        /*m_smpcCycles += masterCycles * 2464;
        const uint64 smpcCycleCount = m_smpcCycles / 17640;
        if (smpcCycleCount > 0) {
            m_smpcCycles -= smpcCycleCount * 17640;
            SMPC.Advance<debug>(smpcCycleCount);
        }*/

        m_scheduler.Advance(masterCycles);
    } else {
        m_msh2SpilloverCycles -= masterCycles;
    }
    return masterCycles;
}

template <bool debug, bool enableSH2Cache>
uint64 Saturn::StepSlaveSH2Impl() {
    if (!slaveSH2Enabled) {
        return 0;
    }

    uint64 slaveCycles = slaveSH2.Step<debug, enableSH2Cache>();
    if (slaveCycles >= m_ssh2SpilloverCycles) {
        slaveCycles -= m_ssh2SpilloverCycles;
        m_ssh2SpilloverCycles = 0;
        const uint64 masterCycles = masterSH2.Advance<debug, enableSH2Cache>(slaveCycles, m_msh2SpilloverCycles);
        m_msh2SpilloverCycles = masterCycles - slaveCycles;
        SCU.Advance<debug>(slaveCycles);
        VDP.Advance<debug>(slaveCycles);

        // SCSP+M68K and CD block are ticked by the scheduler

        if constexpr (static_config::use_cdblock_lle) {
            // Advance SH-1
            const auto &clockRatios = GetClockRatios();
            const uint64 sh1Cycles = slaveCycles * clockRatios.CDBlockNum / clockRatios.CDBlockDen;
            if (sh1Cycles > 0) {
                const uint64 execCycles = SH1.Advance(sh1Cycles, m_sh1SpilloverCycles);
                m_sh1SpilloverCycles = execCycles - sh1Cycles;
            }

            // CD drive is ticked by the scheduler
        }

        // TODO: advance SMPC
        /*m_smpcCycles += slaveCycles * 2464;
        const uint64 smpcCycleCount = m_smpcCycles / 17640;
        if (smpcCycleCount > 0) {
            m_smpcCycles -= smpcCycleCount * 17640;
            SMPC.Advance<debug>(smpcCycleCount);
        }*/

        m_scheduler.Advance(slaveCycles);
    } else {
        m_ssh2SpilloverCycles -= slaveCycles;
    }
    return slaveCycles;
}

void Saturn::UpdateFunctionPointers() {
    m_runFrameFn = m_systemFeatures.enableDebugTracing
                       ? (m_systemFeatures.emulateSH2Cache ? &Saturn::RunFrameImpl<true, true>
                                                           : &Saturn::RunFrameImpl<true, false>)
                       : (m_systemFeatures.emulateSH2Cache ? &Saturn::RunFrameImpl<false, true>
                                                           : &Saturn::RunFrameImpl<false, false>);

    m_stepMSH2Fn = m_systemFeatures.enableDebugTracing
                       ? (m_systemFeatures.emulateSH2Cache ? &Saturn::StepMasterSH2Impl<true, true>
                                                           : &Saturn::StepMasterSH2Impl<true, false>)
                       : (m_systemFeatures.emulateSH2Cache ? &Saturn::StepMasterSH2Impl<false, true>
                                                           : &Saturn::StepMasterSH2Impl<false, false>);

    m_stepSSH2Fn = m_systemFeatures.enableDebugTracing
                       ? (m_systemFeatures.emulateSH2Cache ? &Saturn::StepSlaveSH2Impl<true, true>
                                                           : &Saturn::StepSlaveSH2Impl<true, false>)
                       : (m_systemFeatures.emulateSH2Cache ? &Saturn::StepSlaveSH2Impl<false, true>
                                                           : &Saturn::StepSlaveSH2Impl<false, false>);
}

void Saturn::UpdatePreferredRegionOrder(std::span<const core::config::sys::Region> regions) {
    m_preferredRegionOrder.clear();
    media::AreaCode usedAreaCodes = media::AreaCode::None;
    auto addAreaCode = [&](media::AreaCode areaCode) {
        if (BitmaskEnum(usedAreaCodes).NoneOf(areaCode)) {
            usedAreaCodes |= areaCode;
            m_preferredRegionOrder.push_back(areaCode);
        }
    };

    using Region = core::config::sys::Region;
    for (const Region region : regions) {
        switch (region) {
        case Region::Japan: addAreaCode(media::AreaCode::Japan); break;
        case Region::AsiaNTSC: addAreaCode(media::AreaCode::AsiaNTSC); break;
        case Region::NorthAmerica: addAreaCode(media::AreaCode::NorthAmerica); break;
        case Region::CentralSouthAmericaNTSC: addAreaCode(media::AreaCode::CentralSouthAmericaNTSC); break;
        case Region::Korea: addAreaCode(media::AreaCode::Korea); break;
        case Region::AsiaPAL: addAreaCode(media::AreaCode::AsiaPAL); break;
        case Region::EuropePAL: addAreaCode(media::AreaCode::EuropePAL); break;
        case Region::CentralSouthAmericaPAL: addAreaCode(media::AreaCode::CentralSouthAmericaPAL); break;
        }
    }
}

void Saturn::UpdateSH2CacheEmulation(bool enabled) {
    if (!m_systemFeatures.emulateSH2Cache && enabled) {
        masterSH2.PurgeCache();
        slaveSH2.PurgeCache();
    }
    m_systemFeatures.emulateSH2Cache = enabled;
    UpdateFunctionPointers();
}

void Saturn::UpdateVideoStandard(core::config::sys::VideoStandard videoStandard) {
    m_system.videoStandard = videoStandard;
    m_system.UpdateClockRatios();
}

// -----------------------------------------------------------------------------
// System operations (SMPC) - smpc::ISMPCOperations implementation

Saturn::SMPCOperations::SMPCOperations(Saturn &saturn)
    : m_saturn(saturn) {}

bool Saturn::SMPCOperations::GetNMI() const {
    return m_saturn.masterSH2.GetNMI();
}

void Saturn::SMPCOperations::RaiseNMI() {
    m_saturn.masterSH2.SetNMI();
}

void Saturn::SMPCOperations::EnableAndResetSlaveSH2() {
    m_saturn.slaveSH2Enabled = true;
    m_saturn.slaveSH2.Reset(true);
}

void Saturn::SMPCOperations::DisableSlaveSH2() {
    m_saturn.slaveSH2Enabled = false;
}

void Saturn::SMPCOperations::EnableAndResetM68K() {
    m_saturn.SCSP.SetCPUEnabled(true);
}

void Saturn::SMPCOperations::DisableM68K() {
    m_saturn.SCSP.SetCPUEnabled(false);
}

void Saturn::SMPCOperations::SoftResetSystem() {
    m_saturn.Reset(false);
}

void Saturn::SMPCOperations::ClockChangeSoftReset() {
    m_saturn.VDP.Reset(false);
    m_saturn.SCU.Reset(false);
    m_saturn.SCSP.Reset(false);
}

sys::ClockSpeed Saturn::SMPCOperations::GetClockSpeed() const {
    return m_saturn.GetClockSpeed();
}

void Saturn::SMPCOperations::SetClockSpeed(sys::ClockSpeed clockSpeed) {
    m_saturn.SetClockSpeed(clockSpeed);
}

} // namespace ymir
