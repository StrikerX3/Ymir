#pragma once

#include "cd_drive_internal_callbacks.hpp"

#include <ymir/hw/sh1/sh1_internal_callbacks.hpp>

#include <ymir/core/scheduler.hpp>

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::cdblock {

class CDDrive {
public:
    CDDrive(core::Scheduler &scheduler);

    void Reset();

    void MapCallbacks(CBSetCOMSYNCn setCOMSYNCn, CBSetCOMREQn setCOMREQn) {
        m_cbSetCOMSYNCn = setCOMSYNCn;
        m_cbSetCOMREQn = setCOMREQn;
    }

private:
    core::Scheduler &m_scheduler;
    core::EventID m_stateEvent;

    CBSetCOMSYNCn m_cbSetCOMSYNCn;
    CBSetCOMREQn m_cbSetCOMREQn;

    enum class Command : uint8 {
        Noop = 0x0,
        SeekRing = 0x2,
        ReadTOC = 0x3,
        Stop = 0x4,
        ReadSector = 0x6,
        Pause = 0x8,
        SeekSector = 0x9,
        ScanForwards = 0xA,
        ScanBackwards = 0xB,
    };

    // Received from SH1
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
    } m_command;
    uint8 m_commandBit;
    uint8 m_commandIndex;

    enum class Operation : uint8 {
        Zero = 0x00,
        ReadTOC = 0x04,
        Stopped = 0x12,
        Seek = 0x22,
        Unknown = 0x30,
        ReadAudioSector = 0x34,
        ReadDataSector = 0x36,
        Idle = 0x46,
        TrayOpen = 0x80,
        NoDisc = 0x83,
        SeekSecurityRingB2 = 0xB2,
        SeekSecurityRingB6 = 0xB6
    };

    // Sent to SH1
    union CDStatus {
        std::array<uint8, 13> data;
        std::array<uint8, 11> chksumData;
        struct {
            Operation operation;
            uint8 subcodeQ;

            // Subcode Q data
            uint8 trackNum;
            uint8 indexNum;
            uint8 min;
            uint8 sec;
            uint8 frac;
            uint8 zero;
            uint8 absMin;
            uint8 absSec;
            uint8 absFrac;

            // Checksum of all of the above bytes
            uint8 checksum;

            // Next (and final) byte is always zero
        };
    } m_status;
    uint8 m_statusBit;
    uint8 m_statusIndex;

    enum class TxState {
        Reset,    // deassert COMREQ#, deassert COMSYNC#, initialize, switch to Noop
        Noop,     // init transfer counters, switch to TxBegin
        TxBegin,  // assert COMSYNC#, switch to TxByte
        TxByte,   // assert COMREQ#, do byte transfer
        TxInter1, // deassert COMSYNC#, switch to TxByte
        TxInterN, // switch to TxByte
        TxEnd,    // process command, switch to Noop

        // At the end of a byte transfer (not handled in these states):
        // - deassert COMREQ#, deassert COMSYNC#
        // - switch to TxEnd if 13th byte or TxInter otherwise
    };

    TxState m_state;

    uint32 m_currFAD;
    uint32 m_targetFAD;

    bool SerialRead();
    void SerialWrite(bool bit);

    uint64 ProcessState();
    uint64 ProcessCommand();
    uint64 ProcessOperation();

    void UpdateStatus();

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const sh1::CbSerialRx CbSerialRx = util::MakeClassMemberRequiredCallback<&CDDrive::SerialRead>(this);
    const sh1::CbSerialTx CbSerialTx = util::MakeClassMemberRequiredCallback<&CDDrive::SerialWrite>(this);
};

} // namespace ymir::cdblock
