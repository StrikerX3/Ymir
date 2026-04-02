#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::savestate {

struct SCSPSlotSaveState {
    uint32 SA;
    uint16 LSA;
    uint16 LEA;
    bool PCM8B;
    bool KYONB;
    uint16 SBCTL;

    enum class LoopControl { Off, Normal, Reverse, Alternate };
    LoopControl LPCTL;

    enum class SoundSource { SoundRAM, Noise, Silence, Unknown };
    SoundSource SSCTL;

    uint8 AR;
    uint8 D1R;
    uint8 D2R;
    uint8 RR;
    uint8 DL;

    uint8 KRS;
    bool EGHOLD;
    bool LPSLNK;
    bool EGBYPASS;

    uint8 MDL;
    uint8 MDXSL;
    uint8 MDYSL;
    bool STWINH;

    uint8 TL;
    bool SDIR;

    uint8 OCT;
    uint16 FNS;
    bool MM;

    enum class Waveform : uint8 { Saw, Square, Triangle, Noise };
    bool LFORE;
    uint8 LFOF;
    uint8 ALFOS;
    uint8 PLFOS;
    Waveform ALFOWS;
    Waveform PLFOWS;

    uint8 IMXL;
    uint8 ISEL;
    uint8 DISDL;
    uint8 DIPAN;

    uint8 EFSDL;
    uint8 EFPAN;

    uint16 extra0C;
    uint16 extra14;

    bool active;

    enum class EGState : uint8 { Attack, Decay1, Decay2, Release };
    EGState egState;

    uint16 egLevel;
    uint16 currEGLevel;

    bool egAttackBug;

    uint32 currSample;
    uint32 currPhase;
    uint32 nextPhase;
    sint32 modulation;
    bool reverse;
    bool crossedLoopStart;

    uint32 lfoCycles;
    uint8 lfoStep;

    uint8 alfoOutput;

    sint16 sample1;
    sint16 sample2;
    sint16 output;

    sint32 finalLevel;
};

} // namespace ymir::savestate
