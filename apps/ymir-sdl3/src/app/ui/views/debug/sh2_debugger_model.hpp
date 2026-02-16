#pragma once

#include <ymir/core/types.hpp>

namespace app::ui {

/// Model/shared state for SH-2 debugger UI components (model-view pattern)
struct SH2DebuggerModel {
    bool followPC = true;        // Auto-follow PC in disassembly view
    uint32 jumpAddress = 0;      // Pending jump target
    bool jumpRequested = false;  // Scroll animation request
    bool recenterWindow = false; // One-shot: recenter view window on jump target
};

} // namespace app::ui
