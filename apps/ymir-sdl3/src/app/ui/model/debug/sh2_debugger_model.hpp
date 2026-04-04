#pragma once

#include "sh2_breakpoints_manager.hpp"
#include "sh2_watchpoints_manager.hpp"

#include <ymir/core/types.hpp>

namespace app::ui {

/// @brief SH-2 debugger model.
struct SH2DebuggerModel {
    bool followPC = true;           ///< Auto-follow PC in disassembly view
    bool followPCOnEvents = true;   ///< Auto-follow PC in disassembly view when hitting breakpoints or watchpoints
    bool jumpToPCRequested = false; ///< Whether to jump to PC on the next frame
    bool jumpRequested = false;     ///< Whether to jump to the target address on the next frame
    uint32 jumpAddress = 0;         ///< Pending jump target address

    SH2BreakpointsManager breakpoints{};
    SH2WatchpointsManager watchpoints{};

    void JumpTo(uint32 address) {
        jumpRequested = true;
        jumpAddress = address & ~1u;
    }

    void JumpToPC() {
        jumpToPCRequested = true;
    }
};

} // namespace app::ui
