#include "emu_event_factory.hpp"

#include "gui_event_factory.hpp"

#include <app/shared_context.hpp>

#include <app/services/savestates/save_state_service.hpp>

#include <memory>
#include <ymir/sys/saturn.hpp>

#include <util/rom_loader.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/scope_guard.hpp>

#include <util/file_loader.hpp>

#include <fstream>

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
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->SetClockSpeed(clockSpeed); });
}

EmuEvent SetVideoStandard(core::config::sys::VideoStandard videoStandard) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->SetVideoStandard(videoStandard); });
}

EmuEvent SetAreaCode(uint8 areaCode) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->SMPC.SetAreaCode(areaCode); });
}

EmuEvent SetDeinterlace(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->VDP.SetDeinterlaceRender(enable); });
}

EmuEvent SetTransparentMeshes(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->VDP.SetTransparentMeshes(enable); });
}

EmuEvent SetDebugTrace(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.instance->EnableDebugTracing(enable);
        if (enable) {
            ctx.saturn.instance->masterSH2.UseTracer(&ctx.tracers.masterSH2);
            ctx.saturn.instance->slaveSH2.UseTracer(&ctx.tracers.slaveSH2);
            ctx.saturn.instance->SCU.UseTracer(&ctx.tracers.SCU);
            ctx.saturn.instance->SCSP.UseTracer(&ctx.tracers.SCSP);
            ctx.saturn.instance->CDBlock.UseTracer(&ctx.tracers.CDBlock);
            ctx.saturn.instance->CDDrive.UseTracer(&ctx.tracers.CDDrive);
            ctx.saturn.instance->YGR.UseTracer(&ctx.tracers.YGR);
        }
        ctx.DisplayMessage(fmt::format("Debug tracing {}", (enable ? "enabled" : "disabled")));
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
            ctx.saturn.instance->masterSH2.DumpCacheData(out);
        }
        {
            std::ofstream out{dumpPath / "msh2-cache-addrtag.bin", std::ios::binary};
            ctx.saturn.instance->masterSH2.DumpCacheAddressTag(out);
        }
        {
            std::ofstream out{dumpPath / "ssh2-cache-data.bin", std::ios::binary};
            ctx.saturn.instance->slaveSH2.DumpCacheData(out);
        }
        {
            std::ofstream out{dumpPath / "ssh2-cache-addrtag.bin", std::ios::binary};
            ctx.saturn.instance->slaveSH2.DumpCacheAddressTag(out);
        }
        {
            std::ofstream out{dumpPath / "wram-lo.bin", std::ios::binary};
            ctx.saturn.instance->mem.DumpWRAMLow(out);
        }
        {
            std::ofstream out{dumpPath / "wram-hi.bin", std::ios::binary};
            ctx.saturn.instance->mem.DumpWRAMHigh(out);
        }
        {
            std::ofstream out{dumpPath / "vdp1-vram.bin", std::ios::binary};
            ctx.saturn.instance->VDP.DumpVDP1VRAM(out);
        }
        {
            std::ofstream out{dumpPath / "vdp1-fbs.bin", std::ios::binary};
            ctx.saturn.instance->VDP.DumpVDP1Framebuffers(out);
        }
        {
            std::ofstream out{dumpPath / "vdp2-vram.bin", std::ios::binary};
            ctx.saturn.instance->VDP.DumpVDP2VRAM(out);
        }
        {
            std::ofstream out{dumpPath / "vdp2-cram.bin", std::ios::binary};
            ctx.saturn.instance->VDP.DumpVDP2CRAM(out);
        }
        {
            std::ofstream out{dumpPath / "scu-dsp-prog.bin", std::ios::binary};
            ctx.saturn.instance->SCU.DumpDSPProgramRAM(out);
        }
        {
            std::ofstream out{dumpPath / "scu-dsp-data.bin", std::ios::binary};
            ctx.saturn.instance->SCU.DumpDSPDataRAM(out);
        }
        {
            std::ofstream out{dumpPath / "scu-dsp-regs.bin", std::ios::binary};
            ctx.saturn.instance->SCU.DumpDSPRegs(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-wram.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpWRAM(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-mpro.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSP_MPRO(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-temp.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSP_TEMP(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-mems.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSP_MEMS(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-coef.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSP_COEF(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-madrs.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSP_MADRS(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-mixs.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSP_MIXS(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-efreg.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSP_EFREG(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-exts.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSP_EXTS(out);
        }
        {
            std::ofstream out{dumpPath / "scsp-dsp-regs.bin", std::ios::binary};
            ctx.saturn.instance->SCSP.DumpDSPRegs(out);
        }
        {
            std::ofstream out{dumpPath / "sh1-ram.bin", std::ios::binary};
            ctx.saturn.instance->SH1.DumpRAM(out);
        }
        {
            std::ofstream out{dumpPath / "cdb-dram.bin", std::ios::binary};
            ctx.saturn.instance->DumpCDBlockDRAM(out);
        }
        devlog::info<grp::base>("Dump complete");
    });
}

// handler to dump a specified memory region from the mem viewer
EmuEvent DumpMemRegion(const ui::mem_view::MemoryViewerState &memView) {
    return RunFunction([memView](const SharedContext &ctx) {
        // get path from ctx, setup errors, try dir creation
        const auto dumpPath = ctx.profile.GetPath(ProfilePath::Dumps);
        std::error_code ec{};
        std::filesystem::create_directories(dumpPath, ec);
        if (ec) {
            devlog::warn<grp::base>("Could not create dump directory {}: {}", dumpPath, ec.message());
            return;
        }

        // get size from region, check readFn
        const auto *region = memView.selectedRegion;
        if (!region || region->size == 0 || !region->readFn) {
            // could happen for invalid region or if no readFn exists
            devlog::warn<grp::base>("DumpMemRegion: invalid region/readFn/size");
            return;
        }
        const uint32_t size = region->size;

        devlog::info<grp::base>("Dumping memory region {}...", region->name);

        // fill buffer /w same size as mem region
        std::vector<std::uint8_t> buf(size);
        void *userData = memView.memoryEditor.UserData;
        for (uint32_t i = 0; i < size; ++i) {
            buf[i] = region->readFn(nullptr, i, userData);
        }

        // filename sanitizer to be safe
        auto sanitize = [](std::string s) {
            for (char &c : s) {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') {
                    c = '_';
                }
            }
            return s;
        };

        // get game product num, setup path
        const auto &productNumber = ctx.saturn.GetDisc().header.productNumber;
        const auto outPath = dumpPath / fmt::format("{}_{}_{:08X}_{}B.bin", productNumber, sanitize(region->name),
                                                    region->baseAddress, size);

        // write to dump path
        std::ofstream out{outPath, std::ios::binary | std::ios::trunc};
        if (!out) {
            devlog::warn<grp::base>("DumpMemRegion: failed to open {}", outPath);
            return;
        }
        out.write(reinterpret_cast<const char *>(buf.data()), static_cast<std::streamsize>(buf.size()));

        ctx.DisplayMessage(fmt::format("Dumped {} bytes from [{}:{:08X}..{:08X}] to {}", size, region->addressBlockName,
                                       region->baseAddress, region->baseAddress + size - 1, outPath));
    });
}

static void InsertPeripheral(peripheral::PeripheralType type, peripheral::PeripheralPort &port) {
    switch (type) {
    case ymir::peripheral::PeripheralType::None: port.DisconnectPeripherals(); break;
    case ymir::peripheral::PeripheralType::ControlPad: port.ConnectControlPad(); break;
    case ymir::peripheral::PeripheralType::AnalogPad: port.ConnectAnalogPad(); break;
    case ymir::peripheral::PeripheralType::ArcadeRacer: port.ConnectArcadeRacer(); break;
    case ymir::peripheral::PeripheralType::MissionStick: port.ConnectMissionStick(); break;
    }
}

EmuEvent InsertPort1Peripheral(peripheral::PeripheralType type) {
    return RunFunction([=](SharedContext &ctx) {
        std::unique_lock lock{ctx.locks.peripherals};
        InsertPeripheral(type, ctx.saturn.instance->SMPC.GetPeripheralPort1());
    });
}

EmuEvent InsertPort2Peripheral(peripheral::PeripheralType type) {
    return RunFunction([=](SharedContext &ctx) {
        std::unique_lock lock{ctx.locks.peripherals};
        InsertPeripheral(type, ctx.saturn.instance->SMPC.GetPeripheralPort2());
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
            auto *cart = ctx.saturn.instance->InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));
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
    return RunFunction([](SharedContext &ctx) { ctx.saturn.instance->InsertCartridge<cart::DRAM8MbitCartridge>(); });
}

EmuEvent Insert32MbitDRAMCartridge() {
    return RunFunction([](SharedContext &ctx) { ctx.saturn.instance->InsertCartridge<cart::DRAM32MbitCartridge>(); });
}

EmuEvent Insert48MbitDRAMCartridge() {
    return RunFunction([](SharedContext &ctx) { ctx.saturn.instance->InsertCartridge<cart::DRAM48MbitCartridge>(); });
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
        cart::ROMCartridge *cart = ctx.saturn.instance->InsertCartridge<cart::ROMCartridge>();
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
            ctx.saturn.instance->RemoveCartridge();
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

            // Use default path for specified size
            if (settings.backupRAM.imagePath.empty()) {
                settings.backupRAM.imagePath =
                    ctx.profile.GetPath(ProfilePath::PersistentState) /
                    fmt::format("bup-ext-{}M.bin", CapacityToSize(settings.backupRAM.capacity) * 8 / 1024 / 1024);
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
                    ctx.saturn.instance->InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));
                }
            }};
            if (auto *cart = ctx.saturn.instance->GetCartridge().As<cart::CartType::BackupMemory>()) {
                prevPath = cart->GetBackupMemory().GetPath();
                if (prevPath == settings.backupRAM.imagePath) {
                    ctx.saturn.instance->RemoveCartridge();
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
                ctx.saturn.instance->InsertCartridge<cart::BackupMemoryCartridge>(std::move(bupMem));

                // If the cartridge was successfully inserted, we don't need to reinsert the previous cartridge
                sgReinsertOnFailure.Cancel();
            }
            break;
        }
        case Settings::Cartridge::Type::DRAM:
            switch (settings.dram.capacity) {
            case Settings::Cartridge::DRAM::Capacity::_48Mbit:
                ctx.saturn.instance->InsertCartridge<cart::DRAM48MbitCartridge>();
                devlog::info<grp::base>("48 Mbit DRAM dev cartridge inserted");
                break;
            case Settings::Cartridge::DRAM::Capacity::_32Mbit:
                ctx.saturn.instance->InsertCartridge<cart::DRAM32MbitCartridge>();
                devlog::info<grp::base>("32 Mbit DRAM cartridge inserted");
                break;
            case Settings::Cartridge::DRAM::Capacity::_8Mbit:
                ctx.saturn.instance->InsertCartridge<cart::DRAM8MbitCartridge>();
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
            cart::ROMCartridge *cart = ctx.saturn.instance->InsertCartridge<cart::ROMCartridge>();
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
            if (auto *cart = ctx.saturn.instance->GetCartridge().As<cart::CartType::BackupMemory>()) {
                cart->GetBackupMemory().Delete(filename);
            }
        });
    } else {
        return RunFunction(
            [=](SharedContext &ctx) { ctx.saturn.instance->mem.GetInternalBackupRAM().Delete(filename); });
    }
}

