#pragma once

#include <ymir/core/types.hpp>

namespace ymir::cdblock {

// HIRQ flags
using HIRQFlags = uint16;
inline constexpr HIRQFlags kHIRQ_CMOK = 0x0001; // Ready to receive command
inline constexpr HIRQFlags kHIRQ_DRDY = 0x0002; // Ready to transfer data
inline constexpr HIRQFlags kHIRQ_CSCT = 0x0004; // Sector read finished
inline constexpr HIRQFlags kHIRQ_BFUL = 0x0008; // CD buffer full
inline constexpr HIRQFlags kHIRQ_PEND = 0x0010; // CD playback stopped
inline constexpr HIRQFlags kHIRQ_DCHG = 0x0020; // Disc changed
inline constexpr HIRQFlags kHIRQ_ESEL = 0x0040; // Selector processing finished
inline constexpr HIRQFlags kHIRQ_EHST = 0x0080; // Host I/O processing finished
inline constexpr HIRQFlags kHIRQ_ECPY = 0x0100; // Copy or move finished
inline constexpr HIRQFlags kHIRQ_EFLS = 0x0200; // Filesystem processing finished
inline constexpr HIRQFlags kHIRQ_SCDQ = 0x0400; // Subcode Q updated
inline constexpr HIRQFlags kHIRQ_MPED = 0x0800; // MPEG processing finished
inline constexpr HIRQFlags kHIRQ_MPCM = 0x1000; // Long-running MPEG operation finished
inline constexpr HIRQFlags kHIRQ_MPST = 0x2000; // MPEG interrupt raised
inline constexpr HIRQFlags kHIRQ_mask = 0x3FFF; // all valid HIRQ bits

// Status codes
inline constexpr uint8 kStatusCodeBusy = 0x00;
inline constexpr uint8 kStatusCodePause = 0x01;
inline constexpr uint8 kStatusCodeStandby = 0x02;
inline constexpr uint8 kStatusCodePlay = 0x03;
inline constexpr uint8 kStatusCodeSeek = 0x04;
inline constexpr uint8 kStatusCodeScan = 0x05;
inline constexpr uint8 kStatusCodeOpen = 0x06;
inline constexpr uint8 kStatusCodeNoDisc = 0x07;
inline constexpr uint8 kStatusCodeRetry = 0x08;
inline constexpr uint8 kStatusCodeError = 0x09;
inline constexpr uint8 kStatusCodeFatal = 0x0A;

inline constexpr uint8 kStatusFlagPeriodic = 0x20;
inline constexpr uint8 kStatusFlagXferRequest = 0x40;
inline constexpr uint8 kStatusFlagWait = 0x80;

inline constexpr uint8 kStatusReject = 0xFF;

// ---------------------------------------------------------------------------------------------------------------------
// Drive timings

inline constexpr uint32 kCyclesPerSecond = 20000000;

inline constexpr uint16 kMinStandbyTime = 60;
inline constexpr uint16 kMaxStandbyTime = 900;

// Periodic response intervals:
// - Not playing:         16.667ms =  60 Hz = 1000000/3 (333333.333) cycles @ 20 MHz = once per video frame
// - Playing at 1x speed: 13.333ms =  75 Hz =  800000/3 (266666.667) cycles @ 20 MHz = once per CD frame
// - Playing at 2x speed:  6.667ms = 150 Hz =  400000/3 (133333.333) cycles @ 20 MHz = once per CD frame

// Constants for periodic report cycles are tripled to avoid rounding.
// Cycle counting must be tripled to account for that.
// 2x cycles can be easily derived from 1x cycles.

inline constexpr uint32 kDriveCyclesNotPlaying = 1000000;
inline constexpr uint32 kDriveCyclesPlaying1x = 800000;

// Serial data transfer timings adapted from Yabause.
// Times are in cycles and tripled for the reason above.

inline constexpr uint32 kTxCyclesPowerOn = 451448 * 20 * 3; // Power-on stable -> first COMSYNC# falling edge
inline constexpr uint32 kTxCyclesFirstTx = 416509 * 20 * 3; // First COMSYNC# falling edge -> first transmission
inline constexpr uint32 kTxCyclesBeginTx = 187 * 20 * 3;    // COMSYNC# falling -> rising edge (start of transfer)
inline constexpr uint32 kTxCyclesPerByte = 150 * 20 * 3;    // COMREQ# falling -> rising edge (one byte transfer)
inline constexpr uint32 kTxCyclesInterTx = 26 * 20 * 3;     // COMREQ# rising -> falling edge (inter-byte)

// ---------------------------------------------------------------------------------------------------------------------
// Buffers and filters

inline constexpr uint32 kNumBuffers = 200;   // total number of buffers
inline constexpr uint32 kNumFilters = 24;    // total number of filters
inline constexpr uint32 kNumPartitions = 24; // total number of buffer partitions

struct MSF {
    uint8 m, s, f;
};

inline MSF FADToMSF(uint32 fad) {
    MSF msf{};
    msf.m = fad / 75 / 60;
    msf.s = fad / 75 % 60;
    msf.f = fad % 75;
    return msf;
}

} // namespace ymir::cdblock
