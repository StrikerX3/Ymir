#pragma once

#include "cd_drive_internal_callbacks.hpp"
#include "cdblock_internal_callbacks.hpp"
#include "ygr_internal_callbacks.hpp"
#include <ymir/hw/sh1/sh1_internal_callbacks.hpp>
#include <ymir/sys/system_internal_callbacks.hpp>

#include <ymir/state/state_cd_drive.hpp>

#include <ymir/debug/cd_drive_tracer_base.hpp>

#include <ymir/core/scheduler.hpp>
#include <ymir/sys/clocks.hpp>

#include <ymir/media/disc.hpp>
#include <ymir/media/filesystem.hpp>

#include <ymir/core/configuration.hpp>
#include <ymir/core/hash.hpp>
#include <ymir/core/types.hpp>

#include <array>

namespace ymir::cdblock {

class CDDrive {
public:
    enum class Command : uint8 {
        Continue = 0x0,
        SeekRing = 0x2,
        ReadTOC = 0x3,
        Stop = 0x4,
        ReadSector = 0x6,
        Pause = 0x8,
        SeekSector = 0x9,
        ScanForwards = 0xA,
        ScanBackwards = 0xB,
    };

    enum class Operation : uint8 {
        Reset = 0x00,
        ReadTOC = 0x04,
        Stopped = 0x12,
        Seek = 0x22,
        DiscChanged = 0x30,
        ReadAudioSector = 0x34,
        ReadDataSector = 0x36,
        Idle = 0x46,
        ScanAudioSector = 0x54,
        TrayOpen = 0x80,
        NoDisc = 0x83,
        SeekSecurityRingB2 = 0xB2,
        SeekSecurityRingB6 = 0xB6
    };

    enum class TxState {
        Reset,    // deassert COMREQ#, deassert COMSYNC#, initialize, switch to Continue
        PreTx,    // init transfer counters, switch to TxBegin
        TxBegin,  // assert COMSYNC#, switch to TxByte
        TxByte,   // assert COMREQ#, do byte transfer
        TxInter1, // deassert COMSYNC#, switch to TxByte
        TxInterN, // switch to TxByte
        TxEnd,    // process command, switch to PreTx

        // At the end of a byte transfer (not handled in these states):
        // - deassert COMREQ#, deassert COMSYNC#
        // - switch to TxEnd if 13th byte or TxInter otherwise
    };

    union CDCommand {
        std::array<uint8, 13> data;
        struct {
            Command command;
            uint8 fadTop;
            uint8 fadMid;
            uint8 fadBtm;
            uint8 zero4;
            uint8 zero5;
            uint8 zero6;
            uint8 zero7;
            uint8 zero8;
            uint8 zero9;
            uint8 readSpeed; // 1=1x, otherwise 2x
            uint8 parity;
            uint8 zero13;
        };
    };

    struct CDStatus {
        Operation operation;
        uint8 subcodeQ;
        uint8 trackNum;
        uint8 indexNum;
        uint8 min;
        uint8 sec;
        uint8 frac;
        uint8 zero;
        uint8 absMin;
        uint8 absSec;
        uint8 absFrac;
    };

    CDDrive(core::Scheduler &scheduler, const media::Disc &disc, const media::fs::Filesystem &fs,
            core::Configuration::CDBlock &config);

    void Reset();

    void MapCallbacks(CBSetCOMSYNCn setCOMSYNCn, CBSetCOMREQn setCOMREQn, CBDataSector dataSector,
                      CBCDDASector cddaSector, CBSectorTransferDone sectorTransferDone) {
        m_cbSetCOMSYNCn = setCOMSYNCn;
        m_cbSetCOMREQn = setCOMREQn;
        m_cbDataSector = dataSector;
        m_cbCDDASector = cddaSector;
        m_cbSectorTransferDone = sectorTransferDone;
    }

    void UpdateClockRatios(const sys::ClockRatios &clockRatios);

    void OnDiscLoaded();
    void OnDiscEjected();
    void OpenTray();
    void CloseTray();
    [[nodiscard]] bool IsTrayOpen() const {
        return m_status.operation == Operation::TrayOpen;
    }

