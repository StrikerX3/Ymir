#pragma once

#include <app/input/input_action.hpp>
#include <app/input/input_events.hpp>
#include <app/ui/defs/settings_defs.hpp>

#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace app {

// A filter for the file dialog.
// Follows SDL3 rules:
// - filters must be specified
// - filters are a list of file extensions, separated by semicolons (e.g. "bmp;jpg;png")
// - use "*" to match all files
struct FileDialogFilter {
    const char *name;
    const char *filters;
};

// Parameters for open/save files dialogs.
struct FileDialogParams {
    std::string dialogTitle;
    std::filesystem::path defaultPath;
    std::vector<FileDialogFilter> filters;
    void *userdata;
    void (*callback)(void *userdata, const char *const *filelist, int filter);
};

// Parameters for select folder dialogs.
struct FolderDialogParams {
    std::string dialogTitle;
    std::filesystem::path defaultPath;
    void *userdata;
    void (*callback)(void *userdata, const char *const *filelist, int filter);
};

// Parameters for opening peripheral binds configuration windows.
struct PeripheralBindsParams {
    uint32 portIndex;
    uint32 slotIndex;
};

struct GUIEvent {
    enum class Type {
        LoadDisc,
        LoadRecommendedGameCartridge,
        OpenBackupMemoryCartFileDialog,
        OpenROMCartFileDialog,
        OpenPeripheralBindsEditor,

        OpenFile,      // Invoke generic open single file dialog; uses FileDialogParams
        OpenManyFiles, // Invoke generic open multiple files dialog; uses FileDialogParams
        SaveFile,      // Invoke generic save file dialog; uses FileDialogParams
        SelectFolder,  // Invoke generic select folder dialog; uses FolderDialogParams

        OpenBackupMemoryManager,
        OpenSettings,             // Opens a specific Settings tab; uses ui::SettingsTab
        OpenSH2DebuggerWindow,    // Opens an SH-2 debugger window; uses bool (true=MSH2, false=SSH2)
        OpenSH2BreakpointsWindow, // Opens an SH-2 breakpoints window; uses bool (true=MSH2, false=SSH2)
        OpenSH2WatchpointsWindow, // Opens an SH-2 watchpoints window; uses bool (true=MSH2, false=SSH2)

        SetProcessPriority,

        FitWindowToScreen,

        RebindInputs,
        ReloadGameControllerDatabase,

        ShowErrorMessage,

        EnableRewindBuffer,

        TryLoadIPLROM,
        ReloadIPLROM,
        TryLoadCDBlockROM,
        ReloadCDBlockROM,

        TakeScreenshot,

        // Emulator notifications

        StateLoaded, // A save state slot was just loaded
        StateSaved,  // A save state slot was just saved
    };

    Type type;
    std::variant<std::monostate, bool, uint32, std::string, std::filesystem::path, PeripheralBindsParams,
                 FileDialogParams, FolderDialogParams, ui::SettingsTab>
        value;
};

} // namespace app
