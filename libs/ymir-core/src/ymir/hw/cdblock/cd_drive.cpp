#include <ymir/hw/cdblock/cd_drive.hpp>

#include "cdblock_devlog.hpp"

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/dev_log.hpp>

#include <numeric>

namespace ymir::cdblock {

CDDrive::CDDrive(core::Scheduler &scheduler)
    : m_scheduler(scheduler) {

    // TODO: should reuse/replace the original CD Block drive state once the transition is done
    m_stateEvent = m_scheduler.RegisterEvent(
        core::events::CDBlockLLEDriveState, this, [](core::EventContext &eventContext, void *userContext) {
            const uint64 cycleInterval = static_cast<CDDrive *>(userContext)->ProcessState();
            eventContext.Reschedule(cycleInterval);
        });

    m_trayOpen = false;

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
    CloseTray();
    m_cbDiscChanged();
}

void CDDrive::EjectDisc() {
    m_disc = {};
    m_fs.Clear();
    CloseTray();
    m_cbDiscChanged();
}

void CDDrive::OpenTray() {
    if (!m_trayOpen) {
        m_trayOpen = true;
        m_cbDiscChanged();
        // TODO: check if this is correct
        m_status.operation = Operation::TrayOpen;
    }
}

void CDDrive::CloseTray() {
    if (m_trayOpen) {
        m_trayOpen = false;
        m_cbDiscChanged();
        // TODO: check if this is correct
        m_status.operation = Operation::NoDisc;
    }
}

XXH128Hash CDDrive::GetDiscHash() const {
    return m_fs.GetHash();
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

uint64 CDDrive::ProcessState() {
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
        UpdateStatus();
        OutputStatus();
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

    case TxState::TxEnd: return ProcessCommand(); // also handles the state change
    }

    // Invalid state; shouldn't happen
    return 1000;
}

uint64 CDDrive::ProcessCommand() {
    if constexpr (devlog::trace_enabled<grp::lle_cd_cmd>) {
        fmt::memory_buffer buf{};
        auto out = std::back_inserter(buf);
        fmt::format_to(out, "CD cmd  ");
        for (uint8 b : m_command.data) {
            fmt::format_to(out, " {:02X}", b);
        }
        devlog::trace<grp::lle_cd_cmd>("{}", fmt::to_string(buf));
    }

    // TODO: implement the remaining commmands
    switch (m_command.command) {
    case Command::Noop: return ProcessOperation();
    case Command::SeekRing: break;
    case Command::ReadTOC:
        m_currTOCEntry = 0;
        m_currTOCRepeat = 0;
        devlog::debug<grp::lle_cd>("Read TOC");
        return ReadTOC();

    case Command::Stop: break;
    case Command::ReadSector: break;
    case Command::Pause: break;
    case Command::SeekSector: return BeginSeek(Operation::Idle);
    case Command::ScanForwards: break;
    case Command::ScanBackwards: break;
    default: break;
    }

    // Invalid command; shouldn't happen
    return 1000;
}

uint64 CDDrive::ProcessOperation() {
    switch (m_status.operation) {
    case Operation::Zero:
        // Default value at boot-up, theoretically shouldn't ever be processed
        break;

    case Operation::ReadTOC: return ReadTOC();

    case Operation::Stopped: m_state = TxState::PreTx; break;

    case Operation::Seek: [[fallthrough]];
    case Operation::SeekSecurityRingB2: [[fallthrough]];
    case Operation::SeekSecurityRingB6:
        m_state = TxState::PreTx;
        UpdateStatus();
        OutputStatus();

        if (--m_seekCountdown == 0) {
            m_status.operation = m_seekOp;
            devlog::debug<grp::lle_cd>("Seek done");
        }
        break;

    case Operation::ReadAudioSector: [[fallthrough]];
    case Operation::ReadDataSector:
        // TODO: implement
        break;

    case Operation::Idle:
        m_state = TxState::PreTx;

        ++m_currFAD;
        if (m_currFAD > m_targetFAD + 5) {
            m_currFAD = m_targetFAD;
        }

        UpdateStatus();
        OutputStatus();
        break;

    default: m_state = TxState::PreTx; break;
    }

    // Invalid operation; shouldn't happen
    return 1000;
}

uint64 CDDrive::GetReadSpeedFactor() const {
    // TODO: apply read speed tweak
    return m_command.readSpeed == 1 ? 1 : 2;
}

uint64 CDDrive::ReadTOC() {
    const uint8 readSpeed = GetReadSpeedFactor();

    // No disc
    if (m_disc.sessions.empty()) {
        m_status.operation = Operation::NoDisc;
        m_state = TxState::PreTx;
        return kDriveCyclesPlaying1x / readSpeed;
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
    m_state = TxState::PreTx;

    return kDriveCyclesPlaying1x / readSpeed;
}

uint64 CDDrive::BeginSeek(Operation op) {
    const uint8 readSpeed = GetReadSpeedFactor();

    const uint32 fad = (m_command.fadTop << 16u) | (m_command.fadMid << 8u) | m_command.fadBtm;
    m_currFAD = fad - 4;
    m_targetFAD = fad - 4;
    m_seekOp = op;
    m_seekCountdown = 9;
    devlog::debug<grp::lle_cd>("Seek to FAD {:06X}", fad);

    m_status.operation = Operation::Seek;
    m_state = TxState::PreTx;
    UpdateStatus();
    OutputStatus();

    return kDriveCyclesPlaying1x / readSpeed;
}

void CDDrive::UpdateStatus() {
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
            m_status.absMin = util::to_bcd(leadoutFAD / 75 / 60);
            m_status.absSec = util::to_bcd(leadoutFAD / 75 % 60);
            m_status.absFrac = util::to_bcd(leadoutFAD % 75);
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
}

void CDDrive::OutputStatus() {
    m_statusData.cdStatus = m_status;
    CalcStatusDataChecksum();
}

void CDDrive::CalcStatusDataChecksum() {
    m_statusData.data[11] = ~std::accumulate(m_statusData.chksumData.begin(), m_statusData.chksumData.end(), 0);
}

} // namespace ymir::cdblock
