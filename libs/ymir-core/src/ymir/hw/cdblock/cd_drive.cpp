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
    // State sequence:                                   repeat this 11 times
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
        return 18000000;

    case TxState::Noop: m_state = TxState::TxBegin; return 10000;

    case TxState::TxBegin:
        m_cbSetCOMSYNCn(false);
        m_state = TxState::TxByte;
        return 10000;

    case TxState::TxByte: m_cbSetCOMREQn(false); return 10000;

    case TxState::TxInter1:
        m_cbSetCOMREQn(true);
        m_state = TxState::TxByte;
        return 10000;

    case TxState::TxInterN: m_state = TxState::TxByte; return 10000;

    case TxState::TxEnd:
        // ProcessCommand also handles the state change
        return ProcessCommand();
    }

    return 10000;
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
    return 10000;
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
    return 10000;
}

void CDDrive::UpdateStatus() {
    // TODO: update status fields
    // TODO: needs TOC/disc structure... let's just fake it for now
    m_status.trackNum = 1;
    m_status.indexNum = 1;
    m_status.min = m_status.absMin = m_currFAD / 75 / 60;
    m_status.sec = m_status.absSec = m_currFAD / 75 % 60;
    m_status.frac = m_status.absFrac = m_currFAD % 75;
    m_status.zero = 0x04;

    m_status.checksum = ~std::accumulate(m_status.chksumData.begin(), m_status.chksumData.end(), 0);
}

} // namespace ymir::cdblock
