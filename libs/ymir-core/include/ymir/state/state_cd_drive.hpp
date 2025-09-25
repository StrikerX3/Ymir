#pragma once

#include <ymir/core/hash.hpp>
#include <ymir/core/types.hpp>

#include <array>

namespace ymir::state {

struct CDDriveState {
    XXH128Hash discHash;

    bool autoCloseTray;

    std::array<uint8, 2352> sectorDataBuffer;

    std::array<uint8, 13> commandData;
    uint8 commandPos;

    std::array<uint8, 13> statusData;
    uint8 statusPos;

    struct CDStatusState {
        uint8 operation;
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
    } status;

    enum class TxState : uint8 { Reset, PreTx, TxBegin, TxByte, TxInter1, TxInterN, TxEnd };
    TxState state;

    uint32 currFAD;
    uint32 targetFAD;
    uint8 seekOp;
    uint32 seekCountdown;

    uint32 currTOCEntry;
    uint32 currTOCRepeat;

    uint8 readSpeed;
};

} // namespace ymir::state
