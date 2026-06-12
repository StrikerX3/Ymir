#pragma once

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_properties.h>
#include <app/events/gui_event.hpp>
#include <app/settings.hpp>
#include <app/shared_context.hpp>

namespace app::services {

/// @brief Shows system file and folder dialogs.
class FileDialogService {
public:
    FileDialogService(SharedContext &context, Settings &settings);
    ~FileDialogService();

    FileDialogService(const FileDialogService &) = delete;
    FileDialogService &operator=(const FileDialogService &) = delete;

    /// @brief Binds the dialog to a window and sets up properties.
    /// @param[in] window Target SDL window.
    void Initialize(SDL_Window *window);

    /// @brief Shows a single file selection dialog.
    /// @param[in] params Dialog parameters.
    void InvokeOpenFileDialog(const FileDialogParams &params) const;

    /// @brief Shows a multi-file selection dialog.
    /// @param[in] params Dialog parameters.
    void InvokeOpenManyFilesDialog(const FileDialogParams &params) const;

    /// @brief Shows a file save dialog.
    /// @param[in] params Dialog parameters.
    void InvokeSaveFileDialog(const FileDialogParams &params) const;

    /// @brief Shows a directory selection dialog.
    /// @param[in] params Dialog parameters.
    void InvokeSelectFolderDialog(const FolderDialogParams &params) const;

    /// @brief Direct helper to configure and show an SDL dialog.
    void InvokeFileDialog(SDL_FileDialogType type, const char *title, void *filters, int numFilters, bool allowMany,
                          const char *location, void *userdata, SDL_DialogFileCallback callback) const;

private:
    SharedContext &m_context;
    Settings &m_settings;
    SDL_PropertiesID m_fileDialogProps = 0;
};

} // namespace app::services
