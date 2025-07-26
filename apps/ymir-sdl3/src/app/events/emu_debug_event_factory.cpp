#include "emu_debug_event_factory.hpp"

#include "emu_event_factory.hpp"

#include <app/shared_context.hpp>

namespace app::events::emu::debug {

EmuEvent ExecuteSH2Division(bool master, bool div64) {
    if (div64) {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = master ? ctx.saturn.masterSH2 : ctx.saturn.slaveSH2;
            sh2.GetProbe().ExecuteDiv64();
        });
    } else {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = master ? ctx.saturn.masterSH2 : ctx.saturn.slaveSH2;
            sh2.GetProbe().ExecuteDiv32();
        });
    }
}

EmuEvent WriteMainMemory(uint32 address, uint8 value, bool enableSideEffects) {
    if (enableSideEffects) {
        return RunFunction([=](SharedContext &ctx) { ctx.saturn.mainBus.Write<uint8>(address, value); });
    } else {
        return RunFunction([=](SharedContext &ctx) { ctx.saturn.mainBus.Poke<uint8>(address, value); });
    }
}

EmuEvent WriteSH2Memory(uint32 address, uint8 value, bool enableSideEffects, bool master, bool bypassCache) {
    if (enableSideEffects) {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = master ? ctx.saturn.masterSH2 : ctx.saturn.slaveSH2;
            sh2.GetProbe().MemWriteByte(address, value, bypassCache);
        });
    } else {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = master ? ctx.saturn.masterSH2 : ctx.saturn.slaveSH2;
            sh2.GetProbe().MemPokeByte(address, value, bypassCache);
        });
    }
}

EmuEvent AddSH2Breakpoint(bool master, uint32 address) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = master ? ctx.saturn.masterSH2 : ctx.saturn.slaveSH2;
        std::unique_lock lock{ctx.locks.breakpoints};
        /* TODO: const bool added = */ sh2.AddBreakpoint(address);
    });
}

EmuEvent RemoveSH2Breakpoint(bool master, uint32 address) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = master ? ctx.saturn.masterSH2 : ctx.saturn.slaveSH2;
        std::unique_lock lock{ctx.locks.breakpoints};
        /* TODO: const bool removed = */ sh2.RemoveBreakpoint(address);
    });
}

EmuEvent ReplaceSH2Breakpoints(bool master, const std::set<uint32> &addresses) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = master ? ctx.saturn.masterSH2 : ctx.saturn.slaveSH2;
        std::unique_lock lock{ctx.locks.breakpoints};
        sh2.ReplaceBreakpoints(addresses);
    });
}

EmuEvent ClearSH2Breakpoints(bool master) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = master ? ctx.saturn.masterSH2 : ctx.saturn.slaveSH2;
        std::unique_lock lock{ctx.locks.breakpoints};
        sh2.ClearBreakpoints();
    });
}

EmuEvent SetLayerEnabled(ymir::vdp::Layer layer, bool enabled) {
    return RunFunction([=](SharedContext &ctx) {
        auto &vdp = ctx.saturn.VDP;
        vdp.SetLayerEnabled(layer, enabled);
    });
}

} // namespace app::events::emu::debug
