#include "emu_event_factory.hpp"

#include "gui_event_factory.hpp"

#include <app/shared_context.hpp>

#include <util/ipl_rom_loader.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/scope_guard.hpp>

#include <util/file_loader.hpp>

#include <fmt/std.h>

#include <fstream>
#include <iostream>

using namespace ymir;

namespace app::events::emu {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "Emulator";
    };

} // namespace grp

EmuEvent SetClockSpeed(sys::ClockSpeed clockSpeed) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.SetClockSpeed(clockSpeed); });
}

EmuEvent SetVideoStandard(core::config::sys::VideoStandard videoStandard) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.SetVideoStandard(videoStandard); });
}

EmuEvent SetAreaCode(uint8 areaCode) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.SMPC.SetAreaCode(areaCode); });
}

EmuEvent SetDeinterlace(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.VDP.SetDeinterlaceRender(enable); });
}

EmuEvent SetDebugTrace(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.EnableDebugTracing(enable);
        if (enable) {
            ctx.saturn.masterSH2.UseTracer(&ctx.tracers.masterSH2);
            ctx.saturn.slaveSH2.UseTracer(&ctx.tracers.slaveSH2);
            ctx.saturn.SCU.UseTracer(&ctx.tracers.SCU);
            ctx.saturn.VDP.UseTracer(&ctx.tracers.VDP);
            ctx.saturn.CDBlock.UseTracer(&ctx.tracers.CDBlock);
        }
        devlog::info<grp::base>("Debug tracing {}", (enable ? "enabled" : "disabled"));
    });
}

