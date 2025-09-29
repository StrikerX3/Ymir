#pragma once

#include "cmdline_opts.hpp"

#include "shared_context.hpp"

#include "ui/windows/about_window.hpp"
#include "ui/windows/backup_ram_manager_window.hpp"
#include "ui/windows/peripheral_config_window.hpp"
#include "ui/windows/settings_window.hpp"
#include "ui/windows/system_state_window.hpp"

#include "ui/windows/debug/cdblock_window_set.hpp"
#include "ui/windows/debug/debug_output_window.hpp"
#include "ui/windows/debug/memory_viewer_window.hpp"
#include "ui/windows/debug/scsp_window_set.hpp"
#include "ui/windows/debug/scu_window_set.hpp"
#include "ui/windows/debug/sh2_window_set.hpp"
#include "ui/windows/debug/vdp_window_set.hpp"

#include <ymir/hw/smpc/peripheral/peripheral_report.hpp>

#include <util/rom_loader.hpp>

#include <ymir/util/dev_log.hpp>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_properties.h>

#include <imgui.h>

#include <chrono>
#include <filesystem>
#include <string_view>
#include <thread>
#include <vector>

namespace app {

class App {
public:
    App();

    int Run(const CommandLineOptions &options);

private:
    CommandLineOptions m_options;

    SharedContext m_context;
    SDL_PropertiesID m_fileDialogProps;

    std::thread m_emuThread;
    util::Event m_emuProcessEvent{};

    std::chrono::steady_clock::time_point m_mouseHideTime;

    struct Screenshot {
        std::vector<uint32> fb;
        uint32 fbWidth, fbHeight;
        uint32 fbScaleX, fbScaleY;
        int ssScale;
        std::chrono::system_clock::time_point timestamp;
    };

    std::thread m_screenshotThread;
    util::Event m_writeScreenshotEvent;
    std::queue<Screenshot> m_screenshotQueue;
    std::mutex m_screenshotQueueMtx;
    bool m_screenshotThreadRunning;

    void RunEmulator();

    void EmulatorThread();
    void ScreenshotThread();

    void OpenWelcomeModal(bool scanIPLROMS);

    void RebindInputs();

    void RescaleUI(float displayScale);
    ImGuiStyle &ReloadStyle(float displayScale);
    void LoadFonts();

    void PersistWindowGeometry();

    void LoadDebuggerState();
    void SaveDebuggerState();
    void CheckDebuggerStateDirty();

    template <int port>
    void ReadPeripheral(ymir::peripheral::PeripheralReport &report);

    void ReloadSDLGameControllerDatabases(bool showMessages);
    void ReloadSDLGameControllerDatabase(std::filesystem::path path, bool showMessages);

    void ScanIPLROMs();
    util::ROMLoadResult LoadIPLROM();
    std::filesystem::path GetIPLROMPath();

    void ScanCDBlockROMs();
    util::ROMLoadResult LoadCDBlockROM();
    std::filesystem::path GetCDBlockROMPath();

    void ScanROMCarts();
    void LoadRecommendedCartridge();

    void LoadSaveStates();
    void ClearSaveStates();
    void LoadSaveStateSlot(size_t slot);
    void SaveSaveStateSlot(size_t slot);
    void SelectSaveStateSlot(size_t slot);
    void PersistSaveState(size_t slot);
    void WriteSaveStateMeta();

    void EnableRewindBuffer(bool enable);
    void ToggleRewindBuffer();

    void OpenLoadDiscDialog();
    void ProcessOpenDiscImageFileDialogSelection(const char *const *filelist, int filter);
    bool LoadDiscImage(std::filesystem::path path, bool showErrorModal);
    void LoadRecentDiscs();
    void SaveRecentDiscs();

    void OpenBackupMemoryCartFileDialog();
    void ProcessOpenBackupMemoryCartFileDialogSelection(const char *const *filelist, int filter);

    void OpenROMCartFileDialog();
    void ProcessOpenROMCartFileDialogSelection(const char *const *filelist, int filter);

    void InvokeOpenFileDialog(const FileDialogParams &params) const;
    void InvokeOpenManyFilesDialog(const FileDialogParams &params) const;
    void InvokeSaveFileDialog(const FileDialogParams &params) const;
    void InvokeSelectFolderDialog(const FolderDialogParams &params) const;

    void InvokeFileDialog(SDL_FileDialogType type, const char *title, void *filters, int numFilters, bool allowMany,
                          const char *location, void *userdata, SDL_DialogFileCallback callback) const;

    static void OnMidiInputReceived(double delta, std::vector<unsigned char> *msg, void *userData);

    // -----------------------------------------------------------------------------------------------------------------
    // Windows

    void DrawWindows();
    void OpenMemoryViewer();
    void OpenPeripheralBindsEditor(const PeripheralBindsParams &params);

    ui::SystemStateWindow m_systemStateWindow;
    ui::BackupMemoryManagerWindow m_bupMgrWindow;

    ui::SH2WindowSet m_masterSH2WindowSet;
    ui::SH2WindowSet m_slaveSH2WindowSet;
    ui::SCUWindowSet m_scuWindowSet;
    ui::SCSPWindowSet m_scspWindowSet;
    ui::VDPWindowSet m_vdpWindowSet;
    ui::CDBlockWindowSet m_cdblockWindowSet;

    ui::DebugOutputWindow m_debugOutputWindow;

    std::vector<ui::MemoryViewerWindow> m_memoryViewerWindows;

    ui::SettingsWindow m_settingsWindow;
    ui::PeripheralConfigWindow m_periphConfigWindow;
    ui::AboutWindow m_aboutWindow;

    // Generic modal dialog

    void DrawGenericModal();

    void OpenSimpleErrorModal(std::string message);
    void OpenGenericModal(std::string title, std::function<void()> fnContents, bool showOKButton = true);

    bool m_openGenericModal = false;          // Open generic modal on the next frame
    bool m_closeGenericModal = false;         // Close generic modal on the next frame
    bool m_showOkButtonInGenericModal = true; // Show OK button on generic modal
    std::string m_genericModalTitle = "Message";
    std::function<void()> m_genericModalContents;

    // Rewind bar
    std::chrono::steady_clock::time_point m_rewindBarFadeTimeBase;
};

} // namespace app
