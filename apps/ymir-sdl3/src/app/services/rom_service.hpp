#pragma once

#include <app/settings.hpp>
#include <app/shared_context.hpp>
#include <util/rom_loader.hpp>

#include <filesystem>
#include <functional>
#include <string>

namespace app::services {

/// @brief Scans and loads system ROMs (IPL, CD Block) and cartridges.
class ROMService {
public:
    using ShowModalCallback = std::function<void(std::string title, std::function<void()> contents)>;

    ROMService(SharedContext &context, Settings &settings, ShowModalCallback showModal);
    ~ROMService() = default;

    ROMService(const ROMService &) = delete;
    ROMService &operator=(const ROMService &) = delete;

    /// @brief Reloads controller databases from user profiles.
    /// @param[in] showMessages Show confirmation messages in the UI.
    void ReloadSDLGameControllerDatabases(bool showMessages);

    /// @brief Reloads a controller database from a specific file.
    /// @param[in] path File path to the database.
    /// @param[in] showMessages Show confirmation messages in the UI.
    void ReloadSDLGameControllerDatabase(std::filesystem::path path, bool showMessages);

    /// @brief Scans the profile path for IPL (BIOS) ROMs.
    void ScanIPLROMs();

    /// @brief Loads the selected IPL ROM into the emulator.
    /// @return Result of the ROM loading operation.
    util::ROMLoadResult LoadIPLROM();

    struct IPLROM {
        std::filesystem::path path = "";
        const ymir::db::IPLROMInfo *info = nullptr;
    };

    /// @brief Gets the active IPL ROM file.
    /// @return File path.
    IPLROM GetIPLROM();

    /// @brief Scans the profile path for CD Block ROMs.
    void ScanCDBlockROMs();

    /// @brief Loads the selected CD Block ROM into the emulator.
    /// @return Result of the ROM loading operation.
    util::ROMLoadResult LoadCDBlockROM();

    /// @brief Gets the path to the active CD Block ROM file.
    /// @return File path.
    std::filesystem::path GetCDBlockROMPath();

    /// @brief Scans the profile path for cartridge ROMs.
    void ScanROMCarts();

    /// @brief Inserts the recommended cartridge (DRAM, Backup RAM, etc.) for the current disc.
    void LoadRecommendedCartridge();

    /// @brief Opens a dialog to load a backup memory cartridge file.
    void OpenBackupMemoryCartFileDialog();

    /// @brief Opens a dialog to load a ROM cartridge file.
    void OpenROMCartFileDialog();

private:
    SharedContext &m_context;
    Settings &m_settings;
    ShowModalCallback m_showModal;
};

} // namespace app::services
