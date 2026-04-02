#pragma once

#include <ymir/core/types.hpp>

namespace ymir::savestate {

struct SCSPTimerSaveState {
    uint8 incrementInterval;
    uint8 reload;

    bool doReload;
    uint8 counter;
};

} // namespace ymir::savestate
