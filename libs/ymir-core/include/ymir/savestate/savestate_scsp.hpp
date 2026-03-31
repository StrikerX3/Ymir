#pragma once

#include "savestate_m68k.hpp"
#include "savestate_scsp_dsp.hpp"
#include "savestate_scsp_slot.hpp"
#include "savestate_scsp_timer.hpp"

#include <ymir/hw/m68k/m68k_defs.hpp>
#include <ymir/hw/scsp/scsp_midi_defs.hpp>

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::savestate {

struct SCSPSaveState {
    alignas(16) std::array<uint8, m68k::kM68KWRAMSize> WRAM;

    alignas(16) std::array<uint8, 2352 * 15> cddaBuffer;
    uint32 cddaReadPos;
    uint32 cddaWritePos;
    bool cddaReady;

    M68KSaveState m68k;
    uint64 m68kSpilloverCycles;
    bool m68kEnabled;

    alignas(16) std::array<SCSPSlotSaveState, 32> slots;

    bool KYONEX;
    bool KYONEXExec;

    uint32 MVOL;
    bool DAC18B;
    bool MEM4MB;
    uint8 MSLC;

    std::array<SCSPTimerSaveState, 3> timers;

    uint16 MCIEB;
    uint16 MCIPD;
    uint16 SCIEB;
    uint16 SCIPD;
    std::array<uint8, 3> SCILV;
    // HACK to preserve old savestate behavior which was missing SCILV.
    // Causes SCSP to reuse current SCILV settings instead of loading from save state.
    bool reuseSCILV;

    bool DEXE;
    bool DDIR;
    bool DGATE;
    uint32 DMEA;
    uint16 DRGA;
    uint16 DTLG;

    alignas(16) std::array<uint16, 64> SOUS;
    uint32 soundStackIndex;

    SCSPDSPSaveState dsp;

    uint64 m68kCycles;
    uint64 sampleCounter;

    uint32 lfsr;

    uint32 currSlot;

    std::array<sint32, 2> out;

    std::array<uint8, ymir::scsp::kMidiBufferSize> midiInputBuffer;
    uint32 midiInputReadPos;
    uint32 midiInputWritePos;
    bool midiInputOverflow;

    std::array<uint8, ymir::scsp::kMidiBufferSize> midiOutputBuffer;
    uint32 midiOutputSize;
    sint32 expectedOutputPacketSize;
};

} // namespace ymir::savestate
