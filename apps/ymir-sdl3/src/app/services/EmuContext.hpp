//
// Thin convergence bundle for emulator-thread services.
// Transitional: includes legacy SharedContext reference until consumers are migrated.
//

#pragma once

namespace app {
struct SharedContext;

namespace savestates {
    struct ISaveStateService;
}

struct EmuContext {
    savestates::ISaveStateService &saveStates;
    SharedContext *legacyContext{nullptr};
};

} // namespace app

