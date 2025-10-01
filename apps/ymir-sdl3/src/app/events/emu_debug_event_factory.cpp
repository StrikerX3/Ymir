#include "emu_debug_event_factory.hpp"

#include "emu_event_factory.hpp"

#include <app/shared_context.hpp>

#include <ymir/hw/sh1/sh1.hpp>
#include <ymir/hw/sh2/sh2.hpp>
#include <ymir/hw/vdp/vdp.hpp>
#include <ymir/sys/bus.hpp>

namespace app::events::emu::debug {

EmuEvent ExecuteSH2Division(bool master, bool div64) {
    if (div64) {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = ctx.saturn.GetSH2(master);
            sh2.GetProbe().ExecuteDiv64();
        });
    } else {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = ctx.saturn.GetSH2(master);
            sh2.GetProbe().ExecuteDiv32();
        });
    }
}

EmuEvent WriteMainMemory(uint32 address, uint8 value, bool enableSideEffects) {
    if (enableSideEffects) {
        return RunFunction([=](SharedContext &ctx) { ctx.saturn.GetMainBus().Write<uint8>(address, value); });
    } else {
        return RunFunction([=](SharedContext &ctx) { ctx.saturn.GetMainBus().Poke<uint8>(address, value); });
    }
}

EmuEvent WriteSH1Memory(uint32 address, uint8 value, bool enableSideEffects) {
    if (enableSideEffects) {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh1 = ctx.saturn.GetSH1();
            sh1.GetProbe().MemWriteByte(address, value);
        });
    } else {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh1 = ctx.saturn.GetSH1();
            sh1.GetProbe().MemPokeByte(address, value);
        });
    }
}

EmuEvent WriteSH2Memory(uint32 address, uint8 value, bool enableSideEffects, bool master, bool bypassCache) {
    if (enableSideEffects) {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = ctx.saturn.GetSH2(master);
            sh2.GetProbe().MemWriteByte(address, value, bypassCache);
        });
    } else {
        return RunFunction([=](SharedContext &ctx) {
            auto &sh2 = ctx.saturn.GetSH2(master);
            sh2.GetProbe().MemPokeByte(address, value, bypassCache);
        });
    }
}

EmuEvent AddSH2Breakpoint(bool master, uint32 address) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = ctx.saturn.GetSH2(master);
        std::unique_lock lock{ctx.locks.breakpoints};
        /* TODO: const bool added = */ sh2.AddBreakpoint(address);
    });
}

EmuEvent RemoveSH2Breakpoint(bool master, uint32 address) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = ctx.saturn.GetSH2(master);
        std::unique_lock lock{ctx.locks.breakpoints};
        /* TODO: const bool removed = */ sh2.RemoveBreakpoint(address);
    });
}

EmuEvent ReplaceSH2Breakpoints(bool master, const std::set<uint32> &addresses) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = ctx.saturn.GetSH2(master);
        std::unique_lock lock{ctx.locks.breakpoints};
        sh2.ReplaceBreakpoints(addresses);
    });
}

EmuEvent ClearSH2Breakpoints(bool master) {
    return RunFunction([=](SharedContext &ctx) {
        auto &sh2 = ctx.saturn.GetSH2(master);
        std::unique_lock lock{ctx.locks.breakpoints};
        sh2.ClearBreakpoints();
    });
}

EmuEvent SetLayerEnabled(ymir::vdp::Layer layer, bool enabled) {
    return RunFunction([=](SharedContext &ctx) {
        auto &vdp = ctx.saturn.GetVDP();
        vdp.SetLayerEnabled(layer, enabled);
    });
}

EmuEvent VDP2SetCRAMColor555(uint32 index, ymir::vdp::Color555 color) {
    return RunFunction([=](SharedContext &ctx) {
        auto &vdp = ctx.saturn.GetVDP();
        vdp.GetProbe().VDP2SetCRAMColor555(index, color);
    });
}

EmuEvent VDP2SetCRAMColor888(uint32 index, ymir::vdp::Color888 color) {
    return RunFunction([=](SharedContext &ctx) {
        auto &vdp = ctx.saturn.GetVDP();
        vdp.GetProbe().VDP2SetCRAMColor888(index, color);
    });
}

} // namespace app::events::emu::debug
