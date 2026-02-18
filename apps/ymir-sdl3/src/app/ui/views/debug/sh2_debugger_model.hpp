#pragma once

#include <ymir/core/types.hpp>

namespace app::ui {

/// Model/shared state for SH-2 debugger UI components (model-view pattern)
struct SH2DebuggerModel {
    bool followPC = true;         // Auto-follow PC in disassembly view
    bool followPCOnEvents = true; // Auto-follow PC in disassembly view when hitting breakpoints or watchpoints
    bool jumpRequested = false;   // Whether to jump to the cursor
    uint32 jumpAddress = 0;       // Pending jump target
};

} // namespace app::ui