EmuEvent FormatBackupMemory(bool external) {
    if (external) {
        return RunFunction([](SharedContext &ctx) {
            if (auto *cart = ctx.saturn.instance->GetCartridge().As<cart::CartType::BackupMemory>()) {
                cart->GetBackupMemory().Format();
            }
        });
    } else {
        return RunFunction([](SharedContext &ctx) { ctx.saturn.instance->mem.GetInternalBackupRAM().Format(); });
    }
}

EmuEvent LoadInternalBackupMemory() {
    return RunFunction([](SharedContext &ctx) {
        std::filesystem::path path = ctx.GetInternalBackupRAMPath();

        std::error_code error{};
        if (ctx.saturn.instance->LoadInternalBackupMemoryImage(path, error); error) {
            devlog::warn<grp::base>("Failed to load internal backup memory from {}: {}", path, error.message());
        } else {
            devlog::info<grp::base>("Internal backup memory image loaded from {}", path);
        }
    });
}

EmuEvent SetEmulateSH2Cache(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        const bool currEnable = ctx.saturn.instance->IsSH2CacheEmulationEnabled();
        if (currEnable != enable) {
            ctx.saturn.instance->EnableSH2CacheEmulation(enable);
            devlog::info<grp::base>("SH2 cache emulation {}", (enable ? "enabled" : "disabled"));
        }
    });
}

