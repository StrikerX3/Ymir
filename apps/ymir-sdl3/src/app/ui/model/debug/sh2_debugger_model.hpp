#pragma once

#include <ymir/core/types.hpp>

namespace app::ui {

/// Model/shared state for SH-2 debugger UI components (model-view pattern)
struct SH2DebuggerModel {
    bool followPC = true;           // Auto-follow PC in disassembly view
    bool followPCOnEvents = true;   // Auto-follow PC in disassembly view when hitting breakpoints or watchpoints
    bool jumpToPCRequested = false; // Whether to jump to PC on the next frame
    bool jumpRequested = false;     // Whether to jump to the target address on the next frame
    uint32 jumpAddress = 0;         // Pending jump target address

    void JumpTo(uint32 address) {
        jumpRequested = true;
        jumpAddress = address & ~1u;
    }

    void JumpToPC() {
        jumpToPCRequested = true;
    }
};

} // namespace app::ui