EmuEvent DumpMemory() {
    return RunFunction([](SharedContext &ctx) {
        auto dumpPath = ctx.profile.GetPath(ProfilePath::Dumps);
        std::error_code error{};
        std::filesystem::create_directories(dumpPath, error);
        if (error) {
            devlog::warn<grp::base>("Could not create dump directory {}: {}", dumpPath, error.message());
            return;
        }

        devlog::info<grp::base>("Dumping all memory to {}...", dumpPath);
        {
            std::ofstream out{dumpPath / "msh2-cache-data.bin", std::ios::binary};
            ctx.saturn.masterSH2.DumpCacheData(out);
        }
        {
            std::ofstream out{dumpPath / "msh2-cache-addrtag.bin", std::ios::binary};
            ctx.saturn.masterSH2.DumpCacheAddressTag(out);
        }
        {
            std::ofstream out{dumpPath / "ssh2-cache-data.bin", std::ios::binary};
            ctx.saturn.slaveSH2.DumpCacheData(out);
        }
        {
            std::ofstream out{dumpPath / "ssh2-cache-addrtag.bin", std::ios::binary};
            ctx.saturn.slaveSH2.DumpCacheAddressTag(out);
        }
        {
            std::ofstream out{dumpPath / "wram-lo.bin", std::ios::binary};
            ctx.saturn.mem.DumpWRAMLow(out);
        }
        {
            std::ofstream out{dumpPath / "wram-hi.bin", std::ios::binary};
            ctx.saturn.mem.DumpWRAMHigh(out);
        }
        {
            std::ofstream out{dumpPath / "vdp1-vram.bin", std::ios::binary};
            ctx.saturn.VDP.DumpVDP1VRAM(out);
        }
        {
            std::ofstream out{dumpPath / "vdp1-fbs.bin", std::ios::binary};
            ctx.saturn.VDP.DumpVDP1Framebuffers(out);
        }
        {
            std::ofstream out{dumpPath / "vdp2-vram.bin", std::ios::binary};
            ctx.saturn.VDP.DumpVDP2VRAM(out);
        }
        {
            std::ofstream out{dumpPath / "vdp2-cram.bin", std::ios::binary};
            ctx.saturn.VDP.DumpVDP2CRAM(out);
        }
        {
            std::ofstream out{dumpPath / "scu-dsp-prog.bin", std::ios::binary};
            ctx.saturn.SCU.DumpDSPProgramRAM(out);
        }
        {
            std::ofstream out{dumpPath / "scu-dsp-data.bin", std::ios::binary};
            ctx.saturn.SCU.DumpDSPDataRAM(out);
        }
        {
            std::ofstream out{dumpPath / "scu-dsp-regs.bin", std::ios::binary};
            ctx.saturn.SCU.DumpDSPRegs(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-wram.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpWRAM(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-mpro.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSP_MPRO(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-temp.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSP_TEMP(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-mems.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSP_MEMS(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-coef.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSP_COEF(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-madrs.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSP_MADRS(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-mixs.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSP_MIXS(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-efreg.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSP_EFREG(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-exts.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSP_EXTS(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-regs.bin", std::ios::binary};
            ctx.saturn.SCSP.DumpDSPRegs(out);
        }
        devlog::info<grp::base>("Dump complete");
    });
}

static void InsertPeripheral(peripheral::PeripheralType type, peripheral::PeripheralPort &port) {
    switch (type) {
    case ymir::peripheral::PeripheralType::None: port.DisconnectPeripherals(); break;
    case ymir::peripheral::PeripheralType::ControlPad: port.ConnectControlPad(); break;
    case ymir::peripheral::PeripheralType::AnalogPad: port.ConnectAnalogPad(); break;
    }
}

EmuEvent InsertPort1Peripheral(peripheral::PeripheralType type) {
    return RunFunction([=](SharedContext &ctx) {
        std::unique_lock lock{ctx.locks.peripherals};
        InsertPeripheral(type, ctx.saturn.SMPC.GetPeripheralPort1());
    });
}

EmuEvent InsertPort2Peripheral(peripheral::PeripheralType type) {
    return RunFunction([=](SharedContext &ctx) {
        std::unique_lock lock{ctx.locks.peripherals};
        InsertPeripheral(type, ctx.saturn.SMPC.GetPeripheralPort2());
    });
}

EmuEvent InsertBackupMemoryCartridge(std::filesystem::path path) {
    return RunFunction([=](SharedContext &ctx) {
        // Prevent loading the internal backup RAM file as backup memory cartridge
        if (std::filesystem::absolute(path) ==
            std::filesystem::absolute(ctx.settings.system.internalBackupRAMImagePath)) {
            ctx.EnqueueEvent(events::gui::ShowError(fmt::format(
                "Failed to load external backup memory: file {} is already in use as internal backup memory", path)));
            return;
        }

        std::error_code error{};
        bup::BackupMemory bupMem{};
        const auto result = bupMem.LoadFrom(path, error);
        switch (result) {
        case bup::BackupMemoryImageLoadResult::Success: //
        {
            auto *cart = ctx.saturn.InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));
            ctx.settings.cartridge.backupRAM.capacity = SizeToCapacity(cart->GetBackupMemory().Size());
            ctx.settings.cartridge.backupRAM.imagePath = path;
            devlog::info<grp::base>("External backup memory cartridge loaded from {}", path);
            break;
        }
        case bup::BackupMemoryImageLoadResult::FilesystemError:
            if (error) {
                ctx.EnqueueEvent(
                    events::gui::ShowError(fmt::format("Failed to load external backup memory: {}", error.message())));
            } else {
                ctx.EnqueueEvent(
                    events::gui::ShowError("Failed to load external backup memory: Unspecified file system error"));
            }
            break;
        case bup::BackupMemoryImageLoadResult::InvalidSize:
            ctx.EnqueueEvent(events::gui::ShowError("Failed to load external backup memory: Invalid image size"));
            break;
        default:
            ctx.EnqueueEvent(events::gui::ShowError("Failed to load external backup memory: Unexpected error"));
            break;
        };
    });
}

EmuEvent Insert8MbitDRAMCartridge() {
    return RunFunction([](SharedContext &ctx) { ctx.saturn.InsertCartridge<cart::DRAM8MbitCartridge>(); });
}

EmuEvent Insert32MbitDRAMCartridge() {
    return RunFunction([](SharedContext &ctx) { ctx.saturn.InsertCartridge<cart::DRAM32MbitCartridge>(); });
}

EmuEvent InsertROMCartridge(std::filesystem::path path) {
    return RunFunction([=](SharedContext &ctx) {
        // TODO: deduplicate code

        // Don't even bother if no path was specified
        if (path.empty()) {
            return;
        }

        std::error_code error{};
        std::vector<uint8> rom = util::LoadFile(path, error);

        // Check for file system errors
        if (error) {
            ctx.EnqueueEvent(
                events::gui::ShowError(fmt::format("Could not load ROM cartridge image: {}", error.message())));
            return;
        }

        // Check that the file has contents
        if (rom.empty()) {
            ctx.EnqueueEvent(
                events::gui::ShowError("Could not load ROM cartridge image: file is empty or could not be read."));
            return;
        }

        // Check that the image is not larger than the ROM cartridge capacity
        if (rom.size() > cart::kROMCartSize) {
            ctx.EnqueueEvent(events::gui::ShowError(fmt::format(
                "Could not load ROM cartridge image: file is too large ({} > {})", rom.size(), cart::kROMCartSize)));
            return;
        }

        // TODO: Check that the image is a proper Sega Saturn cartridge (headers)

        // Insert cartridge
        cart::ROMCartridge *cart = ctx.saturn.InsertCartridge<cart::ROMCartridge>();
        if (cart != nullptr) {
            devlog::info<grp::base>("16 Mbit ROM cartridge inserted with image from {}", path);
            cart->LoadROM(rom);
        }
    });
}

EmuEvent InsertCartridgeFromSettings() {
    return RunFunction([](SharedContext &ctx) {
        std::unique_lock lock{ctx.locks.cart};

        auto &settings = ctx.settings.cartridge;

        switch (settings.type) {
        case Settings::Cartridge::Type::None:
            ctx.saturn.RemoveCartridge();
            devlog::info<grp::base>("Cartridge removed");
            break;

        case Settings::Cartridge::Type::BackupRAM: {
            // Prevent loading the internal backup RAM file as backup memory cartridge
            if (std::filesystem::absolute(settings.backupRAM.imagePath) ==
                std::filesystem::absolute(ctx.settings.system.internalBackupRAMImagePath)) {
                ctx.EnqueueEvent(events::gui::ShowError(fmt::format(
                    "Failed to load external backup memory: file {} is already in use as internal backup memory",
                    settings.backupRAM.imagePath)));
                return;
            }

            // If a backup RAM cartridge is inserted, remove it first to unlock the file and reinsert the previous
            // cartridge in case of failure
            std::filesystem::path prevPath = "";
            util::ScopeGuard sgReinsertOnFailure{[&] {
                if (prevPath.empty()) {
                    return;
                }

                std::error_code error{};
                bup::BackupMemory bupMem{};
                auto result = bupMem.LoadFrom(prevPath, error);
                if (result == bup::BackupMemoryImageLoadResult::Success) {
                    ctx.saturn.InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));
                }
            }};
            if (auto *cart = ctx.saturn.GetCartridge().As<cart::CartType::BackupMemory>()) {
                prevPath = cart->GetBackupMemory().GetPath();
                if (prevPath == settings.backupRAM.imagePath) {
                    ctx.saturn.RemoveCartridge();
                } else {
                    sgReinsertOnFailure.Cancel();
                }
            } else {
                sgReinsertOnFailure.Cancel();
            }

            std::error_code error{};
            bup::BackupMemory bupMem{};
            bupMem.CreateFrom(settings.backupRAM.imagePath, CapacityToBupSize(settings.backupRAM.capacity), error);
            if (error) {
                devlog::info<grp::base>("Failed to insert {} backup RAM cartridge from {}: {}",
                                        BupCapacityShortName(settings.backupRAM.capacity), settings.backupRAM.imagePath,
                                        error.message());
            } else {
                devlog::info<grp::base>("{} backup RAM cartridge inserted with image from {}",
                                        BupCapacityShortName(settings.backupRAM.capacity),
                                        settings.backupRAM.imagePath);
                ctx.saturn.InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));

                // If the cartridge was successfully inserted, we don't need to reinsert the previous cartridge
                sgReinsertOnFailure.Cancel();
            }
            break;
        }
        case Settings::Cartridge::Type::DRAM:
            switch (settings.dram.capacity) {
            case Settings::Cartridge::DRAM::Capacity::_32Mbit:
                ctx.saturn.InsertCartridge<cart::DRAM32MbitCartridge>();
                devlog::info<grp::base>("32 Mbit DRAM cartridge inserted");
                break;
            case Settings::Cartridge::DRAM::Capacity::_8Mbit:
                ctx.saturn.InsertCartridge<cart::DRAM8MbitCartridge>();
                devlog::info<grp::base>("8 Mbit DRAM cartridge inserted");
                break;
            }
            break;
        case Settings::Cartridge::Type::ROM: //
        {
            // TODO: deduplicate code

            // Don't even bother if no path was specified
            if (settings.rom.imagePath.empty()) {
                break;
            }

            std::error_code error{};
            std::vector<uint8> rom = util::LoadFile(settings.rom.imagePath, error);

            // Check for file system errors
            if (error) {
                ctx.EnqueueEvent(
                    events::gui::ShowError(fmt::format("Could not load ROM cartridge image: {}", error.message())));
                return;
            }

            // Check that the file has contents
            if (rom.empty()) {
                ctx.EnqueueEvent(
                    events::gui::ShowError("Could not load ROM cartridge image: file is empty or could not be read."));
                return;
            }

            // Check that the image is not larger than the ROM cartridge capacity
            if (rom.size() > cart::kROMCartSize) {
                ctx.EnqueueEvent(events::gui::ShowError(
                    fmt::format("Could not load ROM cartridge image: file is too large ({} > {})", rom.size(),
                                cart::kROMCartSize)));
                return;
            }

            // TODO: Check that the image is a proper Sega Saturn cartridge (headers)

            // Insert cartridge
            cart::ROMCartridge *cart = ctx.saturn.InsertCartridge<cart::ROMCartridge>();
            if (cart != nullptr) {
                devlog::info<grp::base>("16 Mbit ROM cartridge inserted with image from {}", settings.rom.imagePath);
                cart->LoadROM(rom);
            }
            break;
        }
        }
    });
}

EmuEvent DeleteBackupFile(std::string filename, bool external) {
    if (external) {
        return RunFunction([=](SharedContext &ctx) {
            if (auto *cart = ctx.saturn.GetCartridge().As<cart::CartType::BackupMemory>()) {
                cart->GetBackupMemory().Delete(filename);
            }
        });
    } else {
        return RunFunction([=](SharedContext &ctx) { ctx.saturn.mem.GetInternalBackupRAM().Delete(filename); });
    }
}

EmuEvent FormatBackupMemory(bool external) {
    if (external) {
        return RunFunction([](SharedContext &ctx) {
            if (auto *cart = ctx.saturn.GetCartridge().As<cart::CartType::BackupMemory>()) {
                cart->GetBackupMemory().Format();
            }
        });
    } else {
        return RunFunction([](SharedContext &ctx) { ctx.saturn.mem.GetInternalBackupRAM().Format(); });
    }
}

EmuEvent LoadInternalBackupMemory() {
    return RunFunction([](SharedContext &ctx) {
        std::filesystem::path path;
        if (ctx.settings.system.internalBackupRAMPerGame) {
            const std::filesystem::path basePath = ctx.profile.GetPath(ProfilePath::BackupMemory) / "games";
            std::filesystem::create_directories(basePath);
            path = basePath / fmt::format("bup-int-{}.bin", ToString(ctx.saturn.GetDiscHash()));
        } else {
            path = ctx.settings.system.internalBackupRAMImagePath;
            if (path.empty()) {
                path = ctx.profile.GetPath(ProfilePath::PersistentState) / "bup-int.bin";
            }
        }

        std::error_code error{};
        if (ctx.saturn.LoadInternalBackupMemoryImage(path, error); error) {
            devlog::warn<grp::base>("Failed to load internal backup memory from {}: {}", path, error.message());
        } else {
            devlog::info<grp::base>("Internal backup memory image loaded from {}", path);
        }
    });
}

EmuEvent SetEmulateSH2Cache(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.EnableSH2CacheEmulation(enable);
        devlog::info<grp::base>("SH2 cache emulation {}", (enable ? "enabled" : "disabled"));
    });
}

EmuEvent EnableThreadedVDP(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.configuration.video.threadedVDP = enable; });
}