EmuEvent SetCDBlockLLE(bool enable) {
    return RunFunction([=](SharedContext &ctx) {
        ctx.saturn.instance->configuration.cdblock.useLLE = enable;
        ctx.rewindBuffer.Reset();
    });
}

EmuEvent EnableThreadedVDP1(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.settings.video.threadedVDP1 = enable; });
}

EmuEvent EnableThreadedVDP2(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.settings.video.threadedVDP2 = enable; });
}

EmuEvent EnableThreadedDeinterlacer(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.settings.video.threadedDeinterlacer = enable; });
}

EmuEvent EnableThreadedSCSP(bool enable) {
    return RunFunction([=](SharedContext &ctx) { ctx.settings.audio.threadedSCSP = enable; });
}

EmuEvent SetSCSPStepGranularity(uint32 granularity) {
    return RunFunction([=](SharedContext &ctx) { ctx.saturn.instance->SCSP.SetStepGranularity(granularity); });
}

EmuEvent LoadState(uint32 slot) {
    return RunFunction([=](SharedContext &ctx) {
        // grab the service and check for bounds
        auto &saves = ctx.serviceLocator.GetRequired<services::SaveStateService>();
        if (slot >= saves.Size()) {
            return;
        }

        // sanity check: do slotState and underlying state exist?
        auto lock = std::unique_lock{saves.SlotMutex(slot)};
        auto slotState = saves.Peek(slot);
        if (!slotState || !slotState->get().state) {
            ctx.DisplayMessage(fmt::format("Save state slot {} selected", slot + 1));
            return;
        }

        // grab the savestate
        const auto &saveState = slotState->get();
        auto &state = *saveState.state;

        // Sanity check: ensure that the disc hash matches
        {
            if (!state.ValidateDiscHash(ctx.saturn.GetDiscHash())) {
                devlog::warn<grp::base>("Save state disc hash mismatch; refusing to load save state");
                return;
            }
        }

        // Check for IPL and CD block ROM mismatches and locate and load matching ROMs if possible.
        // Refuse to load the save state otherwise.

        std::filesystem::path candidateIPLROMPath{};
        std::filesystem::path candidateCDBROMPath{};

        // Locate ROMs
        if (!state.ValidateIPLROMHash(ctx.saturn.instance->GetIPLHash())) {
            devlog::warn<grp::base>("Save state IPL ROM hash mismatch; locating IPL ROM with hash {}",
                                    ToString(state.system.iplRomHash));

            std::unique_lock lock{ctx.locks.romManager};
            for (auto &[path, info] : ctx.romManager.GetIPLROMs()) {
                if (info.hash == state.system.iplRomHash) {
                    candidateIPLROMPath = path;
                    devlog::info<grp::base>("Found matching IPL ROM at {}", path);
                    break;
                }
            }
            if (candidateIPLROMPath.empty()) {
                devlog::warn<grp::base>("Could not find matching IPL ROM. Refusing to load save state");
                return;
            }
        }

        if (!state.ValidateCDBlockROMHash(ctx.saturn.instance->SH1.GetROMHash())) {
            devlog::warn<grp::base>("Save state CD block ROM hash mismatch; locating CD block ROM with hash {}",
                                    ToString(state.sh1.romHash));

            std::unique_lock lock{ctx.locks.romManager};
            for (auto &[path, info] : ctx.romManager.GetCDBlockROMs()) {
                if (info.hash == state.sh1.romHash) {
                    candidateCDBROMPath = path;
                    devlog::info<grp::base>("Found matching CD block ROM at {}", path);
                    break;
                }
            }
            if (candidateCDBROMPath.empty()) {
                devlog::warn<grp::base>("Could not find matching CD block ROM. Refusing to load save state");
                return;
            }
        }

        // ROMs to load, if possible, into the Saturn instance.
        // Empty optional means "don't load, the Saturn instance already contains the correct ROM."
        std::optional<std::vector<uint8>> iplROMData{};
        std::optional<std::vector<uint8>> cdbROMData{};

        // Load ROMs if needed
        if (!candidateIPLROMPath.empty()) {
            std::error_code error{};
            iplROMData = util::LoadFile(candidateIPLROMPath, error);
            if (error) {
                devlog::warn<grp::base>("Could not load IPL ROM: {}. Refusing to load save state", error.message());
                return;
            }
            if (iplROMData->size() != ymir::sys::kIPLSize) {
                devlog::warn<grp::base>(
                    "Could not load IPL ROM: size mismatch - must be {} bytes. Refusing to load save state",
                    ymir::sys::kIPLSize);
                return;
            }
        }
        if (!candidateCDBROMPath.empty()) {
            std::error_code error{};
            cdbROMData = util::LoadFile(candidateCDBROMPath, error);
            if (error) {
                devlog::warn<grp::base>("Could not load CD block ROM: {}. Refusing to load save state",
                                        error.message());
                return;
            }
            if (cdbROMData->size() != ymir::sh1::kROMSize) {
                devlog::warn<grp::base>(
                    "Could not load CD block ROM: size mismatch - must be {} bytes. Refusing to load save state",
                    ymir::sh1::kROMSize);
                return;
            }
        }

        // At this point the ROMs have been loaded and validated

        if (ctx.saturn.instance->LoadState(state, true)) {
            // Now that the save state has been succesfully loaded, load the ROMs
            if (iplROMData) {
                ctx.saturn.instance->LoadIPL(std::span<uint8, sys::kIPLSize>(*iplROMData));
                ctx.iplRomPath = candidateIPLROMPath;
                ctx.DisplayMessage(fmt::format("IPL ROM used by save state loaded from {}", ctx.iplRomPath));
            }
            if (cdbROMData) {
                ctx.saturn.instance->LoadCDBlockROM(std::span<uint8, sh1::kROMSize>(*cdbROMData));
                ctx.cdbRomPath = candidateCDBROMPath;
                ctx.DisplayMessage(fmt::format("CD block ROM used by save state loaded from {}", ctx.cdbRomPath));
            }

            ctx.EnqueueEvent(events::gui::StateLoaded(slot));
        } else {
            devlog::warn<grp::base>("Failed to load save state");
        }
    });
}

EmuEvent SaveState(uint32 slot) {
    return RunFunction([=](SharedContext &ctx) {
        // grab the service and check bounds
        auto &saves = ctx.serviceLocator.GetRequired<services::SaveStateService>();
        if (slot >= saves.Size()) {
            return;
        }

        {
            // grab the lock and a new state
            auto lock = std::unique_lock{saves.SlotMutex(slot)};
            savestates::SaveState slotState{};

            // build new state logically
            // test if state is present and either clone or create a new one
            auto savePtr = saves.Peek(slot);
            slotState.state = savePtr && savePtr->get().state ? std::make_unique<state::State>(*savePtr->get().state)
                                                              : std::make_unique<state::State>();

            // save state to selected slot and set timestamp
            ctx.saturn.instance->SaveState(*slotState.state);
            slotState.timestamp = std::chrono::system_clock::now();
            const bool ok = saves.Set(slot, std::move(slotState));
            // check for catastrophic OOB (should not happen)
            if (!ok) {
                devlog::warn<grp::base>("Could not set/save new save state for slot {}", slot);
                return;
            }
        }

        ctx.EnqueueEvent(events::gui::StateSaved(slot));
    });
}

} // namespace app::events::emu
