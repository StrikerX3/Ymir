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
    m_commandBit = 0u;
    m_commandIndex = 0u;

    m_status.data.fill(0x00);
    m_statusBit = 0u;
    m_statusIndex = 0u;

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
    const bool bit = (m_status.data[m_statusIndex] >> m_statusBit) & 1;
    if (++m_statusBit == 8) {
        m_statusBit = 0;
        if (++m_statusIndex == m_status.data.size()) {
            m_statusIndex = 0;
        }
    }
    return bit;
}

void CDDrive::SerialWrite(bool bit) {
    m_command.data[m_commandIndex] &= ~(1u << m_commandBit);
    m_command.data[m_commandIndex] |= bit << m_commandBit;
    if (++m_commandBit == 8) {
        m_commandBit = 0;
        if (++m_commandIndex == m_command.data.size()) {
            m_commandIndex = 0;
            m_state = TxState::TxEnd;

            if constexpr (devlog::trace_enabled<grp::lle_cd_status>) {
                fmt::memory_buffer buf{};
                auto out = std::back_inserter(buf);
                fmt::format_to(out, "CD stat ");
                for (uint8 b : m_status.data) {
                    fmt::format_to(out, " {:02X}", b);
                }
                devlog::trace<grp::lle_cd_status>("{}", fmt::to_string(buf));
            }
        } else if (m_commandIndex == 1) {
            m_state = TxState::TxInter1;
        } else {
            m_state = TxState::TxInterN;
        }
        m_cbSetCOMREQn(true);
        m_cbSetCOMSYNCn(true);
    }
}

uint64 CDDrive::ProcessState() {
    // TODO: proper timings between states

    // Signalling based on:
    //   https://web.archive.org/web/20111203080908/http://www.crazynation.org/SEGA/Saturn/cd_tech.htm
    // where:
    //   Start Strobe  = COMSYNC# = PB2
    //   Output Enable = COMREQ#  = TIOCB3
    //
    // State sequence:                                       repeat this 11 times
    //          Reset ... Noop TxBegin TxByte (tx) TxInter1 [TxByte (tx) TxInterN] TxByte (tx) TxEnd Noop ...
    // COMREQ#   HI        HI    HI      LO    HI     HI      LO     HI     HI       LO    HI   HI    HI
    // COMSYNC#  HI        HI    LO      LO    LO     HI      HI     HI     HI       HI    HI   HI    HI
    //
    // (tx) denote byte transfers

    switch (m_state) {
    case TxState::Reset:
        m_status.operation = Operation::Idle;
        UpdateStatus();
        m_cbSetCOMSYNCn(true);
        m_cbSetCOMREQn(true);
        m_state = TxState::Noop;
        return 18000000 * 3;

    case TxState::Noop: m_state = TxState::TxBegin; return 10000 * 3;

    case TxState::TxBegin:
        m_cbSetCOMSYNCn(false);
        m_state = TxState::TxByte;
        return 10000 * 3;

    case TxState::TxByte: m_cbSetCOMREQn(false); return 10000 * 3;

    case TxState::TxInter1:
        m_cbSetCOMREQn(true);
        m_state = TxState::TxByte;
        return 10000 * 3;

    case TxState::TxInterN: m_state = TxState::TxByte; return 10000 * 3;

    case TxState::TxEnd: return ProcessCommand(); // also handles the state change
    }

    return 10000 * 3;
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

    // TODO: implement
    switch (m_command.command) {
    case Command::Noop: return ProcessOperation();
    case Command::SeekRing: break;
    case Command::ReadTOC: break;
    case Command::Stop: break;
    case Command::ReadSector: break;
    case Command::Pause: break;
    case Command::SeekSector: break;
    case Command::ScanForwards: break;
    case Command::ScanBackwards: break;
    default: break;
    }

    // TODO: proper cycles per command
    return 10000 * 3;
}

uint64 CDDrive::ProcessOperation() {
    switch (m_status.operation) {
    case Operation::Zero: break; // default value at boot-up
    case Operation::ReadTOC:
        // TODO: implement
        break;

    case Operation::Stopped: m_state = TxState::Noop; break;

    case Operation::Seek: [[fallthrough]];
    case Operation::SeekSecurityRingB2: [[fallthrough]];
    case Operation::SeekSecurityRingB6:
        // TODO: implement
        break;

    case Operation::ReadAudioSector: [[fallthrough]];
    case Operation::ReadDataSector:
        // TODO: implement
        break;

    case Operation::Idle:
        m_state = TxState::Noop;

        ++m_currFAD;
        if (m_currFAD > m_targetFAD + 5) {
            m_currFAD = m_targetFAD;
        }

        UpdateStatus();
        break;

    default: m_state = TxState::Noop; break;
    }

    // TODO: proper cycles per operation
    return 10000 * 3;
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
            m_status.subcodeQ = 1;
            m_status.trackNum = 0xAA;
            m_status.indexNum = 1;
            m_status.min = m_status.absMin = m_currFAD / 75 / 60;
            m_status.sec = m_status.absSec = m_currFAD / 75 % 60;
            m_status.frac = m_status.absFrac = m_currFAD % 75;
            m_status.zero = 0x04;
        } else {
            const uint8 trackIndex = session.FindTrackIndex(m_currFAD);
            if (trackIndex != 0xFF) {
                m_status.subcodeQ = session.tracks[trackIndex].controlADR;
                m_status.trackNum = trackIndex + 1;
                m_status.indexNum = session.tracks[trackIndex].FindIndex(m_currFAD);
                m_status.min = m_status.absMin = m_currFAD / 75 / 60;
                m_status.sec = m_status.absSec = m_currFAD / 75 % 60;
                m_status.frac = m_status.absFrac = m_currFAD % 75;
                m_status.zero = 0x04;
            } else {
                // FAD < 150
                // TODO: check if this is correct
                m_status.subcodeQ = session.tracks[0].controlADR;
                m_status.trackNum = 1;
                m_status.indexNum = 1;
                m_status.min = m_status.absMin = m_currFAD / 75 / 60;
                m_status.sec = m_status.absSec = m_currFAD / 75 % 60;
                m_status.frac = m_status.absFrac = m_currFAD % 75;
                m_status.zero = 0x04;
            }
        }
    }

    m_status.checksum = ~std::accumulate(m_status.chksumData.begin(), m_status.chksumData.end(), 0);
}

} // namespace ymir::cdblock