EmuEvent IncludeVDP1InVDPRenderThread(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.configuration.video.includeVDP1InRenderThread = enable; });
}

EmuEvent EnableThreadedSCSP(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.configuration.audio.threadedSCSP = enable; });
}

EmuEvent LoadState(uint32 slot) {
    return RunFunction([=](SharedContext &ctx) {
        if (slot < ctx.saveStates.size() && ctx.saveStates[slot]) {
            auto &state = *ctx.saveStates[slot];
            // If the IPL ROM is mismatched, load it if possible
            if (state.system.iplRomHash != ctx.saturn.GetIPLHash()) {
                devlog::warn<grp::base>("Save state IPL ROM hash mismatch; attempting to load IPL ROM with hash {}",
                                        ToString(state.system.iplRomHash));

                std::unique_lock lock{ctx.locks.romManager};
                for (auto &[path, info] : ctx.romManager.GetIPLROMs()) {
                    if (info.hash == state.system.iplRomHash) {
                        auto result = util::LoadIPLROM(path, ctx.saturn);
                        if (result.succeeded) {
                            devlog::info<grp::base>("IPL ROM matching save state loaded successfully from {}", path);
                            ctx.iplRomPath = path;
                        } else {
                            // If the IPL ROM doesn't exist of fails to load, we'll let it slide unless an SH-2 is in
                            // the IPL ROM area
                            devlog::warn<grp::base>("Failed to load IPL ROM from {} matching save state: {}", path,
                                                    result.errorMessage);
                            if (state.msh2.PC < 0x100000) {
                                devlog::warn<grp::base>("Not loading save state with a mismatched IPL ROM and the MSH2 "
                                                        "in IPL area ({:08X})",
                                                        state.msh2.PC);
                                return;
                            }
                            if (state.system.slaveSH2Enabled && state.ssh2.PC < 0x100000) {
                                devlog::warn<grp::base>("Not loading save state with a mismatched IPL ROM and the SSH2 "
                                                        "in IPL area ({:08X})",
                                                        state.ssh2.PC);
                                return;
                            }
                        }
                        break;
                    }
                }
            }

            if (!ctx.saturn.LoadState(state)) {
                devlog::warn<grp::base>("Failed to load save state");
            }
        }
    });
}

EmuEvent SaveState(uint32 slot) {
    return RunFunction([=](SharedContext &ctx) {
        if (slot < ctx.saveStates.size()) {
            {
                std::unique_lock lock{ctx.locks.saveStates[slot]};
                if (!ctx.saveStates[slot]) {
                    ctx.saveStates[slot] = std::make_unique<state::State>();
                }
                ctx.saturn.SaveState(*ctx.saveStates[slot]);
            }
            ctx.EnqueueEvent(events::gui::StateSaved(slot));
        }
    });
}

} // namespace app::events::emu
