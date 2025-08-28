#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::state {

struct SMPCState {
    alignas(16) std::array<uint8, 7> IREG;
    alignas(16) std::array<uint8, 32> OREG;
    uint8 COMREG;
    uint8 SR;
    bool SF;
    uint8 PDR1;
    uint8 PDR2;
    uint8 DDR1;
    uint8 DDR2;
    uint8 IOSEL;
    uint8 EXLE;

    struct INTBACK {
        bool getPeripheralData;
        bool optimize;
        uint8 port1mode;
        uint8 port2mode;

        std::vector<uint8> report;
        size_t reportOffset;
        bool inProgress;
        bool enqueued;
    } intback;

    uint8 busValue;
    bool resetDisable;

    sint64 rtcTimestamp;
    uint64 rtcSysClockCount;
};

} // namespace ymir::state
