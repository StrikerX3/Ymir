#include <ymir/hw/cdblock/cd_drive.hpp>

#include "cdblock_devlog.hpp"

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/dev_assert.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/inline.hpp>

#include <numeric>

namespace ymir::cdblock {

CDDrive::CDDrive(core::Scheduler &scheduler)
    : m_scheduler(scheduler) {

    // TODO: should reuse/replace the original CD Block drive state once the transition is done
    m_stateEvent = m_scheduler.RegisterEvent(
        core::events::CDBlockLLEDriveState, this, [](core::EventContext &eventContext, void *userContext) {
            const uint64 cycleInterval = static_cast<CDDrive *>(userContext)->ProcessTxState();
            eventContext.Reschedule(cycleInterval);
        });

    m_autoCloseTray = false;

    Reset();
}

void CDDrive::Reset() {
    m_command.data.fill(0x00);
    m_commandPos = 0u;

    m_status.operation = Operation::Zero;
    m_status.subcodeQ = 0x00;
    m_status.trackNum = 0x00;
    m_status.indexNum = 0x00;
    m_status.min = m_status.absMin = 0x00;
    m_status.sec = m_status.absSec = 0x00;
    m_status.frac = m_status.absFrac = 0x00;
    m_status.zero = 0x00;
    m_statusData.data.fill(0x00);
    m_statusPos = 0u;

    m_state = TxState::Reset;

    m_currFAD = 0u;
    m_targetFAD = 0u;

    m_readSpeed = 1;

    m_scheduler.ScheduleAt(m_stateEvent, 0);
}

void CDDrive::UpdateClockRatios(const sys::ClockRatios &clockRatios) {
    // Drive state updates is counted in thirds, as explained in cdblock_defs.hpp
    m_scheduler.SetEventCountFactor(m_stateEvent, clockRatios.CDBlockNum * 3, clockRatios.CDBlockDen);
}

void CDDrive::LoadDisc(media::Disc &&disc) {
    m_disc.Swap(std::move(disc));
    if (m_fs.Read(m_disc)) {
        devlog::info<grp::base>("Filesystem built successfully");
    } else {
        devlog::warn<grp::base>("Failed to build filesystem");
    }
    OpenTray(true);
}

void CDDrive::EjectDisc() {
    m_disc = {};
    m_fs.Clear();
    OpenTray(true);
}

void CDDrive::OpenTray() {
    OpenTray(false);
}

void CDDrive::CloseTray() {
    if (m_status.operation != Operation::TrayOpen) {
        return;
    }

    m_autoCloseTray = false;
    if (m_disc.sessions.empty()) {
        m_status.operation = Operation::NoDisc;
    } else {
        m_status.operation = Operation::DiscChanged;
        m_currFAD = 0;
    }
}

XXH128Hash CDDrive::GetDiscHash() const {
    return m_fs.GetHash();
}

FORCE_INLINE void CDDrive::OpenTray(bool autoClose) {
    m_status.operation = Operation::TrayOpen;
    m_autoCloseTray = autoClose;
}

bool CDDrive::SerialRead() {
    const uint8 byteIndex = m_commandPos >> 3u;
    const uint8 bitIndex = m_commandPos & 7u;
    const bool bit = (m_statusData.data[byteIndex] >> bitIndex) & 1;
    if (++m_statusPos == (m_statusData.data.size() << 3u)) {
        m_statusPos = 0;
    }
    return bit;
}

void CDDrive::SerialWrite(bool bit) {
    const uint8 byteIndex = m_commandPos >> 3u;
    const uint8 bitIndex = m_commandPos & 7u;
    m_command.data[byteIndex] &= ~(1u << bitIndex);
    m_command.data[byteIndex] |= bit << bitIndex;
    if ((++m_commandPos & 7u) == 0u) {
        if (m_commandPos == (m_command.data.size() << 3u)) {
            m_commandPos = 0;
            m_state = TxState::TxEnd;

            if constexpr (devlog::trace_enabled<grp::lle_cd_status>) {
                fmt::memory_buffer buf{};
                auto out = std::back_inserter(buf);
                fmt::format_to(out, "CD stat ");
                for (uint8 b : m_statusData.data) {
                    fmt::format_to(out, " {:02X}", b);
                }
                devlog::trace<grp::lle_cd_status>("{}", fmt::to_string(buf));
            }
        } else if (m_commandPos == (1 << 3u)) {
            m_state = TxState::TxInter1;
        } else {
            m_state = TxState::TxInterN;
        }
        m_cbSetCOMREQn(true);
        m_cbSetCOMSYNCn(true);
    }
}

uint64 CDDrive::ProcessTxState() {
    // Signalling based on:
    //   https://web.archive.org/web/20111203080908/http://www.crazynation.org/SEGA/Saturn/cd_tech.htm
    // where:
    //   Start Strobe  = COMSYNC# = PB2
    //   Output Enable = COMREQ#  = TIOCB3
    //
    // State sequence:                                        repeat this 11 times
    //          Reset ... PreTx TxBegin TxByte (tx) TxInter1 [TxByte (tx) TxInterN] TxByte (tx) TxEnd PreTx ...
    // COMREQ#   HI        HI     HI      LO    HI     HI      LO     HI     HI       LO    HI   HI     HI
    // COMSYNC#  HI        HI     LO      LO    LO     HI      HI     HI     HI       HI    HI   HI     HI
    //
    // (tx) denote byte transfers

    // TODO: proper timings between states

    switch (m_state) {
    case TxState::Reset:
        m_status.operation = Operation::Idle;
        OutputDriveStatus();
        m_cbSetCOMSYNCn(true);
        m_cbSetCOMREQn(true);
        m_state = TxState::PreTx;
        return kTxCyclesPowerOn + kTxCyclesFirstTx;

    case TxState::PreTx: m_state = TxState::TxBegin; return kTxCyclesBeginTx;

    case TxState::TxBegin:
        m_cbSetCOMSYNCn(false);
        m_state = TxState::TxByte;
        return kTxCyclesInterTx;

    case TxState::TxByte: m_cbSetCOMREQn(false); return kTxCyclesPerByte;

    case TxState::TxInter1:
        m_cbSetCOMREQn(true);
        m_state = TxState::TxByte;
        return kTxCyclesInterTx;

    case TxState::TxInterN: m_state = TxState::TxByte; return kTxCyclesInterTx;

    // TODO: need to compensate for time spent transmitting serial data
    case TxState::TxEnd: //
    {
        // ProcessCommand() also handles the state change
        const uint64 cycles = ProcessCommand();
        m_state = TxState::PreTx;
        return cycles > kTxCyclesTotal ? cycles - kTxCyclesTotal : 1;
    }
    }

    // Invalid state; shouldn't happen
    devlog::trace<grp::lle_cd>("Processing illegal TX state {:X}", static_cast<uint8>(m_state));
    m_state = TxState::PreTx;
    return kTxCyclesBeginTx;
}

FORCE_INLINE uint64 CDDrive::ProcessCommand() {
    if constexpr (devlog::trace_enabled<grp::lle_cd_cmd>) {
        fmt::memory_buffer buf{};
        auto out = std::back_inserter(buf);
        fmt::format_to(out, "CD cmd  ");
        for (uint8 b : m_command.data) {
            fmt::format_to(out, " {:02X}", b);
        }
        devlog::trace<grp::lle_cd_cmd>("{}", fmt::to_string(buf));
    }

    if (m_command.command != Command::Noop) {
        GetReadSpeedFactor();
    }

    switch (m_command.command) {
    case Command::Noop: return ProcessOperation();
    case Command::SeekRing: return CmdSeekRing();
    case Command::ReadTOC: return CmdReadTOC();
    case Command::Stop: return CmdStop();
    case Command::ReadSector: return CmdReadSector();
    case Command::Pause: return CmdPause();
    case Command::SeekSector: return CmdSeekSector();
    case Command::ScanForwards: return CmdScan(true);
    case Command::ScanBackwards: return CmdScan(false);
    default: return CmdUnknown();
    }
}

FORCE_INLINE uint64 CDDrive::ProcessOperation() {
    switch (m_status.operation) {
    case Operation::ReadTOC: return OpReadTOC();
    case Operation::Stopped: return OpStopped();
    case Operation::Seek: [[fallthrough]];
    case Operation::SeekSecurityRingB2: [[fallthrough]];
    case Operation::SeekSecurityRingB6: return OpSeek();
    case Operation::ReadAudioSector: [[fallthrough]];
    case Operation::ReadDataSector: return OpReadSector();
    case Operation::Idle: return OpIdle();
    case Operation::TrayOpen: return OpTrayOpen();
    default: return OpUnknown();
    }
}

FORCE_INLINE uint64 CDDrive::CmdReadTOC() {
    devlog::debug<grp::lle_cd>("Read TOC");
    m_currTOCEntry = 0;
    m_currTOCRepeat = 0;
    return ReadTOC();
}

FORCE_INLINE uint64 CDDrive::CmdSeekRing() {
    devlog::debug<grp::lle_cd>("Seek security ring");

    SetupSeek(false);

    m_status.operation = Operation::SeekSecurityRingB6;
    OutputRingStatus();

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE uint64 CDDrive::CmdSeekSector() {
    return BeginSeek(false);
}

FORCE_INLINE uint64 CDDrive::CmdReadSector() {
    return BeginSeek(true);
}

FORCE_INLINE uint64 CDDrive::CmdPause() {
    devlog::debug<grp::lle_cd>("Pause");
    m_status.operation = Operation::Idle;

    OutputDriveStatus();

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE uint64 CDDrive::CmdStop() {
    devlog::debug<grp::lle_cd>("Stop");
    m_status.operation = Operation::Stopped;

    OutputDriveStatus();

    return kDriveCyclesPlaying1x / m_readSpeed;
}
FORCE_INLINE uint64 CDDrive::CmdScan(bool fwd) {
    devlog::debug<grp::lle_cd>("Scan {}", (fwd ? "forwards" : "backwards"));
    // TODO: implement
    YMIR_DEV_CHECK();

    m_status.operation = Operation::Idle;

    OutputDriveStatus();

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE uint64 CDDrive::CmdUnknown() {
    devlog::debug<grp::lle_cd>("Unknown command {:02X}", static_cast<uint8>(m_command.command));
    m_status.operation = Operation::Idle;

    OutputDriveStatus();

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE uint64 CDDrive::OpReadTOC() {
    return ReadTOC();
}

FORCE_INLINE uint64 CDDrive::OpStopped() {
    OutputDriveStatus();

    return kDriveCyclesNotPlaying;
}

FORCE_INLINE uint64 CDDrive::OpSeek() {
    OutputDriveStatus();

    if (--m_seekCountdown == 0) {
        m_status.operation = m_seekOp;
        devlog::debug<grp::lle_cd>("Seek done");
    }

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE uint64 CDDrive::OpReadSector() {
    if (m_disc.sessions.empty()) {
        devlog::debug<grp::lle_cd>("Read sector - no disc");
        m_status.operation = Operation::NoDisc;
        return kDriveCyclesPlaying1x / m_readSpeed;
    }

    devlog::debug<grp::lle_cd>("Read sector {:06X}", m_currFAD);

    const auto &session = m_disc.sessions.back();
    const auto *track = session.FindTrack(m_currFAD);
    const bool isData = track == nullptr || (track->controlADR & 0x40);
    m_status.operation = isData ? Operation::ReadDataSector : Operation::ReadAudioSector;

    uint64 cycles = kDriveCyclesPlaying1x / m_readSpeed;
    if (m_currFAD > session.endFrameAddress) {
        // Security ring area
        m_sectorDataBuffer.fill(0);

        uint16 lfsr = 1u;
        for (uint32 i = 12u; i < 2352u; i++) {
            uint8 a = (i & 1) ? 0x59u : 0xA8u;
            for (uint32 j = 0u; j < 8u; j++) {
                uint32 x = a;
                a = std::rotr<uint8>(a ^ (lfsr & 1u), 1u);
                x = (lfsr >> 1u) ^ lfsr;
                lfsr = (lfsr | (x << 15u)) >> 1u;
            }
            m_sectorDataBuffer[i] = a;
        }

        // Sync bytes (for CRC calculation)
        for (uint32 i = 1; i <= 10; ++i) {
            m_sectorDataBuffer[i] = 0xFF;
        }

        m_sectorDataBuffer[12] = util::to_bcd(m_currFAD / 75 / 60);
        m_sectorDataBuffer[13] = util::to_bcd(m_currFAD / 75 % 60);
        m_sectorDataBuffer[14] = util::to_bcd(m_currFAD % 75);
        m_sectorDataBuffer[15] = 0x02; // Mode 2 form 2
        m_sectorDataBuffer[16] = m_sectorDataBuffer[20] = 0x00;
        m_sectorDataBuffer[17] = m_sectorDataBuffer[21] = 0x00;
        m_sectorDataBuffer[18] = m_sectorDataBuffer[22] = 0x1C;
        m_sectorDataBuffer[19] = m_sectorDataBuffer[23] = 0x00;

        const uint32 crc = media::CalcCRC(std::span<uint8, 2064>{std::span<uint8>{m_sectorDataBuffer}.first(2064)});
        util::WriteLE<uint32>(&m_sectorDataBuffer[2348], crc);
    } else if (track == nullptr || !track->ReadSector(m_currFAD, m_sectorDataBuffer)) {
        // Lead-in area or unavailable/empty sector
        m_sectorDataBuffer.fill(0);
        m_sectorDataBuffer[12] = util::to_bcd(m_currFAD / 75 / 60);
        m_sectorDataBuffer[13] = util::to_bcd(m_currFAD / 75 % 60);
        m_sectorDataBuffer[14] = util::to_bcd(m_currFAD % 75);
        m_sectorDataBuffer[15] = 0x01;
    }

    if (isData) {
        // Skip the sync bytes
        m_cbDataSector(std::span<uint8>(m_sectorDataBuffer).subspan(12));
    } else {
        // The callback returns how many thirds of the buffer are full
        const uint32 currBufferLength = m_cbCDDASector(m_sectorDataBuffer);

        // Adjust pace based on how full the SCSP CDDA buffer is
        if (currBufferLength < 1) {
            // Run faster if the buffer is less than a third full
            cycles = kDriveCyclesPlaying1x - (kDriveCyclesPlaying1x >> 2);
        } else if (currBufferLength >= 2) {
            // Run slower if the buffer is more than two-thirds full
            cycles = kDriveCyclesPlaying1x + (kDriveCyclesPlaying1x >> 2);
        } else {
            // Normal speed otherwise
            cycles = kDriveCyclesPlaying1x;
        }
    }
    ++m_currFAD;

    m_cbSectorTransferDone();

    OutputDriveStatus();

    // Need to fudge cycles, otherwise SH-1 rejects the transfers
    static constexpr sint64 kCyclesFudge = +2600;
    if (isData) {
        cycles += kCyclesFudge;
    }
    return cycles;
}

FORCE_INLINE uint64 CDDrive::OpIdle() {
    ++m_currFAD;
    if (m_currFAD > m_targetFAD + 5) {
        m_currFAD = m_targetFAD;
    }

    OutputDriveStatus();

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE uint64 CDDrive::OpTrayOpen() {
    OutputDriveStatus();

    if (m_autoCloseTray) {
        CloseTray();
    }

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE uint64 CDDrive::OpUnknown() {
    OutputDriveStatus();

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE void CDDrive::GetReadSpeedFactor() {
    // TODO: apply read speed tweak
    m_readSpeed = m_command.readSpeed == 1 ? 1 : 2;
}

FORCE_INLINE void CDDrive::SetupSeek(bool read) {
    const uint32 fad = (m_command.fadTop << 16u) | (m_command.fadMid << 8u) | m_command.fadBtm;
    m_currFAD = fad - 4;
    m_targetFAD = fad - 4;
    if (read) {
        if (m_disc.sessions.empty()) {
            m_seekOp = Operation::NoDisc;
        } else {
            const auto &session = m_disc.sessions.back();
            const auto *track = session.FindTrack(fad);
            if (track == nullptr || (track->controlADR & 0x40)) {
                m_seekOp = Operation::ReadDataSector;
            } else {
                m_seekOp = Operation::ReadAudioSector;
            }
        }
    } else {
        m_seekOp = Operation::Idle;
    }
    m_seekCountdown = 9;
    devlog::debug<grp::lle_cd>("Seek to FAD {:06X} then {}", fad, (read ? "read" : "pause"));
}

FORCE_INLINE uint64 CDDrive::BeginSeek(bool read) {
    SetupSeek(read);

    m_status.operation = Operation::Seek;
    OutputDriveStatus();

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE uint64 CDDrive::ReadTOC() {
    // No disc
    if (m_disc.sessions.empty()) {
        m_status.operation = Operation::NoDisc;
        return kDriveCyclesPlaying1x / m_readSpeed;
    }

    auto &session = m_disc.sessions.back();

    // Copy TOC entry to status output
    if (m_currTOCRepeat == 0 && m_currTOCEntry < session.leadInTOCCount) {
        auto &tocEntry = session.leadInTOC[m_currTOCEntry];
        m_statusData.data[0] = static_cast<uint8>(Operation::ReadTOC);
        m_statusData.data[1] = tocEntry.controlADR;
        m_statusData.data[2] = tocEntry.trackNum;
        m_statusData.data[3] = tocEntry.pointOrIndex;
        m_statusData.data[4] = tocEntry.min;
        m_statusData.data[5] = tocEntry.sec;
        m_statusData.data[6] = tocEntry.frac;
        m_statusData.data[7] = tocEntry.zero;
        m_statusData.data[8] = tocEntry.amin;
        m_statusData.data[9] = tocEntry.asec;
        m_statusData.data[10] = tocEntry.afrac;
        CalcStatusDataChecksum();
    }
    m_status.operation = Operation::ReadTOC;
    if (++m_currTOCRepeat == 3) {
        if (++m_currTOCEntry == session.leadInTOCCount) {
            m_status.operation = Operation::Idle;
        } else {
            m_currTOCRepeat = 0;
        }
    }

    return kDriveCyclesPlaying1x / m_readSpeed;
}

FORCE_INLINE void CDDrive::OutputDriveStatus() {
    if (m_disc.sessions.empty()) {
        m_status.subcodeQ = 0xFF;
        m_status.trackNum = 0xFF;
        m_status.indexNum = 0xFF;
        m_status.min = m_status.absMin = 0xFF;
        m_status.sec = m_status.absSec = 0xFF;
        m_status.frac = m_status.absFrac = 0xFF;
        m_status.zero = 0xFF;
    } else {
        auto &session = m_disc.sessions.back();
        if (m_currFAD > session.endFrameAddress) {
            // Lead-out
            const uint32 leadoutFAD = session.endFrameAddress + 1;
            m_status.subcodeQ = 0x01;
            m_status.trackNum = 0xAA;
            m_status.indexNum = 0x01;
            m_status.min = 0x00;
            m_status.sec = 0x00;
            m_status.frac = 0x00;
            m_status.zero = 0x04;
            m_status.absMin = util::to_bcd(m_currFAD / 75 / 60);
            m_status.absSec = util::to_bcd(m_currFAD / 75 % 60);
            m_status.absFrac = util::to_bcd(m_currFAD % 75);
        } else {
            // Tracks 01 to 99
            const bool isLeadIn = m_currFAD < 150;
            const uint8 trackIndex = isLeadIn ? 0 : session.FindTrackIndex(m_currFAD);
            const auto &track = trackIndex == 0xFF ? session.tracks[0] : session.tracks[trackIndex];
            const uint8 trackNum = trackIndex + 1;
            const uint8 indexNum = isLeadIn ? 0 : track.FindIndex(m_currFAD);
            sint32 relFAD = m_currFAD - track.startFrameAddress;
            if (relFAD < 0) {
                relFAD = -relFAD; // INDEX 00 frame addresses count downwards to 00:00:00 until start of INDEX 01
            }
            m_status.subcodeQ = track.controlADR;
            m_status.trackNum = util::to_bcd(trackNum);
            m_status.indexNum = util::to_bcd(indexNum);
            m_status.min = util::to_bcd(relFAD / 75 / 60);
            m_status.sec = util::to_bcd(relFAD / 75 % 60);
            m_status.frac = util::to_bcd(relFAD % 75);
            m_status.zero = 0x04;
            m_status.absMin = util::to_bcd(m_currFAD / 75 / 60);
            m_status.absSec = util::to_bcd(m_currFAD / 75 % 60);
            m_status.absFrac = util::to_bcd(m_currFAD % 75);
        }
    }

    m_statusData.cdStatus = m_status;
    CalcStatusDataChecksum();
}

FORCE_INLINE void CDDrive::OutputRingStatus() {
    const uint32 currFAD = m_currFAD + 4;
    m_statusData.data[0] = static_cast<uint8>(Operation::SeekSecurityRingB6);
    m_statusData.data[1] = 0x44;
    m_statusData.data[2] = 0xF1;
    m_statusData.data[3] = currFAD >> 16u;
    m_statusData.data[4] = currFAD >> 8u;
    m_statusData.data[5] = currFAD;
    m_statusData.data[6] = 0x09;
    m_statusData.data[7] = 0x09;
    m_statusData.data[8] = 0x09;
    m_statusData.data[9] = 0x09;
    m_statusData.data[10] = currFAD % 75;
    CalcStatusDataChecksum();
}

FORCE_INLINE void CDDrive::CalcStatusDataChecksum() {
    m_statusData.data[11] = ~std::accumulate(m_statusData.chksumData.begin(), m_statusData.chksumData.end(), 0);
}

} // namespace ymir::cdblock
