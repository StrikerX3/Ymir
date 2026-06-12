#pragma once

#include "cmdline_opts.hpp"

#include "settings.hpp"
#include "shared_context.hpp"

#include "services/graphics_service.hpp"
#include "services/midi_service.hpp"
#include "services/mouse_capture_service.hpp"
#include "services/rom_service.hpp"
#include "services/save_state_service.hpp"
#include "services/screenshot_service.hpp"
#include "services/update_checker_service.hpp"

#include "ui/windows/about_window.hpp"
#include "ui/windows/backup_ram_manager_window.hpp"
#include "ui/windows/message_history_window.hpp"
#include "ui/windows/peripheral_config_window.hpp"
#include "ui/windows/settings_window.hpp"
#include "ui/windows/system_state_window.hpp"
#include "ui/windows/update_onboarding_window.hpp"
#include "ui/windows/update_window.hpp"

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
#include <set>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace app {

class App {
public:
    App();

    int Run(const CommandLineOptions &options);

private:
    CommandLineOptions m_options;

    SharedContext m_context;
    services::GraphicsService m_graphicsService;
    services::SaveStateService m_saveStateService;
    services::MIDIService m_midiService;
    services::ScreenshotService m_screenshotService;
    services::UpdateCheckerService m_updateCheckerService;
    Settings m_settings;
    services::MouseCaptureService m_mouseCaptureService;
    services::ROMService m_romService;

    SDL_PropertiesID m_fileDialogProps;

    std::thread m_emuThread;
    util::Event m_emuProcessEvent{};

    std::chrono::steady_clock::time_point m_mouseHideTime;

    void RunEmulator();

    void EmulatorThread();

    void OpenWelcomeModal(bool scanIPLROMS);

    void RebindInputs();
    void UpdateInputs(double timeDelta);
    void DrawInputs(ImDrawList *drawList);

    std::pair<float, float> WindowToScreen(float x, float y) const;

    void RescaleUI(float displayScale);
    ImGuiStyle &ReloadStyle(float displayScale);
    void LoadFonts();

    void OnDisplayAdded(SDL_DisplayID id);
    void OnDisplayRemoved(SDL_DisplayID id);

    void ApplyFullscreenMode() const;

    void PersistWindowGeometry();

    void LoadDebuggerState();
    void SaveDebuggerState();
    void CheckDebuggerStateDirty();

    template <int port>
    void ReadPeripheral(ymir::peripheral::PeripheralReport &report);

    void LoadSaveStates();
    void ClearSaveStates();
    void LoadSaveStateSlot(size_t slotIndex);
    void SaveSaveStateSlot(size_t slotIndex);
    void SelectSaveStateSlot(size_t slotIndex);
    void PersistSaveState(size_t slotIndex);
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
    ui::MessageHistoryWindow m_messageHistoryWindow;
    ui::AboutWindow m_aboutWindow;
    ui::UpdateOnboardingWindow m_updateOnboardingWindow;
    ui::UpdateWindow m_updateWindow;

    // -------------------------------------------------------------------------
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
