#include "rom_service.hpp"

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>
#include <app/profile.hpp>
#include <app/services/file_dialog_service.hpp>
#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <ymir/core/hash.hpp>
#include <ymir/db/game_db.hpp>
#include <ymir/media/disc.hpp>
#include <ymir/sys/saturn.hpp>
#include <ymir/util/dev_log.hpp>

#include <SDL3/SDL.h>
#include <cassert>
#include <fmt/format.h>
#include <fmt/std.h>
#include <imgui.h>
#include <mutex>

namespace app::services {

ROMService::ROMService(SharedContext &context, Settings &settings, ShowModalCallback showModal)
    : m_context(context)
    , m_settings(settings)
    , m_showModal(std::move(showModal)) {}

void ROMService::ReloadSDLGameControllerDatabases(bool showMessages) {
    // Load the profile included with the emulator, which should always live next to the executable, followed by the
    // database from the profile
    ReloadSDLGameControllerDatabase(Profile::GetPortableProfilePath() / kGameControllerDBFile, showMessages);
    ReloadSDLGameControllerDatabase(m_context.profile.GetPath(ProfilePath::Root) / kGameControllerDBFile, showMessages);
}

void ROMService::ReloadSDLGameControllerDatabase(std::filesystem::path path, bool showMessages) {
    if (std::filesystem::is_regular_file(path)) {
        devlog::info<grp::base>("Loading game controller database from {}...", path);
        int result = SDL_AddGamepadMappingsFromFile(path.string().c_str());
        if (result < 0) {
            if (showMessages) {
                m_context.DisplayMessage(
                    fmt::format("Failed to load game controller database at {}: {}", path, SDL_GetError()));
            } else {
                devlog::warn<grp::base>("Failed to load game controller database: {}", SDL_GetError());
            }
        } else {
            devlog::info<grp::base>("Game controller database loaded: {} controllers added", result);
            if (showMessages) {
                m_context.DisplayMessage(
                    fmt::format("Game controller database loaded from {}: {} controllers added", path, result));
            }
        }
    } else {
        if (showMessages) {
            m_context.DisplayMessage(fmt::format("Game controller database not found at {}", path));
        } else {
            devlog::warn<grp::base>("Game controller database not found at {}", path);
        }
    }

    char **list = SDL_GetGamepadMappings(&m_context.gameControllerDBCount);
    SDL_free(list);
}

void ROMService::ScanIPLROMs() {
    auto romsPath = m_context.profile.GetPath(ProfilePath::IPLROMImages);
    devlog::info<grp::base>("Scanning for IPL ROMs in {}...", romsPath);

    {
        std::unique_lock lock{m_context.locks.romManager};
        m_context.romManager.ScanIPLROMs(romsPath);
    }

    if constexpr (devlog::info_enabled<grp::base>) {
        int numKnown = 0;
        int numUnknown = 0;
        for (auto &[path, entry] : m_context.romManager.GetIPLROMs()) {
            if (entry.info != nullptr) {
                ++numKnown;
            } else {
                ++numUnknown;
                devlog::debug<grp::base>("Unknown image: hash {}, path {}", ymir::ToString(entry.hash), path);
            }
        }
        devlog::info<grp::base>("Found {} images - {} known, {} unknown", numKnown + numUnknown, numKnown, numUnknown);
    }
}

util::ROMLoadResult ROMService::LoadIPLROM() {
    IPLROM ipl = GetIPLROM();
    if (ipl.path.empty()) {
        devlog::warn<grp::base>("No IPL ROM found");
        return util::ROMLoadResult::Fail("No IPL ROM found");
    }

    devlog::info<grp::base>("Loading IPL ROM from {}...", ipl.path);
    util::ROMLoadResult result = util::LoadIPLROM(ipl.path, *m_context.saturn.instance);
    if (result.succeeded) {
        m_context.iplRomPath = ipl.path;
        m_context.iplRomInfo = ipl.info;
        devlog::info<grp::base>("IPL ROM loaded successfully");
        m_context.EnqueueEvent(events::gui::IPLROMLoaded());
    } else {
        devlog::error<grp::base>("Failed to load IPL ROM: {}", result.errorMessage);
    }
    return result;
}

ROMService::IPLROM ROMService::GetIPLROM() {
    // Load from settings if override is enabled
    if (m_settings.system.ipl.overrideImage && !m_settings.system.ipl.path.empty()) {
        devlog::info<grp::base>("Using IPL ROM overridden by settings");
        const auto &roms = m_context.romManager.GetIPLROMs();
        const ymir::db::IPLROMInfo *info = nullptr;
        auto it = roms.find(m_settings.system.ipl.path);
        if (it != roms.end()) {
            info = it->second.info;
        }
        return {m_settings.system.ipl.path, info};
    }

    // Auto-select ROM from IPL ROM manager based on preferred system variant and area code

    ymir::db::SystemVariant preferredVariant = m_settings.system.ipl.variant;

    // SMPC area codes:
    //   0x1  J  Domestic NTSC        Japan
    //   0x2  T  Asia NTSC            Asia Region (Taiwan, Philippines, South Korea)
    //   0x4  U  North America NTSC   North America (US, Canada), Latin America (Brazil only)
    //   0xC  E  PAL                  Europe, Southeast Asia (China, Middle East), Latin America
    // Replaced SMPC area codes:
    //   0x5  B -> U
    //   0x6  K -> T
    //   0xA  A -> E
    //   0xD  L -> E
    // For all others, use region-free ROMs if available.

    ymir::db::SystemRegion preferredRegion;
    switch (m_context.saturn.instance->SMPC.GetAreaCode()) {
    case 0x1: preferredRegion = ymir::db::SystemRegion::JP; break;

    case 0x2: [[fallthrough]];
    case 0x6: preferredRegion = ymir::db::SystemRegion::KR; break;

    case 0x4: [[fallthrough]];
    case 0x5: [[fallthrough]];
    case 0xA: [[fallthrough]];
    case 0xC: [[fallthrough]];
    case 0xD: preferredRegion = ymir::db::SystemRegion::US_EU; break;

    default: preferredRegion = ymir::db::SystemRegion::None; break;
    }

    // Try to find exact match
    // Keep a region-free fallback in case there isn't a perfect match
    IPLROM regionFreeMatch{};
    IPLROM variantMatch{};
    IPLROM firstMatch{};
    for (auto &[path, entry] : m_context.romManager.GetIPLROMs()) {
        if (entry.info == nullptr) {
            continue;
        }
        if (firstMatch.path.empty()) {
            firstMatch = {path, entry.info};
        }
        if (entry.info->regionFree && regionFreeMatch.path.empty()) {
            regionFreeMatch = {path, entry.info};
        }
        if (preferredVariant == ymir::db::SystemVariant::None || entry.info->variant == preferredVariant) {
            if (entry.info->region == preferredRegion) {
                devlog::info<grp::base>("Using auto-detected IPL ROM");
                return {path, entry.info};
            } else {
                variantMatch = {path, entry.info};
            }
        }
    }

    // Return region-free fallback
    // May be empty if no region-free ROMs were found
    if (!regionFreeMatch.path.empty()) {
        devlog::info<grp::base>("Using auto-detected region-free IPL ROM");
        return regionFreeMatch;
    }

    // Fallback to variant match if found
    if (!variantMatch.path.empty()) {
        devlog::info<grp::base>("Using auto-detected variant IPL ROM with mismatched region");
        return variantMatch;
    }

    return firstMatch;
}

void ROMService::ScanCDBlockROMs() {
    auto romsPath = m_context.profile.GetPath(ProfilePath::CDBlockROMImages);
    devlog::info<grp::base>("Scanning for CD Block ROMs in {}...", romsPath);

    {
        std::unique_lock lock{m_context.locks.romManager};
        m_context.romManager.ScanCDBlockROMs(romsPath);
    }

    if constexpr (devlog::info_enabled<grp::base>) {
        int numKnown = 0;
        int numUnknown = 0;
        for (auto &[path, entry] : m_context.romManager.GetCDBlockROMs()) {
            if (entry.info != nullptr) {
                ++numKnown;
            } else {
                ++numUnknown;
                devlog::debug<grp::base>("Unknown image: hash {}, path {}", ymir::ToString(entry.hash), path);
            }
        }
        devlog::info<grp::base>("Found {} images - {} known, {} unknown", numKnown + numUnknown, numKnown, numUnknown);
    }
}

util::ROMLoadResult ROMService::LoadCDBlockROM() {
    std::filesystem::path romPath = GetCDBlockROMPath();
    if (romPath.empty()) {
        devlog::warn<grp::base>("No CD Block ROM found");
        if (m_settings.cdblock.useLLE) {
            m_settings.cdblock.useLLE = false;
            m_context.DisplayMessage("Low level CD block emulation disabled: no ROMs found");
        }
        return util::ROMLoadResult::Fail("No CD Block ROM found");
    }

    devlog::info<grp::base>("Loading CD Block ROM from {}...", romPath);
    util::ROMLoadResult result = util::LoadCDBlockROM(romPath, *m_context.saturn.instance);
    if (result.succeeded) {
        m_context.cdbRomPath = romPath;
        devlog::info<grp::base>("CD Block ROM loaded successfully");
    } else {
        devlog::error<grp::base>("Failed to load CD Block ROM: {}", result.errorMessage);
    }
    return result;
}

std::filesystem::path ROMService::GetCDBlockROMPath() {
    // Load from settings if override is enabled
    if (m_settings.cdblock.overrideROM && !m_settings.cdblock.romPath.empty()) {
        if (std::filesystem::is_regular_file(m_settings.cdblock.romPath)) {
            devlog::info<grp::base>("Using CD Block ROM overridden by settings");
            return m_settings.cdblock.romPath;
        }
        m_settings.cdblock.romPath = "";
    }

    // Use first available match otherwise
    if (!m_context.romManager.GetCDBlockROMs().empty()) {
        return m_context.romManager.GetCDBlockROMs().begin()->first;
    }
    return "";
}

void ROMService::ScanROMCarts() {
    auto romCartsPath = m_context.profile.GetPath(ProfilePath::ROMCartImages);
    devlog::info<grp::base>("Scanning for cartridge ROMs in {}...", romCartsPath);

    {
        std::unique_lock lock{m_context.locks.romManager};
        std::error_code error{};
        m_context.romManager.ScanROMCarts(romCartsPath, error);
        if (error) {
            devlog::warn<grp::base>("Failed to read ROM carts folder: {}", error.message());
        }
    }

    if constexpr (devlog::info_enabled<grp::base>) {
        int numKnown = 0;
        int numUnknown = 0;
        for (auto &[path, entry] : m_context.romManager.GetROMCarts()) {
            if (entry.info != nullptr) {
                ++numKnown;
            } else {
                ++numUnknown;
                devlog::debug<grp::base>("Unknown image: hash {}, path {}", ymir::ToString(entry.hash), path);
            }
        }
        devlog::info<grp::base>("Found {} images - {} known, {} unknown", numKnown + numUnknown, numKnown, numUnknown);
    }
}

void ROMService::LoadRecommendedCartridge() {
    const ymir::db::GameInfo *info;
    {
        std::unique_lock lock{m_context.locks.disc};
        const auto &discHeader = m_context.saturn.GetDiscHeader();
        info = ymir::db::GetGameInfo(discHeader.productNumber, m_context.saturn.GetDiscHash());
    }
    if (info == nullptr) {
        m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
        return;
    }

    devlog::info<grp::base>("Loading recommended game cartridge...");

    std::unique_lock lock{m_context.locks.cart};
    using Cart = ymir::db::Cartridge;
    switch (info->GetCartridge()) {
    case Cart::None: break;
    case Cart::DRAM8Mbit: m_context.EnqueueEvent(events::emu::Insert8MbitDRAMCartridge()); break;
    case Cart::DRAM32Mbit: m_context.EnqueueEvent(events::emu::Insert32MbitDRAMCartridge()); break;
    case Cart::DRAM48Mbit: m_context.EnqueueEvent(events::emu::Insert48MbitDRAMCartridge()); break;
    case Cart::ROM_KOF95: [[fallthrough]];
    case Cart::ROM_Ultraman: //
    {
        ScanROMCarts();
        const auto expectedHash =
            info->GetCartridge() == Cart::ROM_KOF95 ? ymir::db::kKOF95ROMInfo.hash : ymir::db::kUltramanROMInfo.hash;
        bool found = false;
        for (auto &[path, info] : m_context.romManager.GetROMCarts()) {
            if (info.hash == expectedHash) {
                m_context.EnqueueEvent(events::emu::InsertROMCartridge(path));
                found = true;
                break;
            }
        }
        if (!found) {
            m_showModal("Compatible ROM cart not found", [&context = m_context] {
                ImGui::TextUnformatted(
                    "Could not find required ROM cartridge image. This game will not boot properly.\n"
                    "\n"
                    "Place the image in the following directory:");
                auto path = context.profile.GetPath(ProfilePath::ROMCartImages);
                if (ImGui::TextLink(fmt::format("{}", path).c_str())) {
                    SDL_OpenURL(fmt::format("file:///{}", path).c_str());
                }
            });
        }
        break;
    }
    case Cart::BackupRAM: //
    {
        // TODO: centralize backup RAM image management tasks somewhere
        std::filesystem::path cartPath =
            m_context.GetPerGameExternalBackupRAMPath(ymir::bup::BackupMemorySize::_32Mbit);
        std::error_code error{};
        ymir::bup::BackupMemory bupMem{};
        bupMem.CreateFrom(cartPath, false, error, ymir::bup::BackupMemorySize::_32Mbit);
        if (error) {
            m_context.EnqueueEvent(
                events::gui::ShowError(fmt::format("Failed to load external backup memory: {}", error.message())));
        } else {
            m_context.EnqueueEvent(events::emu::InsertBackupMemoryCartridge(cartPath));
        }
        break;
    }
    }

    // TODO: notify user
}

void ROMService::OpenBackupMemoryCartFileDialog() {
    static constexpr SDL_DialogFileFilter kFileFilters[] = {
        {.name = "Backup memory images (*.bin, *.sav)", .pattern = "bin;sav"},
        {.name = "All files (*.*)", .pattern = "*"},
    };

    std::filesystem::path defaultPath = "";
    {
        std::unique_lock lock{m_context.locks.cart};
        if (auto *cart = m_context.saturn.instance->GetCartridge().As<ymir::cart::CartType::BackupMemory>()) {
            defaultPath = cart->GetBackupMemory().GetPath();
        }
    }

    auto &fileDialogService = m_context.serviceLocator.GetRequired<FileDialogService>();
    fileDialogService.InvokeFileDialog(
        SDL_FILEDIALOG_OPENFILE, "Load Sega Saturn backup memory image", (void *)kFileFilters, std::size(kFileFilters),
        false, fmt::format("{}", defaultPath).c_str(), this,
        [](void *userdata, const char *const *filelist, int filter) {
            auto *service = static_cast<ROMService *>(userdata);
            if (filelist == nullptr) {
                devlog::error<grp::base>("Failed to open file dialog: {}", SDL_GetError());
            } else if (*filelist == nullptr) {
                devlog::info<grp::base>("File dialog cancelled");
            } else {
                // Only one file should be selected
                const char *file = *filelist;
                service->m_context.EnqueueEvent(events::emu::InsertBackupMemoryCartridge(file));
            }
        });
}

void ROMService::OpenROMCartFileDialog() {
    static constexpr SDL_DialogFileFilter kFileFilters[] = {
        {.name = "ROM cartridge images (*.bin, *.ic1)", .pattern = "bin;ic1"},
        {.name = "All files (*.*)", .pattern = "*"},
    };

    const auto &settings = m_settings;
    std::filesystem::path defaultPath = settings.cartridge.rom.imagePath;

    auto &fileDialogService = m_context.serviceLocator.GetRequired<FileDialogService>();
    fileDialogService.InvokeFileDialog(
        SDL_FILEDIALOG_OPENFILE, "Load 16 Mbit ROM cartridge image", (void *)kFileFilters, std::size(kFileFilters),
        false, fmt::format("{}", defaultPath).c_str(), this,
        [](void *userdata, const char *const *filelist, int filter) {
            auto *service = static_cast<ROMService *>(userdata);
            if (filelist == nullptr) {
                devlog::error<grp::base>("Failed to open file dialog: {}", SDL_GetError());
            } else if (*filelist == nullptr) {
                devlog::info<grp::base>("File dialog cancelled");
            } else {
                // Only one file should be selected
                const char *file = *filelist;
                service->m_context.EnqueueEvent(events::emu::InsertROMCartridge(file));
            }
        });
}

} // namespace app::services
