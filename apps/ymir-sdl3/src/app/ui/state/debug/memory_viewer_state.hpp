#pragma once

#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <app/events/emu_debug_event_factory.hpp>

#include <ymir/hw/sh1/sh1.hpp>
#include <ymir/hw/sh2/sh2.hpp>

#include <imgui_memory_editor.h>

namespace app::ui::mem_view {

struct Region;

struct MemoryViewerState {
    MemoryViewerState(SharedContext &sharedCtx)
        : sharedCtx(sharedCtx) {}

    SharedContext &sharedCtx;
    MemoryEditor memoryEditor;
    bool enableSideEffects = false;
    bool bypassSH2Cache = false;
    const Region *selectedRegion = nullptr;
};

// -------------------------------------------------------------------------
// Region handlers

// --- Main address space ------
// [Main:0000000..7FFFFFF] Main address space
// [Main:0000000..00FFFFF] Boot ROM / IPL
// [Main:0100000..010007F] SMPC registers
// [Main:0180000..018FFFF] Internal backup RAM
// [Main:0200000..02FFFFF] Low Work RAM
// [Main:1000000..17FFFFF] MINIT area
// [Main:1800000..1FFFFFF] SINIT area
// [Main:2000000..5FFFFFF] SCU A-Bus
// [Main:2000000..3FFFFFF] SCU A-Bus CS0 (cartridge)
// [Main:4000000..4FFFFFF] SCU A-Bus CS1 (cartridge)
// [Main:5800000..58FFFFF] SCU A-Bus CS2
// [Main:5890000..589003F] CD Block registers
// [Main:5A00000..5FBFFFF] SCU B-Bus
// [Main:5A00000..5A7FFFF] 68000 Work RAM
// [Main:5B00000..5B00FFF] SCSP registers
// [Main:5C00000..5C7FFFF] VDP1 VRAM
// [Main:5C80000..5CBFFFF] VDP1 framebuffer
// [Main:5D00000..5D0001F] VDP1 registers
// [Main:5E00000..5E7FFFF] VDP2 VRAM
// [Main:5F00000..5F00FFF] VDP2 CRAM
// [Main:5F80000..5F801FF] VDP2 registers
// [Main:5FE0000..5FE00FF] SCU registers
// [Main:6000000..60FFFFF] High Work RAM
// --- Master SH-2 -------------
// NOTE: the associate purge area is intentionally omitted; the cache viewer is going to be more useful for that
// [MSH2:00000000..07FFFFFF] MSH2 cached address space
// [MSH2:20000000..27FFFFFF] MSH2 uncached address space
// [MSH2:60000000..600003FF] MSH2 cache address array   (based on currently selected way)
// [MSH2:C0000000..C0000FFF] MSH2 cache data array
// [MSH2:FFFFFE00..FFFFFFFF] MSH2 on-chip registers
// --- Slave SH-2 --------------
// NOTE: the associate purge area is intentionally omitted; the cache viewer is going to be more useful for that
// [SSH2:00000000..07FFFFFF] SSH2 cached address space
// [SSH2:20000000..27FFFFFF] SSH2 uncached address space
// [SSH2:60000000..600003FF] SSH2 cache address array   (based on currently selected way)
// [SSH2:C0000000..C0000FFF] SSH2 cache data array
// [SSH2:FFFFFE00..FFFFFFFF] SSH2 on-chip registers
// --- CD Block SH-1 --------------
// [SH1:00000000..07FFFFFF] SH1 address space
// [SH1:00000000..0000FFFF] SH1 on-chip ROM
// [SH1:05FFFE00..05FFFFFF] SH1 on-chip supporting modules
// [SH1:09000000..0907FFFF] CD block DRAM
// [SH1:0A000000..0A00001F] YGR CD drive registers
// [SH1:0A100000..0A10003F] YGR MPEG card registers (low)
// [SH1:0A180000..0A18000F] YGR MPEG card registers (high)
// [SH1:0E000000..0E07FFFF] MPEG card ROM
// [SH1:0F000000..0F000FFF] SH1 on-chip RAM

// TODO: cartridge contents
// --- Cartridge ---------------
// NOTE: populate based on the currently inserted cartridge
// [Cart:000000..07FFFF] Backup RAM cart data    (4 Mbit)
// [Cart:000000..0FFFFF] Backup RAM cart data    (8 Mbit)
// [Cart:000000..1FFFFF] Backup RAM cart data   (16 Mbit)
// [Cart:000000..3FFFFF] Backup RAM cart data   (32 Mbit)
// [Cart:000000..0FFFFF] DRAM cart data          (8 Mbit)
// [Cart:000000..3FFFFF] DRAM cart data         (32 Mbit)
// [Cart:000000..5FFFFF] DRAM cart data         (48 Mbit)

// TODO: raw CD-ROM contents
// --- CD-ROM ------------------
// [Disc:00000000..xxxxxxxx] CD-ROM contents

struct Region {
    using ReadFn = ImU8 (*)(const ImU8 *mem, size_t off, void *user_data);
    using WriteFn = void (*)(ImU8 *mem, size_t off, ImU8 d, void *user_data);
    using BgColorFn = ImU32 (*)(const ImU8 *mem, size_t off, void *user_data);
    using ParamsFn = void (*)(MemoryViewerState *state);
    using HoverFn = void (*)(uint32 address, MemoryViewerState *state);