    [[nodiscard]] const media::Disc &GetDisc() const {
        return m_disc;
    }
    [[nodiscard]] XXH128Hash GetDiscHash() const;

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::CDDriveState &state) const;
    [[nodiscard]] bool ValidateState(const state::CDDriveState &state) const;
    void LoadState(const state::CDDriveState &state);

    // -------------------------------------------------------------------------
    // Debugger

    // Attaches the specified tracer to this component.
    // Pass nullptr to disable tracing.
    void UseTracer(debug::ICDDriveTracer *tracer) {
        m_tracer = tracer;
    }

    class Probe {
    public:
        Probe(CDDrive &cddrive);

        const CDStatus &GetStatus() const;
        uint8 GetReadSpeed() const;

        uint32 GetCurrentFrameAddress() const;
        uint32 GetTargetFrameAddress() const;

        std::string GetPathAtFrameAddress(uint32 fad) const;

    private:
        CDDrive &m_cddrive;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    core::Scheduler &m_scheduler;
    core::EventID m_stateEvent;
    bool m_eventsEnabled;

    CBSetCOMSYNCn m_cbSetCOMSYNCn;
    CBSetCOMREQn m_cbSetCOMREQn;
    CBDataSector m_cbDataSector;
    CBCDDASector m_cbCDDASector;
    CBSectorTransferDone m_cbSectorTransferDone;

    const media::Disc &m_disc;
    const media::fs::Filesystem &m_fs;

    // The CD block program only responds to disc change events if they follow the Tray Open state.
    // The auto close tray flag causes the TrayOpen operation processor to automatically close the tray and switch to
    // the appropriate state after outputting the tray open status once.
    bool m_autoCloseTray;

    void OpenTray(bool autoClose);

    std::array<uint8, 2352> m_sectorDataBuffer;

    // Received from SH1
    CDCommand m_command;
    uint8 m_commandPos;

    CDStatus m_status;

    // Sent to SH1
    union StatusData {
        std::array<uint8, 13> data;
        std::array<uint8, 11> chksumData;
        CDStatus cdStatus; // for easy copying
    } m_statusData;
    uint8 m_statusPos;

    TxState m_state;

    uint32 m_currFAD;
    uint32 m_targetFAD;
    Operation m_seekOp;     // Operation to set after done seeking
    uint32 m_seekCountdown; // How many ticks until done seeking
    bool m_scan;            // Are audio tracks being scanned?
    bool m_scanDirection;   // true=forwards, false=backwards
    uint32 m_scanCounter;   // Number of ticks until next scan jump

    uint32 m_currTOCEntry;
    uint32 m_currTOCRepeat;

    uint8 m_readSpeed;

    bool SerialRead();
    void SerialWrite(bool bit);

    uint64 ProcessTxState();
    uint64 ProcessCommand();
    uint64 ProcessOperation();

    uint64 CmdReadTOC();
    uint64 CmdSeekRing();
    uint64 CmdSeekSector();
    uint64 CmdReadSector();
    uint64 CmdPause();
    uint64 CmdStop();
    uint64 CmdScan(bool fwd);
    uint64 CmdUnknown();

    uint64 OpReadTOC();
    uint64 OpStopped();
    uint64 OpSeek();
    uint64 OpReadSector();
    uint64 OpIdle();
    uint64 OpTrayOpen();
    uint64 OpUnknown();

    void UpdateReadSpeedFactor();

    void SetupSeek(bool read);
    uint64 BeginSeek(bool read);
    uint64 ReadTOC();

    void OutputDriveStatus();
    void OutputRingStatus();
    void CalcStatusDataChecksum();

    // -------------------------------------------------------------------------
    // Debugger

    Probe m_probe{*this};
    debug::ICDDriveTracer *m_tracer = nullptr;

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const sh1::CbSerialRx CbSerialRx = util::MakeClassMemberRequiredCallback<&CDDrive::SerialRead>(this);
    const sh1::CbSerialTx CbSerialTx = util::MakeClassMemberRequiredCallback<&CDDrive::SerialWrite>(this);

    const sys::CBClockSpeedChange CbClockSpeedChange =
        util::MakeClassMemberRequiredCallback<&CDDrive::UpdateClockRatios>(this);
};

} // namespace ymir::cdblock
