#pragma once

#include "cmdline_opts.hpp"

#include "audio_system.hpp"
#include "shared_context.hpp"

#include "ui/windows/about_window.hpp"
#include "ui/windows/backup_ram_manager_window.hpp"
#include "ui/windows/peripheral_binds_window.hpp"
#include "ui/windows/settings_window.hpp"
#include "ui/windows/system_state_window.hpp"

#include "ui/windows/debug/cdblock_window_set.hpp"
#include "ui/windows/debug/debug_output_window.hpp"
#include "ui/windows/debug/memory_viewer_window.hpp"
#include "ui/windows/debug/scu_window_set.hpp"
#include "ui/windows/debug/sh2_window_set.hpp"
#include "ui/windows/debug/vdp_window_set.hpp"

#include <util/ipl_rom_loader.hpp>

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

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "App";
    };

} // namespace grp

class App {
public:
    App();

    int Run(const CommandLineOptions &options);

private:
    CommandLineOptions m_options;

    SharedContext m_context;
    SDL_PropertiesID m_fileDialogProps;

    std::thread m_emuThread;

    AudioSystem m_audioSystem;

    std::chrono::steady_clock::time_point m_mouseHideTime;

    util::Event m_emuProcessEvent{};

    void RunEmulator();

    void EmulatorThread();

    void OpenWelcomeModal(bool scanIPLROMS);

    void RebindInputs();

    void RescaleUI(bool reloadFonts);
    ImGuiStyle &ReloadStyle(float displayScale);
    void ReloadFonts(float displayScale);

    template <int port>
    void ReadPeripheral(ymir::peripheral::PeripheralReport &report);

    void ScanIPLROMs();
    util::IPLROMLoadResult LoadIPLROM();
    std::filesystem::path GetIPLROMPath();

    void ScanROMCarts();
    void LoadRecommendedCartridge();

    void LoadSaveStates();
    void PersistSaveState(uint32 slot);
    void WriteSaveStateMeta();
    void EnableRewindBuffer(bool enable);
    void ToggleRewindBuffer();

    void OpenLoadDiscDialog();
    void ProcessOpenDiscImageFileDialogSelection(const char *const *filelist, int filter);
    bool LoadDiscImage(std::filesystem::path path);
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
    ui::VDPWindowSet m_vdpWindowSet;
    ui::CDBlockWindowSet m_cdblockWindowSet;

    ui::DebugOutputWindow m_debugOutputWindow;

    std::vector<ui::MemoryViewerWindow> m_memoryViewerWindows;

    ui::SettingsWindow m_settingsWindow;
    ui::PeripheralBindsWindow m_periphBindsWindow;
    ui::AboutWindow m_aboutWindow;

    // Error modal dialog

    void DrawGenericModal();

    void OpenSimpleErrorModal(std::string message);
    void OpenGenericModal(std::string title, std::function<void()> fnContents);

    bool m_openGenericModal = false;  // Open error modal on the next frame
    bool m_closeGenericModal = false; // Close error modal on the next frame
    std::string m_genericModalTitle = "Message";
    std::function<void()> m_genericModalContents;

    // Rewind bar
    std::chrono::steady_clock::time_point m_rewindBarFadeTimeBase;
};

} // namespace app
