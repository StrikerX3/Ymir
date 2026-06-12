#pragma once

#include "cmdline_opts.hpp"

#include "settings.hpp"
#include "shared_context.hpp"

#include "services/disc_service.hpp"
#include "services/display_service.hpp"
#include "services/file_dialog_service.hpp"
#include "services/graphics_service.hpp"
#include "services/input_service.hpp"
#include "services/midi_service.hpp"
#include "services/mouse_capture_service.hpp"
#include "services/rom_service.hpp"
#include "services/save_state_service.hpp"
#include "services/screenshot_service.hpp"
#include "services/update_checker_service.hpp"

#include "services/window_manager_service.hpp"

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
    services::DiscService m_discService;
    services::DisplayService m_displayService;
    services::FileDialogService m_fileDialogService;
    services::WindowManagerService m_windowManagerService;
    services::InputService m_inputService;

    std::thread m_emuThread;
    util::Event m_emuProcessEvent{};

    std::chrono::steady_clock::time_point m_mouseHideTime;

    void RunEmulator();

    void EmulatorThread();

    void LoadDebuggerState();
    void SaveDebuggerState();
    void CheckDebuggerStateDirty();

    void LoadSaveStates();
    void ClearSaveStates();
    void LoadSaveStateSlot(size_t slotIndex);
    void SaveSaveStateSlot(size_t slotIndex);
    void SelectSaveStateSlot(size_t slotIndex);
    void PersistSaveState(size_t slotIndex);
    void WriteSaveStateMeta();

    void EnableRewindBuffer(bool enable);
    void ToggleRewindBuffer();

    void OpenBackupMemoryCartFileDialog();
    void ProcessOpenBackupMemoryCartFileDialogSelection(const char *const *filelist, int filter);

    void OpenROMCartFileDialog();
    void ProcessOpenROMCartFileDialogSelection(const char *const *filelist, int filter);

    static void OnMidiInputReceived(double delta, std::vector<unsigned char> *msg, void *userData);

    // Rewind bar
    std::chrono::steady_clock::time_point m_rewindBarFadeTimeBase;
};

} // namespace app