    const char *name;
    const char *addressBlockName;
    uint32 baseAddress;
    uint32 size;
    ReadFn readFn;
    WriteFn writeFn;
    BgColorFn bgColorFn;
    ParamsFn paramsFn;
    HoverFn hoverFn;

    std::string ToString() const {
        return fmt::format("[{}:{:08X}..{:08X}] {}", addressBlockName, baseAddress, baseAddress + size - 1, name);
    }
};

struct RegionGroup {
    const char *name;
    std::span<const Region> regions;
};

namespace regions {

    inline ImU8 MainBusRead(const ImU8 *mem, size_t off, void *user_data) {
        auto &state = *static_cast<const MemoryViewerState *>(user_data);
        off += state.selectedRegion->baseAddress;
        return state.sharedCtx.saturn.GetMainBus().Peek<uint8>(off);
    }

    inline void MainBusWrite(ImU8 *mem, size_t off, ImU8 d, void *user_data) {
        auto &state = *static_cast<MemoryViewerState *>(user_data);
        off += state.selectedRegion->baseAddress;
        state.sharedCtx.EnqueueEvent(events::emu::debug::WriteMainMemory(off, d, state.enableSideEffects));
    }

    inline ImU32 MainBusBgColor(const ImU8 * /*mem*/, size_t /*off*/, void * /*user_data*/) {
        // auto &state = *static_cast<MemoryViewerState *>(user_data);
        // off += state.selectedRegion->baseAddress;
        // TODO: use this to colorize fields/regions
        return 0;
    }

    template <bool master>
    inline ImU8 SH2BusRead(const ImU8 *mem, size_t off, void *user_data) {
        auto &state = *static_cast<const MemoryViewerState *>(user_data);
        off += state.selectedRegion->baseAddress;
        auto &sh2 = state.sharedCtx.saturn.GetSH2(master);
        return sh2.GetProbe().MemPeekByte(off, state.bypassSH2Cache);
    }

    template <bool master>
    inline void SH2BusWrite(ImU8 *mem, size_t off, ImU8 d, void *user_data) {
        auto &state = *static_cast<MemoryViewerState *>(user_data);
        off += state.selectedRegion->baseAddress;
        state.sharedCtx.EnqueueEvent(
            events::emu::debug::WriteSH2Memory(off, d, state.enableSideEffects, master, state.bypassSH2Cache));
    }

    template <bool master>
    inline ImU32 SH2BusBgColor(const ImU8 *mem, size_t off, void *user_data) {
        // auto &state = *static_cast<MemoryViewerState *>(user_data);
        // off += state.selectedRegion->baseAddress;
        // auto &sh2 = state.sharedCtx.saturn.GetSH2(master);
        // TODO: use this to colorize fields/regions
        return 0;
    }

    static constexpr float kCacheWayHues[] = {38.0f, 96.0f, 193.0f, 282.0f};

    inline ImU32 SH2CacheAddressBgColor(const ImU8 *mem, size_t off, void *user_data) {
        const uint32 index = (off >> 2) & 3;
        return ImColor::HSV(kCacheWayHues[index] / 360.0f, 0.96f, 0.18f);
    }

    inline ImU32 SH2CacheDataBgColor(const ImU8 *mem, size_t off, void *user_data) {
        const uint32 index = (off >> 10) & 3;
        return ImColor::HSV(kCacheWayHues[index] / 360.0f, 0.96f, 0.18f);
    }

    inline void SH2CacheAddressHover(uint32 address, MemoryViewerState * /*state*/) {
        if (ImGui::BeginTooltip()) {
            ImGui::Text("Entry %u, way %u", (address >> 4) & 63, (address >> 2) & 3);
            ImGui::EndTooltip();
        }
    }

    inline void SH2CacheDataHover(uint32 address, MemoryViewerState * /*state*/) {
        if (ImGui::BeginTooltip()) {
            ImGui::Text("Way %u, line %u", (address >> 10) & 3, (address >> 4) & 63);
            ImGui::EndTooltip();
        }
    }

