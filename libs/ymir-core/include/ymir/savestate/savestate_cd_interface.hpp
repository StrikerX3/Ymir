#pragma once

#include <ymir/core/types.hpp>

namespace ymir::savestate {

struct CDInterfaceSaveState {
    uint32 seekTarget;
    uint32 seekFAD;
    bool seekDone;
};

} // namespace ymir::savestate
