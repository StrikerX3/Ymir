#include <ymir/hw/cdblock/cdblock.hpp>

#include "cdblock_devlog.hpp"

#include <ymir/sys/clocks.hpp>

#include <ymir/util/arith_ops.hpp>

#include <cassert>
#include <utility>

namespace ymir::cdblock {

CDBlock::CDBlock(core::Scheduler &scheduler, core::Configuration::CDBlock &config)
    : m_scheduler(scheduler) {

    m_driveStateUpdateEvent = m_scheduler.RegisterEvent(core::events::CDBlockDriveState, this, OnDriveStateUpdateEvent);
    m_commandExecEvent = m_scheduler.RegisterEvent(core::events::CDBlockCommand, this, OnCommandExecEvent);

    config.readSpeedFactor.Observe([&](uint8 factor) {
        m_readSpeedFactor = std::clamp<uint8>(factor, 2u, 200u);
        if (m_readSpeed > 1) {
            m_readSpeed = m_readSpeedFactor;
            devlog::info<grp::base>("Read speed set to {}x", m_readSpeed);
        }
    });
    m_readSpeedFactor = 2;

    Reset(true);
}

void CDBlock::Reset(bool hard) {
    m_CR[0] = 0x0043; // ' C'
    m_CR[1] = 0x4442; // 'DB'
    m_CR[2] = 0x4C4F; // 'LO'
    m_CR[3] = 0x434B; // 'CK'

    m_status.statusCode = kStatusCodePause;
    m_status.frameAddress = 0xFFFFFF;
    m_status.flags = 0xF;
    m_status.repeatCount = 0xF;
    m_status.controlADR = 0xFF;
    m_status.track = 0xFF;
    m_status.index = 0xFF;

    m_readyForPeriodicReports = false;

    m_currDriveCycles = 0;
    m_targetDriveCycles = kDriveCyclesNotPlaying;
    m_scheduler.ScheduleFromNow(m_driveStateUpdateEvent, 0);

    m_playStartParam = 0;
    m_playEndParam = 0;
    m_playRepeatParam = 0;
    m_scanDirection = false;
    m_scanCounter = 0;

    m_playStartPos = 0;
    m_playEndPos = 0;
    m_playMaxRepeat = 0;
    m_playFile = false;
    m_bufferFullPause = false;

    m_readSpeed = 1;

    m_discAuthStatus = 0;
    m_mpegAuthStatus = 0;

    if (hard) {
        m_HIRQ = 0x0BC1; // 0x0BE1;
        m_HIRQMASK = 0;
    } else {
        m_HIRQ &= kHIRQ_mask & ~kHIRQ_DCHG;
    }

    m_xferType = TransferType::None;
    m_xferPos = 0;
    m_xferLength = 0;
    m_xferCount = 0xFFFFFF;
    m_xferBuffer.fill(0xFFFF);
    m_xferBufferPos = 0;

    m_xferSubcodeFrameAddress = 0;
    m_xferSubcodeGroup = 0;

    m_partitionManager.Reset();

    for (int i = 0; auto &filter : m_filters) {
        filter.Reset();
        filter.trueOutput = i;
        i++;
    }
    m_cdDeviceConnection = Filter::kDisconnected;
    m_lastCDWritePartition = ~0;

    m_calculatedPartitionSize = 0;

    m_getSectorLength = 2048;
    m_putSectorLength = 2048;

    m_processingCommand = false;
}

void CDBlock::MapMemory(sys::Bus &bus) {
    // CD Block registers are mirrored every 64 bytes in a 4 KiB block.
    // These 4 KiB blocks are mapped every 32 KiB.

    static constexpr auto cast = [](void *ctx) -> CDBlock & { return *static_cast<CDBlock *>(ctx); };

    for (uint32 address = 0x580'0000; address <= 0x58F'FFFF; address += 0x8000) {
        bus.MapNormal(
            address, address + 0xFFF, this,
            [](uint32 address, void *ctx) -> uint8 { return cast(ctx).ReadReg<uint8>(address); },
            [](uint32 address, void *ctx) -> uint16 { return cast(ctx).ReadReg<uint16>(address); },
            [](uint32 address, void *ctx) -> uint32 {
                uint32 value = cast(ctx).ReadReg<uint16>(address + 0) << 16u;
                value |= cast(ctx).ReadReg<uint16>(address + 2) << 0u;
                return value;
            },
            [](uint32 address, uint8 value, void *ctx) { cast(ctx).WriteReg<uint8>(address, value); },
            [](uint32 address, uint16 value, void *ctx) { cast(ctx).WriteReg<uint16>(address, value); },
            [](uint32 address, uint32 value, void *ctx) {
                cast(ctx).WriteReg<uint16>(address + 0, value >> 16u);
                cast(ctx).WriteReg<uint16>(address + 2, value >> 0u);
            });

        bus.MapSideEffectFree(
            address, address + 0xFFF, this,
            [](uint32 address, void *ctx) -> uint8 { return cast(ctx).PeekReg<uint8>(address); },
            [](uint32 address, void *ctx) -> uint16 { return cast(ctx).PeekReg<uint16>(address); },
            [](uint32 address, void *ctx) -> uint32 { return cast(ctx).PeekReg<uint32>(address); },
            [](uint32 address, uint8 value, void *ctx) { cast(ctx).PokeReg<uint8>(address, value); },
            [](uint32 address, uint16 value, void *ctx) { cast(ctx).PokeReg<uint16>(address, value); },
            [](uint32 address, uint32 value, void *ctx) { cast(ctx).PokeReg<uint32>(address, value); });
    }
}

void CDBlock::UpdateClockRatios(const sys::ClockRatios &clockRatios) {
    // FIXME: audio track playback is too slow with the CD block ratio
    m_scheduler.SetEventCountFactor(m_driveStateUpdateEvent, clockRatios.SCSPNum * 3, clockRatios.SCSPDen);
    // m_scheduler.SetEventCountFactor(m_driveStateUpdateEvent, clockRatios.CDBlockNum * 3, clockRatios.CDBlockDen);
    m_scheduler.SetEventCountFactor(m_commandExecEvent, clockRatios.CDBlockNum, clockRatios.CDBlockDen);
}

void CDBlock::LoadDisc(media::Disc &&disc) {
    m_disc.Swap(std::move(disc));

    const uint8 status = m_status.statusCode & 0xF;
    if (status == kStatusCodeNoDisc || status == kStatusCodeOpen || status == kStatusCodePlay ||
        status == kStatusCodeSeek || status == kStatusCodeBusy) {
        // TODO: stay in Busy status while disc is read
        m_status.statusCode = kStatusCodePause;
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        m_discAuthStatus = 0;
    }
    SetInterrupt(kHIRQ_DCHG | kHIRQ_EFLS);

    // Try building filesystem structure
    if (m_fs.Read(m_disc)) {
        devlog::info<grp::base>("Filesystem built successfully");
    } else {
        devlog::warn<grp::base>("Failed to build filesystem");
    }
}

void CDBlock::EjectDisc() {
    if (!m_disc.sessions.empty()) {
        m_disc = {};

        m_status.statusCode = kStatusCodeNoDisc;
        m_status.frameAddress = 0xFFFFFF;
        m_status.flags = 0xF;
        m_status.repeatCount = 0xF;
        m_status.controlADR = 0xFF;
        m_status.track = 0xFF;
        m_status.index = 0xFF;

        m_fs.Clear();
        SetInterrupt(kHIRQ_DCHG | kHIRQ_EFLS);

        devlog::debug<grp::base>("Ejected disc");
    }
}

void CDBlock::OpenTray() {
    if ((m_status.statusCode & 0xF) != kStatusCodeOpen) {
        // TODO: stay in Busy status while disc stops spinning
        m_status.statusCode = kStatusCodeOpen;
        m_discAuthStatus = 0;

        m_fs.Clear();
        SetInterrupt(kHIRQ_DCHG | kHIRQ_EFLS);

        devlog::info<grp::base>("Tray opened");
    } else {
        devlog::info<grp::base>("Tried to open tray when it's already opened");
    }
}

void CDBlock::CloseTray() {
    if ((m_status.statusCode & 0xF) == kStatusCodeOpen) {
        // TODO: stay in Busy status while drive scans disc
        if (m_disc.sessions.empty()) {
            m_status.statusCode = kStatusCodeNoDisc;
            m_targetDriveCycles = kDriveCyclesNotPlaying;

            devlog::info<grp::base>("Tray closed - no disc");
        } else {
            m_status.statusCode = kStatusCodePause;
            m_targetDriveCycles = kDriveCyclesNotPlaying;

            devlog::info<grp::base>("Tray closed - paused");
        }
    } else {
        devlog::info<grp::base>("Tried to close tray when it's already closed");
    }
}

bool CDBlock::IsTrayOpen() const {
    return (m_status.statusCode & 0xF) == kStatusCodeOpen;
}

XXH128Hash CDBlock::GetDiscHash() const {
    return m_fs.GetHash();
}

void CDBlock::SaveState(state::CDBlockState &state) const {
    state.discHash = m_fs.GetHash();

    state.CR = m_CR;
    state.HIRQ = m_HIRQ;
    state.HIRQMASK = m_HIRQMASK;

    state.status.statusCode = m_status.statusCode;
    state.status.frameAddress = m_status.frameAddress;
    state.status.flags = m_status.flags;
    state.status.repeatCount = m_status.repeatCount;
    state.status.controlADR = m_status.controlADR;
    state.status.track = m_status.track;
    state.status.index = m_status.index;

    state.readyForPeriodicReports = m_readyForPeriodicReports;

    state.currDriveCycles = m_currDriveCycles;
    state.targetDriveCycles = m_targetDriveCycles;

    state.playStartParam = m_playStartParam;
    state.playEndParam = m_playEndParam;
    state.playRepeatParam = m_playRepeatParam;
    state.scanDirection = m_scanDirection;
    state.scanCounter = m_scanCounter;

    state.playStartPos = m_playStartPos;
    state.playEndPos = m_playEndPos;
    state.playMaxRepeat = m_playMaxRepeat;
    state.playFile = m_playFile;
    state.bufferFullPause = m_bufferFullPause;

    state.readSpeed = m_readSpeed;

    state.discAuthStatus = m_discAuthStatus;

    state.mpegAuthStatus = m_mpegAuthStatus;

    switch (m_xferType) {
    case TransferType::None: state.xferType = state::CDBlockState::TransferType::None; break;
    case TransferType::TOC: state.xferType = state::CDBlockState::TransferType::TOC; break;
    case TransferType::GetSector: state.xferType = state::CDBlockState::TransferType::GetSector; break;
    case TransferType::GetThenDeleteSector:
        state.xferType = state::CDBlockState::TransferType::GetThenDeleteSector;
        break;
    case TransferType::FileInfo: state.xferType = state::CDBlockState::TransferType::FileInfo; break;
    case TransferType::Subcode: state.xferType = state::CDBlockState::TransferType::Subcode; break;
    }

    state.xferPos = m_xferPos;
    state.xferLength = m_xferLength;
    state.xferCount = m_xferCount;
    state.xferBuffer = m_xferBuffer;
    state.xferBufferPos = m_xferBufferPos;

    state.xferSectorPos = m_xferSectorPos;
    state.xferSectorEnd = m_xferSectorEnd;
    state.xferPartition = m_xferPartition;

    state.xferSubcodeFrameAddress = m_xferSubcodeFrameAddress;
    state.xferSubcodeGroup = m_xferSubcodeGroup;

    state.xferExtraCount = m_xferExtraCount;

    for (auto &buffer : state.buffers) {
        buffer.data.fill(0);
        buffer.size = 0;
        buffer.frameAddress = 0;
        buffer.fileNum = 0;
        buffer.chanNum = 0;
        buffer.submode = 0;
        buffer.codingInfo = 0;
        buffer.partitionIndex = 0xFF;
    }
    state.scratchBuffer.data = m_scratchBuffer.data;
    state.scratchBuffer.size = m_scratchBuffer.size;
    state.scratchBuffer.frameAddress = m_scratchBuffer.frameAddress;
    state.scratchBuffer.fileNum = m_scratchBuffer.subheader.fileNum;
    state.scratchBuffer.chanNum = m_scratchBuffer.subheader.chanNum;
    state.scratchBuffer.submode = m_scratchBuffer.subheader.submode;
    state.scratchBuffer.codingInfo = m_scratchBuffer.subheader.codingInfo;

    m_partitionManager.SaveState(state);

    for (size_t i = 0; i < kNumFilters; i++) {
        state.filters[i].startFrameAddress = m_filters[i].startFrameAddress;
        state.filters[i].frameAddressCount = m_filters[i].frameAddressCount;

        state.filters[i].mode = m_filters[i].mode;

        state.filters[i].fileNum = m_filters[i].fileNum;
        state.filters[i].chanNum = m_filters[i].chanNum;

        state.filters[i].submodeMask = m_filters[i].submodeMask;
        state.filters[i].submodeValue = m_filters[i].submodeValue;

        state.filters[i].codingInfoMask = m_filters[i].codingInfoMask;
        state.filters[i].codingInfoValue = m_filters[i].codingInfoValue;

        state.filters[i].trueOutput = m_filters[i].trueOutput;
        state.filters[i].falseOutput = m_filters[i].falseOutput;
    }

    state.cdDeviceConnection = m_cdDeviceConnection;
    state.lastCDWritePartition = m_lastCDWritePartition;

    state.calculatedPartitionSize = m_calculatedPartitionSize;

    state.getSectorLength = m_getSectorLength;
    state.putSectorLength = m_putSectorLength;

    state.processingCommand = m_processingCommand;
}

bool CDBlock::ValidateState(const state::CDBlockState &state) const {
    if (state.discHash != m_fs.GetHash()) {
        return false;
    }
    if (!m_partitionManager.ValidateState(state)) {
        return false;
    }
    return true;
}

void CDBlock::LoadState(const state::CDBlockState &state) {
    m_CR = state.CR;
    m_HIRQ = state.HIRQ;
    m_HIRQMASK = state.HIRQMASK;

    m_status.statusCode = state.status.statusCode;
    m_status.frameAddress = state.status.frameAddress;
    m_status.flags = state.status.flags;
    m_status.repeatCount = state.status.repeatCount;
    m_status.controlADR = state.status.controlADR;
    m_status.track = state.status.track;
    m_status.index = state.status.index;

    m_readyForPeriodicReports = state.readyForPeriodicReports;

    m_currDriveCycles = state.currDriveCycles;
    m_targetDriveCycles = state.targetDriveCycles;

    m_playStartParam = state.playStartParam;
    m_playEndParam = state.playEndParam;
    m_playRepeatParam = state.playRepeatParam;
    m_scanDirection = state.scanDirection;
    m_scanCounter = state.scanCounter;

    m_playStartPos = state.playStartPos;
    m_playEndPos = state.playEndPos;
    m_playMaxRepeat = state.playMaxRepeat;
    m_playFile = state.playFile;
    m_bufferFullPause = state.bufferFullPause;

    m_readSpeed = state.readSpeed;

    m_discAuthStatus = state.discAuthStatus;

    m_mpegAuthStatus = state.mpegAuthStatus;

    switch (state.xferType) {
    case state::CDBlockState::TransferType::None: m_xferType = TransferType::None; break;
    case state::CDBlockState::TransferType::TOC: m_xferType = TransferType::TOC; break;
    case state::CDBlockState::TransferType::GetSector: m_xferType = TransferType::GetSector; break;
    case state::CDBlockState::TransferType::GetThenDeleteSector: m_xferType = TransferType::GetThenDeleteSector; break;
    case state::CDBlockState::TransferType::FileInfo: m_xferType = TransferType::FileInfo; break;
    case state::CDBlockState::TransferType::Subcode: m_xferType = TransferType::Subcode; break;
    }

    m_xferPos = state.xferPos;
    m_xferLength = state.xferLength;
    m_xferCount = state.xferCount;
    m_xferBuffer = state.xferBuffer;
    m_xferBufferPos = state.xferBufferPos;

    m_xferSectorPos = state.xferSectorPos;
    m_xferSectorEnd = state.xferSectorEnd;
    m_xferPartition = state.xferPartition;

    m_xferSubcodeFrameAddress = state.xferSubcodeFrameAddress;
    m_xferSubcodeGroup = state.xferSubcodeGroup;

    m_xferExtraCount = state.xferExtraCount;

    m_scratchBuffer.data = state.scratchBuffer.data;
    m_scratchBuffer.size = state.scratchBuffer.size;
    m_scratchBuffer.frameAddress = state.scratchBuffer.frameAddress;
    m_scratchBuffer.subheader.fileNum = state.scratchBuffer.fileNum;
    m_scratchBuffer.subheader.chanNum = state.scratchBuffer.chanNum;
    m_scratchBuffer.subheader.submode = state.scratchBuffer.submode;
    m_scratchBuffer.subheader.codingInfo = state.scratchBuffer.codingInfo;

    m_partitionManager.LoadState(state);

    for (size_t i = 0; i < kNumFilters; i++) {
        m_filters[i].startFrameAddress = state.filters[i].startFrameAddress;
        m_filters[i].frameAddressCount = state.filters[i].frameAddressCount;

        m_filters[i].mode = state.filters[i].mode;

        m_filters[i].fileNum = state.filters[i].fileNum;
        m_filters[i].chanNum = state.filters[i].chanNum;

        m_filters[i].submodeMask = state.filters[i].submodeMask;
        m_filters[i].submodeValue = state.filters[i].submodeValue;

        m_filters[i].codingInfoMask = state.filters[i].codingInfoMask;
        m_filters[i].codingInfoValue = state.filters[i].codingInfoValue;

        m_filters[i].trueOutput = state.filters[i].trueOutput;
        m_filters[i].falseOutput = state.filters[i].falseOutput;
    }

    m_cdDeviceConnection = state.cdDeviceConnection;
    m_lastCDWritePartition = state.lastCDWritePartition;

    m_calculatedPartitionSize = state.calculatedPartitionSize;

    m_getSectorLength = state.getSectorLength;
    m_putSectorLength = state.putSectorLength;

    m_processingCommand = state.processingCommand;
}

void CDBlock::OnDriveStateUpdateEvent(core::EventContext &eventContext, void *userContext) {
    auto &cdb = *static_cast<CDBlock *>(userContext);
    cdb.ProcessDriveState();
    eventContext.RescheduleFromNow(cdb.m_targetDriveCycles);
}

void CDBlock::OnCommandExecEvent(core::EventContext &eventContext, void *userContext) {
    auto &cdb = *static_cast<CDBlock *>(userContext);
    cdb.ProcessCommand();
}

template <mem_primitive T>
T CDBlock::ReadReg(uint32 address) {
    address &= 0x3F;

    switch (address) {
    case 0x00: return DoReadTransfer();
    case 0x02: return DoReadTransfer();
    case 0x08: return m_HIRQ;
    case 0x0C: return m_HIRQMASK;
    case 0x18: return m_CR[0];
    case 0x1C: return m_CR[1];
    case 0x20: return m_CR[2];
    case 0x24:
        m_processingCommand = false;
        m_readyForPeriodicReports = true;
        return m_CR[3];
    default: devlog::debug<grp::regs>("Unhandled {}-bit register read from {:02X}", sizeof(T) * 8, address); return 0;
    }
}

template <mem_primitive T>
void CDBlock::WriteReg(uint32 address, T value) {
    address &= 0x3F;

    devlog::trace<grp::regs>("{}-bit register write to {:02X} = {:X}", sizeof(T) * 8, address, value);
    switch (address) {
    case 0x00: DoWriteTransfer(value); break;
    case 0x02: DoWriteTransfer(value); break;
    case 0x08:
        m_HIRQ &= value;
        UpdateInterrupts();
        break;
    case 0x0C:
        m_HIRQMASK = value;
        UpdateInterrupts();
        break;
    case 0x18:
        m_processingCommand = true;
        m_status.statusCode &= ~kStatusFlagPeriodic;
        m_CR[0] = value;
        break;
    case 0x1C: m_CR[1] = value; break;
    case 0x20: m_CR[2] = value; break;
    case 0x24:
        m_CR[3] = value;
        SetupCommand();
        break;

    default:
        devlog::debug<grp::regs>("Unhandled {}-bit register write to {:02X} = {:X}", sizeof(T) * 8, address, value);
        break;
    }
}

template <mem_primitive T>
T CDBlock::PeekReg(uint32 address) {
    if constexpr (std::is_same_v<T, uint8>) {
        return PeekReg<uint16>(address & ~1) >> ((~address & 1u) * 8u);
    } else if constexpr (std::is_same_v<T, uint32>) {
        uint32 value = PeekReg<uint16>((address & ~1) | 0) << 16u;
        value |= PeekReg<uint16>((address & ~1) | 2) << 0u;
        return value;
    } else if constexpr (std::is_same_v<T, uint16>) {
        address &= 0x3F;

        switch (address) {
        case 0x00: return m_xferBuffer[m_xferBufferPos];
        case 0x02: return m_xferBuffer[m_xferBufferPos];
        case 0x08: return m_HIRQ;
        case 0x0C: return m_HIRQMASK;
        case 0x18: return m_CR[0];
        case 0x1C: return m_CR[1];
        case 0x20: return m_CR[2];
        case 0x24: return m_CR[3];
        default: return 0;
        }
    }
}

template <mem_primitive T>
void CDBlock::PokeReg(uint32 address, T value) {
    if constexpr (std::is_same_v<T, uint8>) {
        PokeReg<uint16>(address & ~1, value << ((~address & 1) * 8));
    } else if constexpr (std::is_same_v<T, uint32>) {
        PokeReg<uint16>((address & ~1) | 0, value >> 16u);
        PokeReg<uint16>((address & ~1) | 2, value >> 0u);
    } else if constexpr (std::is_same_v<T, uint16>) {
        address &= 0x3F;

        switch (address) {
        case 0x00: m_xferBuffer[m_xferBufferPos] = value; break;
        case 0x02: m_xferBuffer[m_xferBufferPos] = value; break;
        case 0x08: m_HIRQ = value; break;
        case 0x0C: m_HIRQMASK = value; break;
        case 0x18: m_CR[0] = value; break;
        case 0x1C: m_CR[1] = value; break;
        case 0x20: m_CR[2] = value; break;
        case 0x24: m_CR[3] = value; break;
        }
    }
}

bool CDBlock::SetupGenericPlayback(uint32 startParam, uint32 endParam, uint16 repeatParam) {
    // Handle "no change" parameters
    if (startParam == 0xFFFFFF || endParam == 0xFFFFFF || repeatParam == 0xFF) {
        // "No change" must be specified on all parameters at once, and is only valid while paused
        if (startParam == 0xFFFFFF && endParam == 0xFFFFFF && repeatParam == 0xFF) {
            if ((m_status.statusCode & 0xF) == kStatusCodePause || (m_status.statusCode & 0xF) == kStatusCodeScan) {
                m_status.statusCode = kStatusCodePlay;
                return true;
            }
        }
        return false;
    }

    const bool isStartFAD = bit::test<23>(startParam);
    const bool isEndFAD = bit::test<23>(endParam);
    const bool resetPos = bit::test<15>(repeatParam);

    // Sanity check: both must be FADs or tracks, not a mix
    if (isStartFAD != isEndFAD) {
        devlog::debug<grp::play_init>("Start/End FAD type mismatch: {:06X} {:06X}", startParam, endParam);
        return false; // reject
    }

    // Store playback parameters
    m_playStartParam = startParam;
    m_playEndParam = endParam;
    m_playRepeatParam = repeatParam & 0xF;
    m_playMaxRepeat = m_playRepeatParam;
    m_playFile = false;

    // Make sure we have a disc
    if (m_disc.sessions.empty()) {
        devlog::info<grp::play_init>("No disc");
        m_status.statusCode = kStatusCodeNoDisc;
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        return true;
    }

    const media::Session &session = m_disc.sessions.back();

    if (isStartFAD) {
        // Frame address range
        m_playStartPos = startParam & 0x7FFFFF;
        m_playEndPos = m_playStartPos + (endParam & 0x7FFFFF) - 1;

        devlog::debug<grp::play_init>("FAD range {:06X} to {:06X}", m_playStartPos, m_playEndPos);

        uint32 frameAddress = m_status.frameAddress;
        if (resetPos || frameAddress < m_playStartPos || frameAddress > m_playEndPos) {
            frameAddress = m_playStartPos;
        }

        // Find track containing the requested start frame address
        const uint8 trackIndex = session.FindTrackIndex(frameAddress);
        if (trackIndex != 0xFF) {
            const uint8 index = session.tracks[trackIndex].FindIndex(frameAddress);
            m_status.statusCode = kStatusCodeSeek;
            m_status.flags = 0x8;     // CD-ROM decoding flag
            m_status.repeatCount = 0; // first repeat
            m_status.controlADR = session.tracks[trackIndex].controlADR;
            m_status.track = trackIndex + 1;
            m_status.index = index;

            // TODO: delay seek for a realistic amount of time
            if (m_status.controlADR == 0x41) {
                m_targetDriveCycles = kDriveCyclesPlaying1x / m_readSpeed;
            } else {
                // Force 1x speed if playing audio track
                m_targetDriveCycles = kDriveCyclesPlaying1x;
            }

            devlog::debug<grp::play_init>("Track:Index {:02d}:{:02d} ctl/ADR={:02X}", m_status.track, m_status.index,
                                          m_status.controlADR);

            if (resetPos) {
                m_status.frameAddress = m_playStartPos;
                devlog::debug<grp::play_init>("Reset playback position to {:06X}", m_status.frameAddress);
            } else {
                m_status.frameAddress = frameAddress;
            }
        } else {
            m_targetDriveCycles = kDriveCyclesNotPlaying;
            m_status.statusCode = kStatusCodePause;
        }
    } else {
        // Track range

        // startParam and endParam contain the track number on the upper byte and index on the lower byte
        uint8 startTrack = bit::extract<8, 15>(startParam);
        uint8 startIndex = bit::extract<0, 7>(startParam);
        uint8 endTrack = bit::extract<8, 15>(endParam);
        uint8 endIndex = bit::extract<0, 7>(endParam);

        // Handle default parameters - use first or last track and index in the disc
        if (startParam == 0) {
            startTrack = session.firstTrackIndex + 1;
            startIndex = 1;
        }
        if (endParam == 0) {
            endTrack = session.lastTrackIndex + 1;
            endIndex = session.tracks[session.lastTrackIndex].indices.size() + 1;
        }

        devlog::debug<grp::play_init>("Track:Index range {:02d}:{:02d}-{:02d}:{:02d} ", startTrack, startIndex,
                                      endTrack, endIndex);

        // Clamp track numbers to what's available in the disc
        // If end < start, ProcessDriveState() will switch to the Pause state automatically
        uint8 firstTrack = session.firstTrackIndex + 1;
        uint8 lastTrack = session.lastTrackIndex + 1;
        startTrack = std::clamp(startTrack, firstTrack, lastTrack);
        endTrack = std::clamp(endTrack, firstTrack, lastTrack);
        startIndex = std::clamp<uint8>(startIndex, 1, session.tracks[startTrack - 1].indices.size() + 1);
        endIndex = std::clamp<uint8>(endIndex, 1, session.tracks[endTrack - 1].indices.size() + 1);
        devlog::debug<grp::play_init>("Track:Index range after clamping {:02d}:{:02d}-{:02d}:{:02d}", startTrack,
                                      startIndex, endTrack, endIndex);

        // Play frame address range for the specified tracks
        m_playStartPos = session.tracks[startTrack - 1].indices[startIndex - 1].startFrameAddress;
        m_playEndPos = session.tracks[endTrack - 1].indices[endIndex - 1].endFrameAddress;

        uint32 frameAddress = m_status.frameAddress;
        if (resetPos || frameAddress < m_playStartPos || frameAddress > m_playEndPos) {
            frameAddress = m_playStartPos;
        }

        const media::Track *track = session.FindTrack(frameAddress);

        if (track != nullptr) [[likely]] {
            // Switch to seek mode
            m_status.statusCode = kStatusCodeSeek;
            m_status.repeatCount = 0; // first repeat
            m_status.controlADR = track->controlADR;
            m_status.track = track->index;
            m_status.index = track->FindIndex(frameAddress);
            m_status.flags = m_status.controlADR == 0x01 ? 0x8 : 0x0;

            // TODO: delay seek for a realistic amount of time
            if (m_status.controlADR == 0x41) {
                m_targetDriveCycles = kDriveCyclesPlaying1x / m_readSpeed;
            } else {
                // Force 1x speed if playing audio track
                m_targetDriveCycles = kDriveCyclesPlaying1x;
            }
            devlog::debug<grp::play_init>("Track:Index FAD range {:06X}-{:06X}", m_playStartPos, m_playEndPos);

            if (resetPos) {
                m_status.frameAddress = m_playStartPos;
                devlog::debug<grp::play_init>("Reset playback position to {:06X}", m_status.frameAddress);
            } else {
                m_status.frameAddress = frameAddress;
            }
        } else {
            // The disc image is truncated or corrupted
            // Let's pretend this is a disc read error
            // TODO: what happens on a real disc read error?
            devlog::debug<grp::play>("Could not find track - disc image is truncated or corrupted");
            return false;
        }
    }

    return true;
}

bool CDBlock::SetupFilePlayback(uint32 fileID, uint32 offset, uint8 filterNumber) {
    // Bail out if there is no file system
    if (!m_fs.IsValid()) {
        devlog::debug<grp::play_init>("No file system; rejecting playback request");
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        m_status.statusCode = kStatusCodePause;
        return true;
    }

    // Bail out if there is no current directory
    if (!m_fs.HasCurrentDirectory()) {
        devlog::debug<grp::play_init>("No current directory set; rejecting playback request");
        m_targetDriveCycles = kDriveCyclesNotPlaying;
        m_status.statusCode = kStatusCodePause;
        return true;
    }

    // Reject if the file ID is out of range
    const media::fs::FileInfo &fileInfo = m_fs.GetFileInfo(fileID);
    if (!fileInfo.IsValid()) {
        devlog::debug<grp::play_init>("Invalid file ID; rejecting playback request");
        return false;
    }

    // Reject if frame address doesn't point to a valid data track
    const uint32 fileOffset = fileInfo.frameAddress + offset;
    const auto &session = m_disc.sessions.back();
    const uint8 trackIndex = session.FindTrackIndex(fileOffset);
    if (trackIndex == 0xFF) {
        devlog::debug<grp::play_init>("Track not found for frame address {:06X}; rejecting playback request",
                                      fileOffset);
        return false;
    }
    if (session.tracks[trackIndex].controlADR != 0x41) {
        devlog::debug<grp::play_init>("Not a data track at frame address {:06X}; rejecting playback request",
                                      fileOffset);
        return false;
    }

    // Determine starting and ending frame addresses and other parameters for "playback"
    m_playStartPos = fileInfo.frameAddress + offset;
    m_playEndPos = fileInfo.frameAddress + (fileInfo.fileSize + 2047) / 2048 - offset - 1;
    m_playMaxRepeat = 0;
    m_playFile = true;

    // Connect CD device to specified filter
    ConnectCDDevice(filterNumber);

    // Setup status
    m_status.statusCode = kStatusCodeSeek;
    m_status.flags = 0x8;     // CD-ROM decoding flag
    m_status.repeatCount = 0; // first repeat
    m_status.controlADR = session.tracks[trackIndex].controlADR;
    m_status.track = trackIndex + 1;
    m_status.index = 1;

    devlog::debug<grp::play_init>("Read file {}, offset {}, filter {}, frame addresses {:06X} to {:06X}", fileID,
                                  offset, filterNumber, m_playStartPos, m_playEndPos);
    return true;
}

bool CDBlock::SetupScan(uint8 direction) {
    if (direction >= 2) {
        return false;
    }

    m_scanDirection = direction;
    m_scanCounter = 0;

    m_status.statusCode = kStatusCodeScan;

    devlog::info<grp::base>("Scan disc {}", (m_scanDirection ? "backward" : "forward"));

    return true;
}

void CDBlock::ProcessDriveState() {
    switch (m_status.statusCode & 0xF) {
    case kStatusCodeSeek:
        if (m_status.controlADR == 0x41) {
            m_targetDriveCycles = kDriveCyclesPlaying1x / m_readSpeed;
        } else {
            // Force 1x speed if playing audio track
            m_targetDriveCycles = kDriveCyclesPlaying1x;
        }
        m_status.statusCode = kStatusCodePlay;
        if (m_status.frameAddress < m_playStartPos || m_status.frameAddress > m_playEndPos) {
            m_status.frameAddress = m_playStartPos;
        }
        break;
    case kStatusCodePlay: [[fallthrough]];
    case kStatusCodeScan: ProcessDriveStatePlay(); break;
    case kStatusCodePause:
        // Resume playback if paused due to running out of buffers
        if (m_bufferFullPause && m_partitionManager.GetFreeBufferCount() > 0) {
            m_bufferFullPause = false;
            m_status.statusCode = kStatusCodePlay;
        }
        break;
    }

    if (m_readyForPeriodicReports && !m_processingCommand) {
        // HACK to ensure the system detects the absence of a disc properly
        if (m_disc.sessions.empty()) {
            m_status.statusCode = kStatusCodeNoDisc;
            m_targetDriveCycles = kDriveCyclesNotPlaying;
        }
        m_status.statusCode |= kStatusFlagPeriodic;
        ReportCDStatus();
        SetInterrupt(kHIRQ_SCDQ);
    }
}

void CDBlock::ProcessDriveStatePlay() {
    const bool scan = (m_status.statusCode & 0xF) == kStatusCodeScan;
    const uint32 frameAddress = m_status.frameAddress;
    if (frameAddress <= m_playEndPos) {
        devlog::trace<grp::play>("Read from frame address {:06X}", frameAddress);

        if (m_disc.sessions.empty()) [[unlikely]] {
            devlog::debug<grp::play>("Disc removed");

            m_status.statusCode = kStatusCodeNoDisc; // TODO: is this correct?
            SetInterrupt(kHIRQ_DCHG);
        } else {
            // TODO: consider caching the track pointer
            const media::Session &session = m_disc.sessions.back();
            const media::Track *track = session.FindTrack(frameAddress);

            Buffer &buffer = m_scratchBuffer;

            // Sanity check: is the track valid?
            if (track != nullptr && track->ReadSector(frameAddress, buffer.data, m_getSectorLength)) [[likely]] {
                devlog::trace<grp::play>("Read {} bytes from frame address {:06X}", buffer.size, buffer.frameAddress);

                if (track->controlADR == 0x01) {
                    // If playing an audio track, send to SCSP
                    if (track->bigEndian) {
                        // Swap endianness if necessary
                        for (uint32 offset = 0; offset < 2352; offset += 2) {
                            util::WriteLE<uint16>(&buffer.data[offset], util::ReadBE<uint16>(&buffer.data[offset]));
                        }
                    }

                    if (scan) {
                        // While scanning, lower volume by 12 dB
                        for (uint32 offset = 0; offset < 2352; offset += 2) {
                            util::WriteLE<sint16>(&buffer.data[offset],
                                                  util::ReadLE<sint16>(&buffer.data[offset]) >> 2u);
                        }
                    }

                    // The callback returns how many thirds of the buffer are full
                    const uint32 currBufferLength =
                        m_cbCDDASector(std::span<uint8, 2352>(buffer.data.begin(), buffer.data.end()));

                    // Adjust pace based on how full the SCSP CDDA buffer is
                    if (currBufferLength < 1) {
                        // Run faster if the buffer is less than a third full
                        m_targetDriveCycles = kDriveCyclesPlaying1x - (kDriveCyclesPlaying1x >> 2);
                    } else if (currBufferLength >= 2) {
                        // Run slower if the buffer is more than two-thirds full
                        m_targetDriveCycles = kDriveCyclesPlaying1x + (kDriveCyclesPlaying1x >> 2);
                    } else {
                        // Normal speed otherwise
                        m_targetDriveCycles = kDriveCyclesPlaying1x;
                    }

                    devlog::trace<grp::play>("Sector {:06X} sent to SCSP", frameAddress);
                } else if (m_partitionManager.GetFreeBufferCount() == 0) [[unlikely]] {
                    devlog::debug<grp::play>("No free buffer available");

                    // TODO: what is the correct status code here?
                    // TODO: there really should be a separate state machine for handling this...
                    m_status.statusCode = kStatusCodePause;
                    SetInterrupt(kHIRQ_BFUL);
                    m_bufferFullPause = true;
                } else {
                    buffer.size = m_getSectorLength;
                    buffer.frameAddress = frameAddress;
                    track->ReadSectorSubheader(frameAddress, buffer.subheader);

                    // Check against CD device filter and send data to the appropriate destination
                    uint8 filterNum = m_cdDeviceConnection;
                    for (int i = 0; i < kNumFilters && filterNum != Filter::kDisconnected; i++) {
                        const Filter &filter = m_filters[filterNum];
                        if (filter.Test(buffer)) {
                            if (filter.trueOutput == Filter::kDisconnected) [[unlikely]] {
                                devlog::trace<grp::play>("Passed filter; output disconnected - discarded");
                            } else {
                                assert(filter.trueOutput < m_filters.size());
                                devlog::trace<grp::play>("Passed filter; sent to buffer partition {}",
                                                         filter.trueOutput);
                                m_partitionManager.InsertHead(filter.trueOutput, buffer);
                                m_lastCDWritePartition = filter.trueOutput;
                                SetInterrupt(kHIRQ_CSCT);
                            }
                            break;
                        } else {
                            if (filter.falseOutput == Filter::kDisconnected) [[unlikely]] {
                                devlog::trace<grp::play>("Filtered out; output disconnected - discarded");
                                break;
                            } else {
                                assert(filter.falseOutput < m_filters.size());
                                devlog::trace<grp::play>("Filtered out; sent to filter {}", filter.falseOutput);
                                filterNum = filter.falseOutput;
                            }
                        }
                    }
                }

                if (!m_bufferFullPause) {
                    // Skip frames while scanning
                    if (scan) {
                        constexpr uint8 kScanCounter = 15;
                        constexpr uint8 kScanFrameSkip = 75;
                        static_assert(
                            kScanFrameSkip >= kScanCounter,
                            "scan frame skip includes the frame counter, so it cannot be shorter than the counter");

                        m_scanCounter++;
                        if (m_scanCounter >= kScanCounter) {
                            m_scanCounter = 0;
                            if (m_scanDirection) {
                                m_status.frameAddress -= kScanFrameSkip + kScanCounter;
                            } else {
                                m_status.frameAddress += kScanFrameSkip - kScanCounter;
                            }
                        }
                    }

                    m_status.frameAddress++;
                    m_status.track = track->index;
                    m_status.index = track->FindIndex(m_status.frameAddress);
                    m_status.controlADR = track->controlADR;
                    m_status.flags = track->controlADR == 0x41 ? 0x8 : 0x0;
                }
            } else if (track == nullptr) {
                // This shouldn't really happen unless we're given an invalid disc image
                // Let's pretend this is a disc read error
                // TODO: what happens on a real disc read error?
                devlog::debug<grp::play>("Track not found");
                m_status.statusCode = kStatusCodeError;
            } else {
                // The disc image is truncated or corrupted
                // Let's pretend this is a disc read error
                // TODO: what happens on a real disc read error?
                devlog::debug<grp::play>("Could not read sector - disc image is truncated or corrupted");
                m_status.statusCode = kStatusCodeError;
            }
        }
    }

    if (m_status.frameAddress > m_playEndPos) {
        // 0x0 to 0xE = 0 to 14 repeats
        // 0xF = infinite repeats
        if (m_playMaxRepeat == 0xF || m_status.repeatCount < m_playMaxRepeat) {
            if (m_playMaxRepeat == 0xF) {
                devlog::debug<grp::play>("Playback repeat (infinite)");
            } else {
                devlog::debug<grp::play>("Playback repeat: {} of {}", m_status.repeatCount + 1, m_playMaxRepeat);
            }
            m_status.frameAddress = m_playStartPos;
            m_status.repeatCount++;
        } else {
            devlog::debug<grp::play>("Playback ended");
            m_status.frameAddress = m_playEndPos + 1;
            m_status.statusCode = kStatusCodePause;
            m_targetDriveCycles = kDriveCyclesNotPlaying;
            SetInterrupt(kHIRQ_PEND);
            if (m_playFile) {
                SetInterrupt(kHIRQ_EFLS | kHIRQ_EHST);
            }
        }
    }
}

void CDBlock::SetInterrupt(uint16 bits) {
    m_HIRQ |= bits;
    UpdateInterrupts();
}

void CDBlock::UpdateInterrupts() {
    devlog::trace<grp::base>("HIRQ = {:04X}  mask = {:04X}  active = {:04X}", m_HIRQ, m_HIRQMASK, m_HIRQ & m_HIRQMASK);
    if (m_HIRQ & m_HIRQMASK) {
        m_cbTriggerExternalInterrupt0();
    }
}

void CDBlock::ReportCDStatus() {
    ReportCDStatus(m_status.statusCode);
}

void CDBlock::ReportCDStatus(uint8 statusCode) {
    m_CR[0] = (statusCode << 8u) | (m_status.flags << 4u) | (m_status.repeatCount);
    m_CR[1] = (m_status.controlADR << 8u) | m_status.track;
    m_CR[2] = (m_status.index << 8u) | ((m_status.frameAddress >> 16u) & 0xFF);
    m_CR[3] = m_status.frameAddress;
}

void CDBlock::SetupTOCTransfer() {
    devlog::debug<grp::xfer>("Starting TOC transfer");

    m_xferType = TransferType::TOC;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = sizeof(media::Session::toc) / sizeof(uint16);
    m_xferCount = 0;
    m_xferExtraCount = 0;

    if (m_disc.sessions.empty()) {
        std::fill_n(m_xferBuffer.begin(), m_xferLength, 0xFFFF);
    } else {
        auto &toc = m_disc.sessions.back().toc;
        for (size_t i = 0; i < toc.size(); i++) {
            m_xferBuffer[i * 2 + 0] = toc[i] >> 16u;
            m_xferBuffer[i * 2 + 1] = toc[i] >> 0u;
        }
    }
}

void CDBlock::SetupGetSectorTransfer(uint16 sectorPos, uint16 sectorCount, uint8 partitionNumber, bool del) {
    devlog::trace<grp::xfer>("Starting sector {} transfer - sectors {} to {} into buffer partition {}",
                             (del ? "read then delete" : "read"), sectorPos, sectorPos + sectorCount - 1,
                             partitionNumber);

    const uint8 partitionSize = m_partitionManager.GetBufferCount(partitionNumber);
    if (sectorPos == 0xFFFF) {
        m_xferSectorPos = partitionSize - sectorCount;
        m_xferSectorEnd = partitionSize - 1;
    } else if (sectorCount == 0xFFFF) {
        m_xferSectorPos = sectorPos;
        m_xferSectorEnd = partitionSize - 1;
    } else {
        m_xferSectorPos = sectorPos;
        m_xferSectorEnd = std::min<uint32>(sectorPos + sectorCount - 1, partitionSize);
    }
    m_xferPartition = partitionNumber;

    m_xferType = del ? TransferType::GetThenDeleteSector : TransferType::GetSector;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = m_getSectorLength / sizeof(uint16) * (m_xferSectorEnd - m_xferSectorPos + 1);
    m_xferCount = 0;
    m_xferExtraCount = 0;

    ReadSector();
}

uint32 CDBlock::SetupFileInfoTransfer(uint32 fileID) {
    devlog::debug<grp::xfer>("Starting file info transfer - file ID {:X}", fileID);

    const uint32 fileOffset = m_fs.GetFileOffset();
    const uint32 fileCount = m_fs.GetFileCount();
    assert(fileOffset < fileCount);

    const bool readAll = fileID == 0xFFFFFF;
    uint32 numFileInfos;
    if (readAll) {
        numFileInfos = std::min(fileCount - fileOffset - 2, 254u);
    } else {
        numFileInfos = 1;
    }

    m_xferType = TransferType::FileInfo;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = numFileInfos * 12 / sizeof(uint16);
    m_xferCount = 0;
    m_xferExtraCount = 0;

    const uint32 baseFileID = readAll ? 0 : fileID;
    for (uint32 i = 0; i < numFileInfos; i++) {
        const media::fs::FileInfo &fileInfo = m_fs.GetFileInfoWithOffset(baseFileID + i);
        m_xferBuffer[i * 6 + 0] = fileInfo.frameAddress >> 16u;
        m_xferBuffer[i * 6 + 1] = fileInfo.frameAddress >> 0u;
        m_xferBuffer[i * 6 + 2] = fileInfo.fileSize >> 16u;
        m_xferBuffer[i * 6 + 3] = fileInfo.fileSize >> 0u;
        m_xferBuffer[i * 6 + 4] = (fileInfo.unitSize << 8u) | fileInfo.interleaveGapSize;
        m_xferBuffer[i * 6 + 5] = (fileInfo.fileNumber << 8u) | fileInfo.attributes;
    }

    return numFileInfos;
}

bool CDBlock::SetupSubcodeTransfer(uint8 type) {
    if (m_disc.sessions.empty()) {
        return false;
    }

    if (type == 0) {
        devlog::trace<grp::xfer>("Starting subcode Q transfer");

        m_xferType = TransferType::Subcode;
        m_xferPos = 0;
        m_xferBufferPos = 0;
        m_xferLength = 5;
        m_xferCount = 0;
        m_xferExtraCount = 0;

        const uint32 relativeFAD =
            m_status.frameAddress - m_disc.sessions.back().tracks[m_status.track - 1].startFrameAddress;

        auto [m, s, f] = FADToMSF(m_status.frameAddress);
        auto [relM, relS, relF] = FADToMSF(relativeFAD);

        m_xferBuffer[0] = m_status.controlADR;
        m_xferBuffer[1] = util::to_bcd(m_status.track);
        m_xferBuffer[2] = util::to_bcd(m_status.index);
        m_xferBuffer[3] = util::to_bcd(relM);
        m_xferBuffer[4] = util::to_bcd(relS);
        m_xferBuffer[5] = util::to_bcd(relF);
        m_xferBuffer[6] = 0;
        m_xferBuffer[7] = util::to_bcd(m);
        m_xferBuffer[8] = util::to_bcd(s);
        m_xferBuffer[9] = util::to_bcd(f);

        m_CR[0] = m_status.statusCode << 8u;
        m_CR[1] = 5;
        m_CR[2] = 0x0000;
        m_CR[3] = 0x0000;

        return true;
    } else if (type == 1) {
        devlog::trace<grp::xfer>("Starting subcode R-W transfer");

        m_xferType = TransferType::Subcode;
        m_xferPos = 0;
        m_xferBufferPos = 0;
        m_xferLength = 12;
        m_xferCount = 0;
        m_xferExtraCount = 0;

        if (m_status.frameAddress != m_xferSubcodeFrameAddress) {
            m_xferSubcodeFrameAddress = m_status.frameAddress;
            m_xferSubcodeGroup = 0;
        } else {
            m_xferSubcodeGroup++;
        }

        // TODO: read subcode R-W from current sector (24 bytes starting at 2352 + 24*group), & 0x3F all bytes
        // - only works with discs that have 2448 byte sectors
        if (m_disc.sessions.back().tracks[m_status.track - 1].sectorSize < 2448) {
            m_xferBuffer.fill(0xFF);
        } else {
            devlog::trace<grp::xfer>("Subcode R-W transfer is unimplemented");
            m_xferBuffer.fill(0xFF);
        }

        m_CR[0] = m_status.statusCode << 8u;
        m_CR[1] = 12;
        m_CR[2] = 0x0000;
        m_CR[3] = m_xferSubcodeGroup;

        return true;
    }

    return false;
}

void CDBlock::EndTransfer() {
    if (m_xferType == TransferType::None) {
        return;
    }

    devlog::trace<grp::xfer>("Ending transfer at {} of {} words", m_xferPos, m_xferLength);
    if (m_xferExtraCount > 0) {
        devlog::debug<grp::xfer>("{} unexpected transfer attempts", m_xferExtraCount);
    }

    // Trigger EHST HIRQ if ending certain sector transfers
    switch (m_xferType) {
    case TransferType::GetSector:
    case TransferType::GetThenDeleteSector: {
        SetInterrupt(kHIRQ_EHST);
        break;
    }
    default: break;
    }

    m_xferType = TransferType::None;
    m_xferPos = 0;
    m_xferBufferPos = 0;
    m_xferLength = 0;
    m_xferCount = 0xFFFFFF;
}

void CDBlock::ReadSector() {
    const Buffer *buffer = m_partitionManager.GetTail(m_xferPartition, m_xferSectorPos);
    if (buffer != nullptr) {
        devlog::trace<grp::xfer>("Starting transfer from sector at frame address {:08X} - sector {}",
                                 buffer->frameAddress, m_xferSectorPos);

        for (size_t i = 0; i < m_getSectorLength; i += sizeof(uint16)) {
            m_xferBuffer[i / sizeof(uint16)] = util::ReadBE<uint16>(&buffer->data[i]);
        }
    } else {
        devlog::warn<grp::xfer>("Out of bounds transfer - sector {}", m_xferSectorPos);
    }
}

uint16 CDBlock::DoReadTransfer() {
    uint16 value;
    if (m_xferBufferPos < m_xferBuffer.size()) {
        // TODO: what happens when games attempt to do out-of-bounds reads from TOC of file info transfers?
        value = m_xferBuffer[m_xferBufferPos++];
    } else {
        // TODO: what to return here?
        value = 0xFFFF;
    }

    switch (m_xferType) {
    case TransferType::GetSector: [[fallthrough]];
    case TransferType::GetThenDeleteSector:
        if (m_xferBufferPos >= m_getSectorLength / sizeof(uint16)) {
            if (m_xferType == TransferType::GetThenDeleteSector) {
                // Delete sector once fully read
                m_partitionManager.RemoveTail(m_xferPartition, m_xferSectorPos);
                devlog::trace<grp::xfer>("Sector freed");
            } else {
                m_xferSectorPos++;
                devlog::trace<grp::xfer>("Going to sector {}", m_xferSectorPos);
            }
            m_xferBufferPos = 0;
            if (m_xferPos + 1 < m_xferLength) {
                ReadSector();
            }
        }
        break;

    case TransferType::TOC: break;
    case TransferType::FileInfo: break;
    case TransferType::Subcode: break;

    default: m_xferExtraCount++; return 0; // write-only or no active transfer, or unimplemented read transfer
    }

    AdvanceTransfer();

    return value;
}

void CDBlock::DoWriteTransfer(uint16 value) {
    // TODO: implement write transfers
    /*switch (m_xferType) {
    }*/

    m_xferExtraCount++;

    AdvanceTransfer();
}

void CDBlock::AdvanceTransfer() {
    m_xferPos++;
    m_xferCount++;
    if (m_xferPos >= m_xferLength) {
        devlog::trace<grp::xfer>("Transfer finished - {} of {} words transferred", m_xferCount, m_xferLength);
        m_xferType = TransferType::None;
        m_xferPos = 0;
        m_xferLength = 0;
    }
}

bool CDBlock::ConnectCDDevice(uint8 filterNumber) {
    if (filterNumber < m_filters.size()) {
        // Connect CD to specified filter
        DisconnectFilterInput(filterNumber);
        m_cdDeviceConnection = filterNumber;
    } else if (filterNumber == Filter::kDisconnected) {
        // Disconnect CD
        m_cdDeviceConnection = Filter::kDisconnected;
    } else {
        return false;
    }
    return true;
}

void CDBlock::DisconnectFilterInput(uint8 filterNumber) {
    if (m_cdDeviceConnection == filterNumber) {
        m_cdDeviceConnection = Filter::kDisconnected;
    }
    for (auto &filter : m_filters) {
        if (filter.falseOutput == filterNumber) {
            filter.falseOutput = Filter::kDisconnected;
            break; // there can be only one input connection to a filter
        }
    }
}

void CDBlock::SetupCommand() {
    m_scheduler.ScheduleFromNow(m_commandExecEvent, 50);
}

FORCE_INLINE void CDBlock::ProcessCommand() {
    devlog::trace<grp::base>("Processing command {:04X} {:04X} {:04X} {:04X}", m_CR[0], m_CR[1], m_CR[2], m_CR[3]);

    const uint8 cmd = m_CR[0] >> 8u;

    switch (cmd) {
    case 0x00: CmdGetStatus(); break;
    case 0x01: CmdGetHardwareInfo(); break;
    case 0x02: CmdGetTOC(); break;
    case 0x03: CmdGetSessionInfo(); break;
    case 0x04: CmdInitializeCDSystem(); break;
    case 0x05: CmdOpenTray(); break;
    case 0x06: CmdEndDataTransfer(); break;
    case 0x10: CmdPlayDisc(); break;
    case 0x11: CmdSeekDisc(); break;
    case 0x12: CmdScanDisc(); break;
    case 0x20: CmdGetSubcodeQ_RW(); break;
    case 0x30: CmdSetCDDeviceConnection(); break;
    case 0x31: CmdGetCDDeviceConnection(); break;
    case 0x32: CmdGetLastBufferDest(); break;
    case 0x40: CmdSetFilterRange(); break;
    case 0x41: CmdGetFilterRange(); break;
    case 0x42: CmdSetFilterSubheaderConditions(); break;
    case 0x43: CmdGetFilterSubheaderConditions(); break;
    case 0x44: CmdSetFilterMode(); break;
    case 0x45: CmdGetFilterMode(); break;
    case 0x46: CmdSetFilterConnection(); break;
    case 0x47: CmdGetFilterConnection(); break;
    case 0x48: CmdResetSelector(); break;
    case 0x50: CmdGetBufferSize(); break;
    case 0x51: CmdGetSectorNumber(); break;
    case 0x52: CmdCalculateActualSize(); break;
    case 0x53: CmdGetActualSize(); break;
    case 0x54: CmdGetSectorInfo(); break;
    // case 0x55: CmdExecuteFADSearch(); break;
    // case 0x56: CmdGetFADSearchResults(); break;
    case 0x60: CmdSetSectorLength(); break;
    case 0x61: CmdGetSectorData(); break;
    case 0x62: CmdDeleteSectorData(); break;
    case 0x63: CmdGetThenDeleteSectorData(); break;
    // case 0x64: CmdPutSectorData(); break;
    // case 0x65: CmdCopySectorData(); break;
    // case 0x66: CmdMoveSectorData(); break;
    case 0x67: CmdGetCopyError(); break;
    case 0x70: CmdChangeDirectory(); break;
    // case 0x71: CmdReadDirectory(); break;
    case 0x72: CmdGetFileSystemScope(); break;
    case 0x73: CmdGetFileInfo(); break;
    case 0x74: CmdReadFile(); break;
    case 0x75: CmdAbortFile(); break;

    // case 0x90: CmdMpegGetStatus(); break;
    // case 0x91: CmdMpegGetInterrupt(); break;
    // case 0x92: CmdMpegSetInterruptMask(); break;
    case 0x93:
        CmdMpegInit();
        break;
        // case 0x94: CmdMpegSetMode(); break;
        // case 0x95: CmdMpegPlay(); break;
        // case 0x96: CmdMpegSetDecodingMethod(); break;
        // case 0x9A: CmdMpegSetConnection(); break;
        // case 0x9B: CmdMpegGetConnection(); break;
        // case 0x9D: CmdMpegSetStream(); break;
        // case 0x9E: CmdMpegGetStream(); break;
        // case 0xA0: CmdMpegDisplay(); break;
        // case 0xA1: CmdMpegSetWindow(); break;
        // case 0xA2: CmdMpegSetBorderColor(); break;
        // case 0xA3: CmdMpegSetFade(); break;
        // case 0xA4: CmdMpegSetVideoEffects(); break;
        // case 0xAF: CmdMpegSetLSI(); break;

    case 0xE0: CmdAuthenticateDevice(); break;
    case 0xE1:
        CmdIsDeviceAuthenticated();
        break;
        // case 0xE2: CmdGetMpegROM(); break;

    default: devlog::warn<grp::base>("Unimplemented command {:02X}", cmd); break;
    }

    devlog::trace<grp::base>("Command response:  {:04X} {:04X} {:04X} {:04X}", m_CR[0], m_CR[1], m_CR[2], m_CR[3]);
}

void CDBlock::CmdGetStatus() {
    devlog::trace<grp::base>("-> Get status");

    // Input structure:
    // 0x00     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetHardwareInfo() {
    devlog::trace<grp::base>("-> Get hardware info");

    // Input structure:
    // 0x01     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code      <blank>
    // hardware flags   hardware version
    // <blank>          MPEG version (0 if unauthenticated)
    // drive version    drive revision
    m_CR[0] = m_status.statusCode << 8u;
    m_CR[1] = 0x0002;
    m_CR[2] = 0x0000;
    m_CR[3] = 0x0600;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetTOC() {
    devlog::trace<grp::base>("-> Get TOC");

    // Input structure:
    // 0x02     <blank>
    // <blank>
    // <blank>
    // <blank>

    SetupTOCTransfer();

    // Output structure:
    // status code   <blank>
    // TOC size in words
    // <blank>
    // <blank>
    m_CR[0] = m_status.statusCode << 8u;
    m_CR[1] = sizeof(media::Session::toc) / sizeof(uint16);
    m_CR[2] = 0x0000;
    m_CR[3] = 0x0000;

    // TODO: make busy for a brief moment
    m_status.statusCode = kStatusCodePause;
    m_targetDriveCycles = kDriveCyclesNotPlaying;

    SetInterrupt(kHIRQ_CMOK | kHIRQ_DRDY);
}

void CDBlock::CmdGetSessionInfo() {
    devlog::trace<grp::base>("-> Get session info");

    // Input structure:
    // 0x03     session data type (00 = all, others = specific session)
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code        <blank>
    // <blank>
    // session num/count  lba bits 23-16
    // lba bits 15-0

    m_CR[0] = m_status.statusCode << 8u;
    m_CR[1] = 0x0000;

    const uint8 sessionNum = bit::extract<0, 7>(m_CR[0]);
    if (sessionNum == 0) {
        // Get information about all sessions
        m_CR[2] = (m_disc.sessions.size() << 8u); // TODO: session LBA?
        m_CR[3] = 0x0000;
    } else if (sessionNum <= m_disc.sessions.size()) {
        // Get information about a specific session
        m_CR[2] = (sessionNum << 8u) | bit::extract<16, 23>(m_disc.sessions[sessionNum - 1].toc[101]);
        m_CR[3] = bit::extract<0, 15>(m_disc.sessions[sessionNum - 1].toc[101]);
    } else {
        // Return FFFFFFFF for nonexistent sessions
        m_CR[2] = 0xFFFF;
        m_CR[3] = 0xFFFF;
    }

    // TODO: make busy for a brief moment
    m_status.statusCode = kStatusCodePause;
    m_targetDriveCycles = kDriveCyclesNotPlaying;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdInitializeCDSystem() {
    devlog::trace<grp::base>("-> Initialize CD system");

    // Input structure:
    // 0x04           initialization flags
    // standby time
    // <blank>
    // ECC            retry count
    const bool softReset = bit::test<0>(m_CR[0]);
    // const bool decodeSubcodeRW = bit::test<1>(m_CR[0]);
    // const bool ignoreMode2Subheader = bit::test<2>(m_CR[0]);
    // const bool retryForm2Read = bit::test<3>(m_CR[0]);
    const uint8 readSpeed = bit::extract<4, 5>(m_CR[0]); // 0=max (2x), 1=1x, 2=2x, 3=invalid
    // const bool keepSettings = bit::test<7>(m_CR[0]);
    // const uint16 standbyTime = m_CR[1];
    // const uint8 ecc = bit::extract<8, 15>(m_CR[3]);
    // const uint8 retryCount = bit::extract<0, 7>(m_CR[3]);

    if ((m_status.statusCode & 0xF) != kStatusCodeOpen && (m_status.statusCode & 0xF) != kStatusCodeNoDisc) {
        // TODO: switch to Busy for a bit before NoDisc/Pause
        if (m_disc.sessions.empty()) {
            m_status.statusCode = kStatusCodeNoDisc;
        } else {
            m_status.statusCode = kStatusCodePause;
        }
        m_status.frameAddress = 150;

        // TODO: should it also reset playback state?
    }

    if (softReset) {
        devlog::debug<grp::base>("Soft reset");
        // TODO: use Reset(false)
        // TODO: switch to Busy for a bit before NoDisc/Pause
        m_targetDriveCycles = kDriveCyclesNotPlaying;

        // Reset state and configuration
        m_playStartParam = 0xFFFFFF;
        m_playEndParam = 0xFFFFFF;
        m_playRepeatParam = 0;

        m_playStartPos = 0xFFFFFF;
        m_playEndPos = 0xFFFFFF;
        m_playMaxRepeat = 0;
        m_playFile = false;

        m_discAuthStatus = 0;
        m_mpegAuthStatus = 0;
    }

    m_readSpeed = readSpeed == 1 ? 1 : m_readSpeedFactor;
    devlog::info<grp::base>("Read speed set to {}x", m_readSpeed);

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdOpenTray() {
    devlog::trace<grp::base>("-> Open tray");

    // Input structure:
    // 0x05     <blank>
    // <blank>
    // <blank>
    // <blank>

    OpenTray();

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS | kHIRQ_DCHG);
}

void CDBlock::CmdEndDataTransfer() {
    devlog::trace<grp::base>("-> End data transfer");

    // Input structure:
    // 0x06     <blank>
    // <blank>
    // <blank>
    // <blank>

    const uint32 transferCount = m_xferCount;

    EndTransfer();

    // Output structure:
    // status code      transferred word count bits 23-16
    // transferred word count bits 15-0
    // <blank>
    // <blank>
    m_CR[0] = (m_status.statusCode << 8u) | (transferCount >> 16u);
    m_CR[1] = transferCount;
    m_CR[2] = 0x0000;
    m_CR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdPlayDisc() {
    devlog::trace<grp::base>("-> Play disc");

    // Input structure:
    // 0x10           start position bits 23-16
    // start position bits 15-0
    // play mode      end position bits 23-16
    // end position bits 15-0
    const uint8 repeatParam = bit::extract<8, 15>(m_CR[2]);
    const uint32 startParam = (bit::extract<0, 7>(m_CR[0]) << 16u) | m_CR[1];
    const uint32 endParam = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    devlog::debug<grp::base>("Play parameters: start={:06X} end={:06X} repeat={:X}", startParam, endParam, repeatParam);

    // Output structure: standard CD status data
    if (SetupGenericPlayback(startParam, endParam, repeatParam)) {
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSeekDisc() {
    devlog::trace<grp::base>("-> Seek disc");

    // Input structure:
    // 0x11           start position bits 23-16
    // start position bits 15-0
    // <blank>
    // <blank>
    const uint32 startPos = (bit::extract<0, 7>(m_CR[0]) << 16u) | m_CR[1];
    const bool isStartFAD = bit::test<23>(startPos);

    devlog::trace<grp::base>("Seek position: {:06X}", startPos);
    if (startPos == 0xFFFFFF) {
        devlog::debug<grp::base>("Paused");
        m_status.statusCode = kStatusCodePause;
        m_targetDriveCycles = kDriveCyclesNotPlaying;
    } else if (startPos == 0x000000) {
        devlog::debug<grp::base>("Stopped");
        m_status.statusCode = kStatusCodeStandby;
        m_status.frameAddress = 0xFFFFFF;
        m_status.flags = 0xF;
        m_status.repeatCount = 0xF;
        m_status.controlADR = 0xFF;
        m_status.track = 0xFF;
        m_status.index = 0xFF;
        m_targetDriveCycles = kDriveCyclesNotPlaying;
    } else if (isStartFAD) {
        const uint32 frameAddress = startPos & 0x7FFFFF;
        devlog::debug<grp::base>("Seeking to frame address {:06X}", frameAddress);
        if (m_disc.sessions.empty()) {
            devlog::debug<grp::base>("No disc in drive - stopped");
            m_status.statusCode = kStatusCodeNoDisc;
            m_status.frameAddress = 0xFFFFFF;
            m_status.flags = 0xF;
            m_status.repeatCount = 0xF;
            m_status.controlADR = 0xFF;
            m_status.track = 0xFF;
            m_status.index = 0xFF;
            m_targetDriveCycles = kDriveCyclesNotPlaying;
        } else {
            const auto &session = m_disc.sessions.back();
            const uint8 trackIndex = session.FindTrackIndex(frameAddress);
            if (trackIndex < 99) {
                const auto &track = session.tracks[trackIndex];
                m_status.statusCode = kStatusCodePause;
                m_status.frameAddress = frameAddress;
                m_status.flags = track.controlADR == 0x41 ? 0x8 : 0x0;
                m_status.controlADR = track.controlADR;
                m_status.track = trackIndex;
                m_status.index = 1;
                m_targetDriveCycles = kDriveCyclesNotPlaying;
            } else {
                devlog::debug<grp::base>("Frame address out of range - stopped");
                m_status.statusCode = kStatusCodeStandby;
                m_status.frameAddress = 0xFFFFFF;
                m_status.flags = 0xF;
                m_status.repeatCount = 0xF;
                m_status.controlADR = 0xFF;
                m_status.track = 0xFF;
                m_status.index = 0xFF;
                m_targetDriveCycles = kDriveCyclesNotPlaying;
            }
        }
    } else {
        const uint32 trackNum = bit::extract<8, 14>(startPos);
        const uint32 indexNum = bit::extract<0, 6>(startPos);
        devlog::debug<grp::base>("Seeking to track:index {}:{}", trackNum, indexNum);
        if (m_disc.sessions.empty()) {
            devlog::debug<grp::base>("No disc in drive - stopped");
            m_status.statusCode = kStatusCodeNoDisc;
            m_status.frameAddress = 0xFFFFFF;
            m_status.flags = 0xF;
            m_status.repeatCount = 0xF;
            m_status.controlADR = 0xFF;
            m_status.track = 0xFF;
            m_status.index = 0xFF;
            m_targetDriveCycles = kDriveCyclesNotPlaying;
        } else {
            const auto &session = m_disc.sessions.back();
            if (trackNum - 1 >= session.firstTrackIndex && trackNum - 1 <= session.lastTrackIndex) {
                const auto &track = session.tracks[trackNum - 1];
                m_status.statusCode = kStatusCodePause;
                m_status.frameAddress = track.startFrameAddress;
                m_status.flags = track.controlADR == 0x41 ? 0x8 : 0x0;
                m_status.controlADR = track.controlADR;
                m_status.track = trackNum;
                m_status.index = 1;
                m_targetDriveCycles = kDriveCyclesNotPlaying;
            } else {
                devlog::debug<grp::base>("Track:index out of range - stopped");
                m_status.statusCode = kStatusCodeStandby;
                m_status.frameAddress = 0xFFFFFF;
                m_status.flags = 0xF;
                m_status.repeatCount = 0xF;
                m_status.controlADR = 0xFF;
                m_status.track = 0xFF;
                m_status.index = 0xFF;
                m_targetDriveCycles = kDriveCyclesNotPlaying;
            }
        }
    }

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdScanDisc() {
    devlog::trace<grp::base>("-> Scan disc");

    // Input structure:
    // 0x12     scan direction
    // <blank>
    // <blank>
    // <blank>
    const uint8 direction = bit::extract<0, 7>(m_CR[0]);

    // Output structure: standard CD status data
    if (SetupScan(direction)) {
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetSubcodeQ_RW() {
    devlog::trace<grp::base>("-> Get Subcode Q/RW");

    // Input structure:
    // 0x20     type
    // <blank>
    // <blank>
    // <blank>
    const uint8 type = bit::extract<0, 7>(m_CR[0]);

    // TODO: handle types
    //   type 0 = Q subcode
    //   type 1 = R-W subcodes
    if (SetupSubcodeTransfer(type)) {
        // Output structure if valid (handled by SetupSubcodeTransfer):
        // status code     <blank>
        // Q/RW size in words (Q = 5, RW = 12)
        // <blank>
        // subcode flags

        SetInterrupt(kHIRQ_DRDY);
    } else {
        // Output structure if invalid:
        // 0x80   <blank>
        // <blank>
        // <blank>
        // <blank>
        m_CR[0] = 0x8000;
        m_CR[1] = 0x0000;
        m_CR[2] = 0x0000;
        m_CR[3] = 0x0000;
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSetCDDeviceConnection() {
    devlog::trace<grp::base>("-> Set CD device connection");

    // Input structure:
    // 0x30           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    // Output structure: standard CD status data
    if (ConnectCDDevice(filterNumber)) {
        devlog::debug<grp::base>("CD device connected to filter {}", filterNumber);
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetCDDeviceConnection() {
    devlog::trace<grp::base>("-> Get CD device connection");

    // Input structure:
    // 0x31           <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code    <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    m_CR[0] = m_status.statusCode << 8u;
    m_CR[1] = 0x0000;
    m_CR[2] = m_cdDeviceConnection << 8u;
    m_CR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetLastBufferDest() {
    devlog::trace<grp::base>("-> Get last buffer destination");

    // Input structure:
    // 0x32     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code        <blank>
    // <blank>
    // partition number   <blank>
    // <blank>
    m_CR[0] = m_status.statusCode << 8u;
    m_CR[1] = 0x0000;
    m_CR[2] = m_lastCDWritePartition << 8u;
    m_CR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSetFilterRange() {
    devlog::trace<grp::base>("-> Set filter range");

    // Input structure:
    // 0x40           start frame address bits 23-16
    // start frame address bits 15-0
    // filter number  frame address count bits 23-16
    // frame address count bits 15-0
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        const uint32 start = (bit::extract<0, 7>(m_CR[0]) << 16u) | m_CR[1];
        const uint32 count = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];
        auto &filter = m_filters[filterNumber];
        filter.startFrameAddress = start;
        filter.frameAddressCount = count;

        devlog::debug<grp::base>("Filter {} FAD range: start={:06X} count={:06X}", filterNumber, start, count);

        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFilterRange() {
    devlog::trace<grp::base>("-> Get filter range");

    // Input structure:
    // 0x41           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        // Output structure:
        // status code    start frame address bits 23-16
        // start frame address bits 15-0
        // filter number  frame address count bits 23-16
        // frame address count bits 15-0
        const auto &filter = m_filters[filterNumber];
        m_CR[0] = (m_status.statusCode << 8u) | (filter.startFrameAddress >> 16u);
        m_CR[1] = filter.startFrameAddress;
        m_CR[2] = (filterNumber << 8u) | (filter.frameAddressCount >> 16u);
        m_CR[3] = filter.frameAddressCount;
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSetFilterSubheaderConditions() {
    devlog::trace<grp::base>("-> Set filter subheader conditions");

    // Input structure:
    // 0x42           channel
    // submode mask   coding info mask
    // filter number  file ID
    // submode value  coding info value
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        const uint8 chanNum = bit::extract<0, 7>(m_CR[0]);
        const uint8 submodeMask = bit::extract<8, 15>(m_CR[1]);
        const uint8 codingInfoMask = bit::extract<0, 7>(m_CR[1]);
        const uint8 fileID = bit::extract<0, 7>(m_CR[2]);
        const uint8 submodeValue = bit::extract<8, 15>(m_CR[3]);
        const uint8 codingInfoValue = bit::extract<0, 7>(m_CR[3]);

        auto &filter = m_filters[filterNumber];
        filter.chanNum = chanNum;
        filter.fileNum = fileID;
        filter.submodeMask = submodeMask;
        filter.submodeValue = submodeValue;
        filter.codingInfoMask = codingInfoMask;
        filter.codingInfoValue = codingInfoValue;

        devlog::debug<grp::base>(
            "Filter {} subheader conditions: chanNum={:02X} fileNum={:02X} smmask={:02X} smval={:02X} "
            "cimask={:02X} cival={:02X}",
            filterNumber, chanNum, fileID, submodeMask, submodeValue, codingInfoMask, codingInfoValue);

        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFilterSubheaderConditions() {
    devlog::trace<grp::base>("-> Get filter subheader conditions");

    // Input structure:
    // 0x43           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        // Output structure:
        // status code    channel
        // submode mask   coding info mask
        // filter number  file ID
        // submode value  coding info value
        const auto &filter = m_filters[filterNumber];
        m_CR[0] = (m_status.statusCode << 8u) | filter.chanNum;
        m_CR[1] = (filter.submodeMask << 8u) | filter.codingInfoMask;
        m_CR[2] = (filterNumber << 8u) | filter.fileNum;
        m_CR[3] = (filter.submodeValue << 8u) | filter.codingInfoValue;
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdSetFilterMode() {
    devlog::trace<grp::base>("-> Set filter mode");

    // Input structure:
    // 0x44           mode
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        const uint8 mode = bit::extract<0, 7>(m_CR[0]);

        auto &filter = m_filters[filterNumber];
        filter.mode = mode & 0x5F; // TODO: should it be masked?

        devlog::debug<grp::base>(
            "Filter {} mode={:02X}{}{}{}{}{}{}", filterNumber, filter.mode,
            (bit::test<0>(filter.mode) ? " filenum" : ""), (bit::test<1>(filter.mode) ? " channum" : ""),
            (bit::test<2>(filter.mode) ? " submode" : ""), (bit::test<3>(filter.mode) ? " codinginfo" : ""),
            (bit::test<4>(filter.mode) ? " <- inverted" : ""), (bit::test<6>(filter.mode) ? " fad" : ""));

        if (mode & 0x80) {
            devlog::debug<grp::base>("Filter {} conditions reset", filterNumber);
            filter.ResetConditions();
        }

        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFilterMode() {
    devlog::trace<grp::base>("-> Get filter mode");

    // Input structure:
    // 0x45           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        // Output structure:
        // status code    mode
        // <blank>
        // filter number  <blank>
        // <blank>
        const auto &filter = m_filters[filterNumber];
        m_CR[0] = (m_status.statusCode << 8u) | filter.mode;
        m_CR[1] = 0x0000;
        m_CR[2] = (filterNumber << 8u);
        m_CR[3] = 0x0000;
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdSetFilterConnection() {
    devlog::trace<grp::base>("-> Set filter connection");

    // Input structure:
    // 0x46           connection flags
    // true conn      false conn
    // filter number  <blank>
    // <blank>
    const bool setTrueConn = bit::test<0>(m_CR[0]);
    const bool setFalseConn = bit::test<1>(m_CR[0]);
    const uint8 trueConn = bit::extract<8, 15>(m_CR[1]);
    const uint8 falseConn = bit::extract<0, 7>(m_CR[1]);
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        auto &filter = m_filters[filterNumber];
        if (setTrueConn) {
            devlog::debug<grp::base>("Filter {} true output={:02X}", filterNumber, trueConn);
            filter.trueOutput = trueConn;
        }
        if (setFalseConn) {
            devlog::debug<grp::base>("Filter {} false output={:02X}", filterNumber, falseConn);
            DisconnectFilterInput(filterNumber);
            filter.falseOutput = falseConn;
        }

        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFilterConnection() {
    devlog::trace<grp::base>("-> Get filter connection");

    // Input structure:
    // 0x47           <blank>
    // <blank>
    // filter number  <blank>
    // <blank>
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);

    if (filterNumber < kNumFilters) {
        // Output structure:
        // status code    <blank>
        // true conn      false conn
        // <blank>
        // <blank>
        const auto &filter = m_filters[filterNumber];
        m_CR[0] = (m_status.statusCode << 8u);
        m_CR[1] = (filter.trueOutput << 8u) | filter.falseOutput;
        m_CR[2] = 0x0000;
        m_CR[3] = 0x0000;
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdResetSelector() {
    devlog::trace<grp::base>("-> Reset selector");

    // Input structure:
    // 0x48              reset flags
    // <blank>
    // partition number  <blank>
    // <blank>
    const uint8 resetFlags = bit::extract<0, 7>(m_CR[0]);

    bool reject = false;
    if (resetFlags == 0) {
        const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
        devlog::trace<grp::base>("Clearing buffer partition {}", partitionNumber);
        if (partitionNumber < kNumFilters) {
            m_partitionManager.Clear(partitionNumber);
        } else {
            reject = true;
        }
    } else {
        const bool clearBufferData = bit::test<2>(resetFlags);
        const bool clearPartitionOutputs = bit::test<3>(resetFlags);
        const bool clearFilterConditions = bit::test<4>(resetFlags);
        const bool clearFilterInputs = bit::test<5>(resetFlags);
        const bool clearFilterTrueOutputs = bit::test<6>(resetFlags);
        const bool clearFilterFalseOutputs = bit::test<7>(resetFlags);

        if (clearBufferData) {
            devlog::debug<grp::base>("Clearing all buffer partitions");
            m_partitionManager.Reset();
        }
        if (clearPartitionOutputs) {
            devlog::debug<grp::base>("Clearing all partition output connectors");
            // TODO: clear device inputs and filter inputs connected to partition outputs
        }
        if (clearFilterConditions) {
            devlog::debug<grp::base>("Clearing all filter conditions");
            for (auto &filter : m_filters) {
                filter.ResetConditions();
            }
        }
        if (clearFilterInputs) {
            devlog::debug<grp::base>("Clearing all filter input connectors");
            for (auto &filter : m_filters) {
                filter.falseOutput = Filter::kDisconnected;
            }
            m_cdDeviceConnection = Filter::kDisconnected;
        }
        if (clearFilterTrueOutputs) {
            devlog::debug<grp::base>("Clearing all true filter output connectors");
            for (int i = 0; auto &filter : m_filters) {
                filter.trueOutput = i;
                i++;
            }
        }
        if (clearFilterFalseOutputs) {
            devlog::debug<grp::base>("Clearing all false filter output connectors");
            for (auto &filter : m_filters) {
                filter.falseOutput = Filter::kDisconnected;
            }
        }
    }

    // Output structure: standard CD status data
    if (reject) {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetBufferSize() {
    devlog::trace<grp::base>("-> Get buffer size");

    // Input structure:
    // 0x50     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code          <blank>
    // free buffer count
    // total filter count   <blank>
    // total buffer count
    const uint32 freeBuffers = m_partitionManager.GetFreeBufferCount();
    m_CR[0] = m_status.statusCode << 8u;
    m_CR[1] = freeBuffers;
    m_CR[2] = kNumFilters << 8u;
    m_CR[3] = kNumBuffers;

    devlog::trace<grp::base>("Get buffer size: free buffers = {}", freeBuffers);

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetSectorNumber() {
    devlog::trace<grp::base>("-> Get sector number");

    // Input structure:
    // 0x51              <blank>
    // <blank>
    // partition number  <blank>
    // <blank>
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint8 sectorCount = m_partitionManager.GetBufferCount(partitionNumber);

    // Output structure:
    // status code      <blank>
    // <blank>
    // <blank>
    // number of blocks
    m_CR[0] = (m_status.statusCode << 8u);
    m_CR[1] = 0x0000;
    m_CR[2] = 0x0000;
    m_CR[3] = sectorCount;

    devlog::trace<grp::base>("Partition {} has {} sectors", partitionNumber, sectorCount);

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdCalculateActualSize() {
    devlog::trace<grp::base>("-> Calculate actual size");

    // Input structure:
    // 0x52               <blank>
    // sector offset
    // partition number   <blank>
    // sector number
    const uint16 sectorOffset = m_CR[1];
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint16 sectorNumber = m_CR[3];

    bool reject = false;
    if (partitionNumber > kNumPartitions) [[unlikely]] {
        reject = true;
    } else {
        const uint8 bufferCount = m_partitionManager.GetBufferCount(partitionNumber);
        if (sectorOffset != 0xFFFF && sectorOffset >= bufferCount) [[unlikely]] {
            reject = true;
        } else {
            uint16 startSector;
            uint16 endSector;
            if (sectorOffset == 0xFFFF) {
                startSector = bufferCount - 1;
                if (sectorNumber < bufferCount) {
                    endSector = startSector - sectorNumber + 1;
                } else {
                    endSector = 0;
                }
            } else {
                startSector = sectorOffset;
                endSector = std::min<uint16>(startSector + sectorNumber - 1, bufferCount - 1);
            }
            m_calculatedPartitionSize =
                m_partitionManager.CalculateSize(partitionNumber, startSector, endSector) / sizeof(uint16);
            devlog::trace<grp::base>("Actual size of partition {} from sector {} to {} = {} words", partitionNumber,
                                     startSector, endSector, m_calculatedPartitionSize);
        }
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetActualSize() {
    devlog::trace<grp::base>("-> Get actual size");

    // Input structure:
    // 0x53     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code   calculated size bits 23-16 (in words)
    // calculated size bits 15-0 (in words)
    // <blank>
    // <blank>
    m_CR[0] = (m_status.statusCode << 8u) | bit::extract<16, 23>(m_calculatedPartitionSize);
    m_CR[1] = bit::extract<0, 15>(m_calculatedPartitionSize);
    m_CR[2] = 0x0000;
    m_CR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetSectorInfo() {
    devlog::trace<grp::base>("-> Get sector info");

    // Input structure:
    // 0x54               <blank>
    // <blank>            sector number
    // partition number   <blank>
    // <blank>
    const uint8 sectorNumber = bit::extract<0, 7>(m_CR[1]);
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);

    bool reject = false;
    if (partitionNumber > kNumPartitions) [[unlikely]] {
        reject = true;
    } else {
        const Buffer *buffer = m_partitionManager.GetTail(partitionNumber, sectorNumber);
        if (buffer == nullptr) {
            reject = true;
        } else {
            // Output structure:
            // status code          sector frame address bits 23-16
            // sector frame address bits 15-0
            // sector file number   sector coding number
            // sector submode       sector coding info
            m_CR[0] = (m_status.statusCode << 8u) | (buffer->frameAddress >> 16u);
            m_CR[1] = buffer->frameAddress;
            m_CR[2] = (buffer->subheader.fileNum << 8u) | buffer->subheader.chanNum;
            m_CR[3] = (buffer->subheader.submode << 8u) | buffer->subheader.codingInfo;
        }
    }

    if (reject) {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdExecuteFADSearch() {
    devlog::trace<grp::base>("-> Execute frame address search");

    // Input structure:
    // 0x55     <blank>
    // sector position
    // partition number   frame address bits 23-16
    // frame address bits 15-0
    // const uint16 sectorPos = m_CR[1];
    // const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    // const uint32 frameAddress = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    // TODO: search for a sector with the largest FAD <= searched FAD within specified partition
    // - how does sectorPos factor in here?
    devlog::info<grp::base>("Execute frame address search command is unimplemented");

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetFADSearchResults() {
    devlog::trace<grp::base>("-> Get frame address search results");

    // Input structure:
    // 0x56     <blank>
    // <blank>
    // <blank>
    // <blank>

    // TODO: return search FAD results
    devlog::info<grp::base>("Get frame address search results command is unimplemented");

    // Output structure:
    // status code        <blank>
    // sector position
    // partition number   frame address bits 23-16
    // frame address bits 15-0
    m_CR[0] = (m_status.statusCode << 8u);
    m_CR[1] = 0; // TODO: sector position
    m_CR[2] = 0; // TODO: partition number, FAD high
    m_CR[3] = 0; // TODO: FAD low

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdSetSectorLength() {
    devlog::trace<grp::base>("-> Set sector length");

    // Input structure:
    // 0x60               get sector length
    // put sector length  <blank>
    // <blank>
    // <blank>
    const uint8 getSectorLength = bit::extract<0, 7>(m_CR[0]);
    const uint8 putSectorLength = bit::extract<8, 15>(m_CR[1]);

    static constexpr uint32 kSectorLengths[] = {
        2048, // user data
        2336, // + subheader (checksum, ECC)
        2340, // + header (sector offset and mode)
        2352, // + sync bytes
    };

    if (getSectorLength < 4) {
        m_getSectorLength = kSectorLengths[getSectorLength];
    }
    if (putSectorLength < 4) {
        m_putSectorLength = kSectorLengths[putSectorLength];
    }
    devlog::debug<grp::base>("Sector lengths: get={} put={}", m_getSectorLength, m_putSectorLength);

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ESEL);
}

void CDBlock::CmdGetSectorData() {
    devlog::trace<grp::base>("-> Get sector data");

    // Input structure:
    // 0x61               <blank>
    // sector offset
    // partition number   <blank>
    // sector number
    const uint16 sectorOffset = m_CR[1];
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint16 sectorNumber = m_CR[3];

    bool reject = false;
    if (partitionNumber >= kNumPartitions) [[unlikely]] {
        reject = true;
    } else if (m_partitionManager.GetBufferCount(partitionNumber) == 0) [[unlikely]] {
        reject = true;
    } else {
        SetupGetSectorTransfer(sectorOffset, sectorNumber, partitionNumber, false);
        // TODO: should set status flag kStatusFlagXferRequest until ready
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_DRDY | kHIRQ_EHST);
}

void CDBlock::CmdDeleteSectorData() {
    devlog::trace<grp::base>("-> Delete sector data");

    // Input structure:
    // 0x62               <blank>
    // sector offset
    // partition number   <blank>
    // sector number
    const uint16 sectorOffset = m_CR[1];
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint16 sectorNumber = m_CR[3];

    bool reject = false;
    if (partitionNumber >= kNumPartitions) [[unlikely]] {
        reject = true;
    } else if (m_partitionManager.GetBufferCount(partitionNumber) == 0) [[unlikely]] {
        reject = true;
    } else {
        const uint32 numFreedSectors = m_partitionManager.DeleteSectors(partitionNumber, sectorOffset, sectorNumber);
        devlog::trace<grp::base>("Freed {} sectors from partition {} at offset {}", numFreedSectors, partitionNumber,
                                 sectorOffset);
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EHST);
}

void CDBlock::CmdGetThenDeleteSectorData() {
    devlog::trace<grp::base>("-> Get then delete sector data");

    // Input structure:
    // 0x63               <blank>
    // sector offset
    // partition number   <blank>
    // sector number
    const uint16 sectorOffset = m_CR[1];
    const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    const uint16 sectorNumber = m_CR[3];

    bool reject = false;
    if (partitionNumber >= kNumPartitions) [[unlikely]] {
        reject = true;
    } else if (m_partitionManager.GetBufferCount(partitionNumber) == 0) [[unlikely]] {
        reject = true;
    } else {
        SetupGetSectorTransfer(sectorOffset, sectorNumber, partitionNumber, true);
        // TODO: should set status flag kStatusFlagXferRequest until ready
    }

    // Output structure: standard CD status data
    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_DRDY | kHIRQ_EHST);
}

void CDBlock::CmdPutSectorData() {
    devlog::trace<grp::base>("-> Put sector data");

    // Input structure:
    // 0x64               <blank>
    // <blank>
    // partition number   <blank>
    // sector number
    // const uint8 partitionNumber = bit::extract<8, 15>(m_CR[2]);
    // const uint16 sectorNumber = m_CR[3];

    // TODO: setup sector write transfer
    // TODO: raise kHIRQ_EHST if not enough buffer space available
    // TODO: should set status flag kStatusFlagXferRequest until ready
    devlog::info<grp::base>("Put sector data command is unimplemented");

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_DRDY);
}

void CDBlock::CmdCopySectorData() {
    devlog::trace<grp::base>("-> Copy sector data");

    // Input structure:
    // 0x65                      destination filter number
    // sector offset
    // source partition number   <blank>
    // sector number
    // const uint8 dstPartitionNumber = bit::extract<0, 7>(m_CR[0]);
    // const uint16 sectorOffset = m_CR[1];
    // const uint8 srcPartitionNumber = bit::extract<8, 15>(m_CR[2]);
    // const uint16 sectorNumber = m_CR[3];

    // TODO: setup async sector copy transfer
    // TODO: report Reject status if not enough buffer space available
    devlog::info<grp::base>("Copy sector data command is unimplemented");

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ECPY);
}

void CDBlock::CmdMoveSectorData() {
    devlog::trace<grp::base>("-> Move sector data");

    // Input structure:
    // 0x66                      destination filter number
    // sector offset
    // source partition number   <blank>
    // sector number
    // const uint8 dstPartitionNumber = bit::extract<0, 7>(m_CR[0]);
    // const uint16 sectorOffset = m_CR[1];
    // const uint8 srcPartitionNumber = bit::extract<8, 15>(m_CR[2]);
    // const uint16 sectorNumber = m_CR[3];

    // TODO: setup async sector move transfer
    devlog::info<grp::base>("Move sector data command is unimplemented");

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_ECPY);
}

void CDBlock::CmdGetCopyError() {
    devlog::trace<grp::base>("-> Get copy error");

    // Input structure:
    // 0x67     <blank>
    // <blank>
    // <blank>
    // <blank>

    devlog::info<grp::base>("Get copy error command is unimplemented");

    // Output structure:
    // status code   error code
    // <blank>
    // <blank>
    // <blank>
    m_CR[0] = (m_status.statusCode << 8u) | 0x00; // TODO: async copy/move error code
    m_CR[1] = 0x0000;
    m_CR[2] = 0x0000;
    m_CR[3] = 0x0000;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdChangeDirectory() {
    devlog::trace<grp::base>("-> Change directory");

    // Input structure:
    // 0x70            <blank>
    // <blank>
    // filter number   file ID bits 23-16
    // file ID bits 15-0
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);
    const uint32 fileID = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    // Output structure: standard CD status data
    bool reject = false;
    if (filterNumber < m_filters.size()) {
        // TODO: use filter to read the sector(s) containing the directory record
        reject = !m_fs.ChangeDirectory(fileID);
        if (!reject) {
            devlog::debug<grp::base>("Changed directory to file ID {:X} using filter {}", fileID, filterNumber);
        }
    } else if (filterNumber == 0xFF) {
        reject = true;
    }

    if (reject) [[unlikely]] {
        ReportCDStatus(kStatusReject);
    } else {
        ReportCDStatus();
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS);
}

void CDBlock::CmdReadDirectory() {
    devlog::trace<grp::base>("-> Read directory");

    // Input structure:
    // 0x71            <blank>
    // <blank>
    // filter number   file ID bits 23-16
    // file ID bits 15-0
    // const uint8 filterNum = bit::extract<8, 15>(m_CR[2]);
    // const uint32 fileID = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    // TODO: read directory contents starting from fileID
    // TODO: write sectors to specified filter
    devlog::info<grp::base>("Read directory command is unimplemented");

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS);
}

void CDBlock::CmdGetFileSystemScope() {
    devlog::trace<grp::base>("-> Get file system scope");

    // Input structure:
    // 0x72     <blank>
    // <blank>
    // <blank>
    // <blank>

    // Output structure:
    // status code            <blank>
    // index number
    // directory end offset   first file ID bits 23-16
    // first file ID bits 15-0

    const uint32 fileOffset = m_fs.GetFileOffset() + 2;
    const uint32 fileCount = m_fs.GetFileCount();
    const bool endOfDirectory = fileOffset + 254 >= fileCount;
    m_CR[0] = m_status.statusCode << 8u;
    m_CR[1] = fileCount;
    m_CR[2] = (endOfDirectory << 8u) | bit::extract<16, 23>(fileOffset);
    m_CR[3] = bit::extract<0, 15>(fileOffset);

    devlog::trace<grp::base>("Get file system scope: {} files from offset {}, {}", fileCount, fileOffset,
                             (endOfDirectory ? "end of list" : "more files available"));

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS);
}

void CDBlock::CmdGetFileInfo() {
    devlog::trace<grp::base>("-> Get file info");

    // Input structure:
    // 0x73     <blank>
    // <blank>
    // <blank>  file ID bits 23-16
    // file ID bits 15-0
    const uint32 fileID = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    bool reject = false;
    if (!m_fs.IsValid() || !m_fs.HasCurrentDirectory()) {
        reject = true;
    }
    if (fileID != 0xFFFFFF && fileID > 2 + m_fs.GetFileCount() - m_fs.GetFileOffset()) {
        reject = true;
    }

    if (!reject) [[likely]] {
        const uint32 numFileInfos = SetupFileInfoTransfer(fileID);

        // Output structure:
        // status code            <blank>
        // file info size in words
        // <blank>
        // <blank>
        m_CR[0] = m_status.statusCode << 8u;
        m_CR[1] = numFileInfos * 12 / sizeof(uint16);
        m_CR[2] = 0x0000;
        m_CR[3] = 0x0000;
        SetInterrupt(kHIRQ_DRDY);
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdReadFile() {
    devlog::trace<grp::base>("-> Read file");

    // Input structure:
    // 0x74            offset bits 23-16
    // offset bits 15-0
    // filter number   file ID bits 23-16
    // file ID bits 15-0
    const uint32 offset = (bit::extract<0, 7>(m_CR[0]) << 16u) | m_CR[1];
    const uint8 filterNumber = bit::extract<8, 15>(m_CR[2]);
    const uint32 fileID = (bit::extract<0, 7>(m_CR[2]) << 16u) | m_CR[3];

    if (SetupFilePlayback(fileID, offset, filterNumber)) {
        // Output structure: standard CD status data
        ReportCDStatus();
    } else {
        ReportCDStatus(kStatusReject);
    }

    SetInterrupt(kHIRQ_CMOK | kHIRQ_DRDY);
}

void CDBlock::CmdAbortFile() {
    devlog::trace<grp::base>("-> Abort file");

    // Input structure:
    // 0x75     <blank>
    // <blank>
    // <blank>
    // <blank>

    devlog::debug<grp::base>("Aborting transfer");

    EndTransfer();

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK | kHIRQ_EFLS);
}

void CDBlock::CmdMpegGetStatus() {}

void CDBlock::CmdMpegGetInterrupt() {}

void CDBlock::CmdMpegSetInterruptMask() {}

void CDBlock::CmdMpegInit() {
    devlog::trace<grp::base>("-> MPEG init");

    // Input structure:
    // 0x93     <blank>
    // <blank>
    // <blank>
    // <blank>

    // TODO: initialize MPEG stuff
    devlog::info<grp::base>("MPEG init command is unimplemented");

    // Output structure:
    // status code (FF=unauthenticated)  <blank>
    // <blank>
    // <blank>
    // <blank>
    m_CR[0] = 0xFF00;
    m_CR[1] = 0;
    m_CR[2] = 0;
    m_CR[3] = 0;

    SetInterrupt(kHIRQ_CMOK | kHIRQ_MPED | kHIRQ_MPST);
}

void CDBlock::CmdMpegSetMode() {}

void CDBlock::CmdMpegPlay() {}

void CDBlock::CmdMpegSetDecodingMethod() {}

void CDBlock::CmdMpegSetConnection() {}

void CDBlock::CmdMpegGetConnection() {}

void CDBlock::CmdMpegSetStream() {}

void CDBlock::CmdMpegGetStream() {}

void CDBlock::CmdMpegDisplay() {}

void CDBlock::CmdMpegSetWindow() {}

void CDBlock::CmdMpegSetBorderColor() {}

void CDBlock::CmdMpegSetFade() {}

void CDBlock::CmdMpegSetVideoEffects() {}

void CDBlock::CmdMpegSetLSI() {}

void CDBlock::CmdAuthenticateDevice() {
    devlog::trace<grp::base>("-> Authenticate device");

    // Input structure:
    // 0xE1    <blank>
    // authentication type (0x0000=CD, 0x0001=MPEG)
    // <blank>
    // <blank>

    const uint16 authType = m_CR[1];

    switch (authType) {
    case 0x0000:
        devlog::debug<grp::base>("Performing CD authentication");
        m_discAuthStatus = 4; // always authenticated ;)
        SetInterrupt(kHIRQ_EFLS | kHIRQ_CSCT);
        break;
    case 0x0001:
        devlog::debug<grp::base>("Performing MPEG authentication");
        m_mpegAuthStatus = 2;
        SetInterrupt(kHIRQ_MPED);
        break;
    default:
        devlog::debug<grp::base>("Authenticate device parameter invalid: unexpected authentication type {}", authType);
        break;
    }

    // TODO: make busy for a brief moment
    m_status.statusCode = kStatusCodePause;
    m_targetDriveCycles = kDriveCyclesNotPlaying;

    // Output structure: standard CD status data
    ReportCDStatus();

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdIsDeviceAuthenticated() {
    devlog::trace<grp::base>("-> Is device authenticated");

    // Input structure:
    // 0xE2    <blank>
    // authentication type (0x0000=CD, 0x0001=MPEG)
    // <blank>
    // <blank>

    const uint16 authType = m_CR[1];

    // Output structure:
    // status code  <blank>
    // authentication status
    // <blank>
    // <unknown>
    m_CR[0] = (m_status.statusCode << 8u);
    m_CR[1] = authType == 0x0000 ? m_discAuthStatus : m_mpegAuthStatus;
    m_CR[2] = 0;
    m_CR[3] = 0;

    SetInterrupt(kHIRQ_CMOK);
}

void CDBlock::CmdGetMpegROM() {}

// -----------------------------------------------------------------------------
// Probe implementation

CDBlock::Probe::Probe(CDBlock &cdblock)
    : m_cdblock(cdblock) {}

uint8 CDBlock::Probe::GetCurrentStatusCode() const {
    return m_cdblock.m_status.statusCode & 0xF;
}

uint32 CDBlock::Probe::GetCurrentFrameAddress() const {
    return m_cdblock.m_status.frameAddress;
}

uint8 CDBlock::Probe::GetCurrentRepeatCount() const {
    return m_cdblock.m_status.repeatCount & 0xF;
}

uint8 CDBlock::Probe::GetMaxRepeatCount() const {
    return m_cdblock.m_playMaxRepeat;
}

uint8 CDBlock::Probe::GetCurrentControlADRBits() const {
    return m_cdblock.m_status.controlADR;
}

uint8 CDBlock::Probe::GetCurrentTrack() const {
    return m_cdblock.m_status.track;
}

uint8 CDBlock::Probe::GetCurrentIndex() const {
    return m_cdblock.m_status.index;
}

uint8 CDBlock::Probe::GetReadSpeed() const {
    return m_cdblock.m_readSpeed;
}

} // namespace ymir::cdblock