    inline void SH2CachedAreaParams(MemoryViewerState *state) {
        const auto &settings = state->sharedCtx.serviceLocator.GetRequired<Settings>();
        const bool emulateSH2Cache = settings.system.emulateSH2Cache;
        ImGui::SameLine();
        if (!emulateSH2Cache) {
            ImGui::BeginDisabled();
        }
        ImGui::Checkbox("Bypass cache", &state->bypassSH2Cache);
        if (!emulateSH2Cache) {
            ImGui::EndDisabled();
        }
    }

    inline ImU8 SH1BusRead(const ImU8 *mem, size_t off, void *user_data) {
        auto &state = *static_cast<const MemoryViewerState *>(user_data);
        off += state.selectedRegion->baseAddress;
        auto &sh1 = state.sharedCtx.saturn.GetSH1();
        return sh1.GetProbe().MemPeekByte(off);
    }

    inline void SH1BusWrite(ImU8 *mem, size_t off, ImU8 d, void *user_data) {
        auto &state = *static_cast<MemoryViewerState *>(user_data);
        off += state.selectedRegion->baseAddress;
        state.sharedCtx.EnqueueEvent(events::emu::debug::WriteSH1Memory(off, d, state.enableSideEffects));
    }

    // clang-format off
    inline constexpr Region kMainRegions[] = {
        { .name = "Main address space",          .addressBlockName = "Main", .baseAddress = 0x0000000, .size = 0x8000000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "Boot ROM / IPL",              .addressBlockName = "Main", .baseAddress = 0x0000000, .size =  0x100000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SMPC registers",              .addressBlockName = "Main", .baseAddress = 0x0100000, .size =      0x80, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "Internal backup RAM",         .addressBlockName = "Main", .baseAddress = 0x0180000, .size =   0x10000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "Low Work RAM",                .addressBlockName = "Main", .baseAddress = 0x0200000, .size =  0x100000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "MINIT area",                  .addressBlockName = "Main", .baseAddress = 0x1000000, .size =  0x800000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SINIT area",                  .addressBlockName = "Main", .baseAddress = 0x1000000, .size =  0x800000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SCU A-Bus",                   .addressBlockName = "Main", .baseAddress = 0x2000000, .size = 0x4000000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SCU A-Bus CS0 (cartridge)",   .addressBlockName = "Main", .baseAddress = 0x2000000, .size = 0x2000000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SCU A-Bus CS1 (cartridge)",   .addressBlockName = "Main", .baseAddress = 0x4000000, .size = 0x1000000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SCU A-Bus CS2",               .addressBlockName = "Main", .baseAddress = 0x5800000, .size =  0x100000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "CD Block registers",          .addressBlockName = "Main", .baseAddress = 0x5890000, .size =      0x40, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SCU B-Bus",                   .addressBlockName = "Main", .baseAddress = 0x5A00000, .size =  0x5C0000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "68000 Work RAM",              .addressBlockName = "Main", .baseAddress = 0x5A00000, .size =   0x80000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SCSP registers",              .addressBlockName = "Main", .baseAddress = 0x5B00000, .size =    0x1000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "VDP1 VRAM",                   .addressBlockName = "Main", .baseAddress = 0x5C00000, .size =   0x80000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "VDP1 framebuffer",            .addressBlockName = "Main", .baseAddress = 0x5C80000, .size =   0x40000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "VDP1 registers",              .addressBlockName = "Main", .baseAddress = 0x5D00000, .size =      0x20, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "VDP2 VRAM",                   .addressBlockName = "Main", .baseAddress = 0x5E00000, .size =   0x80000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "VDP2 CRAM",                   .addressBlockName = "Main", .baseAddress = 0x5F00000, .size =    0x1000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "VDP2 registers",              .addressBlockName = "Main", .baseAddress = 0x5F80000, .size =     0x200, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SCU registers",               .addressBlockName = "Main", .baseAddress = 0x5FE0000, .size =     0x100, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "High Work RAM",               .addressBlockName = "Main", .baseAddress = 0x6000000, .size =  0x100000, .readFn = MainBusRead, .writeFn = MainBusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
    };

    inline constexpr Region kMSH2Regions[] = {
        { .name = "MSH2 cached address space",   .addressBlockName = "MSH2", .baseAddress = 0x00000000, .size = 0x8000000, .readFn = SH2BusRead<true>, .writeFn = SH2BusWrite<true>, .bgColorFn = SH2BusBgColor<true>,    .paramsFn = SH2CachedAreaParams, .hoverFn = nullptr              },
        { .name = "MSH2 uncached address space", .addressBlockName = "MSH2", .baseAddress = 0x20000000, .size = 0x8000000, .readFn = SH2BusRead<true>, .writeFn = SH2BusWrite<true>, .bgColorFn = SH2BusBgColor<true>,    .paramsFn = nullptr,             .hoverFn = nullptr              },
        { .name = "MSH2 cache address array",    .addressBlockName = "MSH2", .baseAddress = 0x60000000, .size =     0x400, .readFn = SH2BusRead<true>, .writeFn = SH2BusWrite<true>, .bgColorFn = SH2CacheAddressBgColor, .paramsFn = nullptr,             .hoverFn = SH2CacheAddressHover },
        { .name = "MSH2 cache data array",       .addressBlockName = "MSH2", .baseAddress = 0xC0000000, .size =    0x1000, .readFn = SH2BusRead<true>, .writeFn = SH2BusWrite<true>, .bgColorFn = SH2CacheDataBgColor,    .paramsFn = nullptr,             .hoverFn = SH2CacheDataHover    },
        { .name = "MSH2 on-chip registers",      .addressBlockName = "MSH2", .baseAddress = 0xFFFFFE00, .size =     0x200, .readFn = SH2BusRead<true>, .writeFn = SH2BusWrite<true>, .bgColorFn = SH2BusBgColor<true>,    .paramsFn = nullptr,             .hoverFn = nullptr              },
    };

    inline constexpr Region kSSH2Regions[] = {
        { .name = "SSH2 cached address space",   .addressBlockName = "SSH2", .baseAddress = 0x00000000, .size = 0x8000000, .readFn = SH2BusRead<false>, .writeFn = SH2BusWrite<false>, .bgColorFn = SH2BusBgColor<false>,   .paramsFn = SH2CachedAreaParams, .hoverFn = nullptr              },
        { .name = "SSH2 uncached address space", .addressBlockName = "SSH2", .baseAddress = 0x20000000, .size = 0x8000000, .readFn = SH2BusRead<false>, .writeFn = SH2BusWrite<false>, .bgColorFn = SH2BusBgColor<false>,   .paramsFn = nullptr,             .hoverFn = nullptr              },
        { .name = "SSH2 cache address array",    .addressBlockName = "SSH2", .baseAddress = 0x60000000, .size =     0x400, .readFn = SH2BusRead<false>, .writeFn = SH2BusWrite<false>, .bgColorFn = SH2CacheAddressBgColor, .paramsFn = nullptr,             .hoverFn = SH2CacheAddressHover },
        { .name = "SSH2 cache data array",       .addressBlockName = "SSH2", .baseAddress = 0xC0000000, .size =    0x1000, .readFn = SH2BusRead<false>, .writeFn = SH2BusWrite<false>, .bgColorFn = SH2CacheDataBgColor,    .paramsFn = nullptr,             .hoverFn = SH2CacheDataHover    },
        { .name = "SSH2 on-chip registers",      .addressBlockName = "SSH2", .baseAddress = 0xFFFFFE00, .size =     0x200, .readFn = SH2BusRead<false>, .writeFn = SH2BusWrite<false>, .bgColorFn = SH2BusBgColor<false>,   .paramsFn = nullptr,             .hoverFn = nullptr              },
    };

    inline constexpr Region kSH1Regions[] = {
        { .name = "SH1 address space",               .addressBlockName = "SH1", .baseAddress = 0x00000000, .size = 0x8000000, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SH1 on-chip ROM",                 .addressBlockName = "SH1", .baseAddress = 0x00000000, .size =   0x10000, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SH1 on-chip supporting modules",  .addressBlockName = "SH1", .baseAddress = 0x05FFFE00, .size =     0x200, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "CD block DRAM",                   .addressBlockName = "SH1", .baseAddress = 0x09000000, .size =   0x80000, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "YGR registers (CD drive)",        .addressBlockName = "SH1", .baseAddress = 0x0A000000, .size =      0x20, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "YGR registers (MPEG card, low)",  .addressBlockName = "SH1", .baseAddress = 0x0A100000, .size =      0x40, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "YGR registers (MPEG card, high)", .addressBlockName = "SH1", .baseAddress = 0x0A180000, .size =      0x10, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "MPEG card ROM",                   .addressBlockName = "SH1", .baseAddress = 0x0E000000, .size =   0x80000, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
        { .name = "SH1 on-chip RAM",                 .addressBlockName = "SH1", .baseAddress = 0x0F000000, .size =    0x1000, .readFn = SH1BusRead, .writeFn = SH1BusWrite, .bgColorFn = MainBusBgColor, .paramsFn = nullptr, .hoverFn = nullptr },
    };

    inline constexpr RegionGroup kRegionGroups[] = {
        { .name = "Main address space", .regions = kMainRegions },
        { .name = "Master SH-2",        .regions = kMSH2Regions },
        { .name = "Slave SH-2",         .regions = kSSH2Regions },
        { .name = "CD block SH-1",      .regions = kSH1Regions  },
    };
    // clang-format on

} // namespace regions

} // namespace app::ui::mem_view
