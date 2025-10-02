#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::state {

struct YGRState {
    struct FIFOState {
        std::array<uint16, 6> data;
        uint32 readPos;
        uint32 writePos;
        uint32 count;
    } fifo;

    struct Registers {
        uint16 TRCTL;
        uint16 CDIRQL;
        uint16 CDIRQU;
        uint16 CDMSKL;
        uint16 CDMSKU;
        uint16 REG0C;
        uint16 REG0E;
        std::array<uint16, 4> CR;
        std::array<uint16, 4> RR;
        uint16 REG18;
        uint16 REG1A;
        uint16 REG1C;
        uint16 HIRQ;
        uint16 HIRQMASK;
    } regs;
};

} // namespace ymir::state
