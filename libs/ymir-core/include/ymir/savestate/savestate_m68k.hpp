#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::savestate {

struct M68KSaveState {
    alignas(16) std::array<uint32, 8 + 8> DA;
    uint32 SP_swap;
    uint32 PC;
    uint16 SR;

    std::array<uint16, 2> prefetchQueue;
    uint8 extIntrLevel;
};

} // namespace ymir::savestate
