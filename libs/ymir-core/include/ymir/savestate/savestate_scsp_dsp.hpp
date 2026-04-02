#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::savestate {

struct SCSPDSPSaveState {
    alignas(16) std::array<uint64, 128> MPRO;
    alignas(16) std::array<sint32, 128> TEMP;
    alignas(16) std::array<sint32, 32> MEMS;
    alignas(16) std::array<uint16, 64> COEF;
    alignas(16) std::array<uint16, 32> MADRS;
    alignas(16) std::array<sint32, 16 * 2> MIXS;
    alignas(16) std::array<sint16, 16> EFREG;
    alignas(16) std::array<sint16, 2> EXTS;

    uint8 MIXSGen;
    uint16 MIXSNull;

    uint8 RBP;
    uint8 RBL;

    uint8 PC;
    sint32 INPUTS;

    uint32 SFT_REG;
    uint16 FRC_REG;
    uint32 Y_REG;
    uint16 ADRS_REG;

    uint16 MDEC_CT;

    bool readPending;
    bool readNOFL;
    uint32 readValue;

    bool writePending;
    uint16 writeValue;

    uint32 readWriteAddr;
};

} // namespace ymir::savestate
