#include "file_dialog_service.hpp"
#include "display_service.hpp"

#include <SDL3/SDL.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <ymir/util/dev_log.hpp>

namespace app::services {

static const char *StrNullIfEmpty(const std::string &str) {
    return str.empty() ? nullptr : str.c_str();
}

FileDialogService::FileDialogService(SharedContext &context, Settings &settings)
    : m_context(context)
    , m_settings(settings) {}

FileDialogService::~FileDialogService() {
    if (m_fileDialogProps != 0) {
        SDL_DestroyProperties(m_fileDialogProps);
    }
}

void FileDialogService::Initialize(SDL_Window *window) {
    m_fileDialogProps = SDL_CreateProperties();
    if (m_fileDialogProps != 0) {
        SDL_SetPointerProperty(m_fileDialogProps, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window);
    }
}

void FileDialogService::InvokeOpenFileDialog(const FileDialogParams &params) const {
    InvokeFileDialog(SDL_FILEDIALOG_OPENFILE, StrNullIfEmpty(params.dialogTitle), (void *)params.filters.data(),
                     params.filters.size(), false, StrNullIfEmpty(fmt::format("{}", params.defaultPath)),
                     params.userdata, params.callback);
}

void FileDialogService::InvokeOpenManyFilesDialog(const FileDialogParams &params) const {
    InvokeFileDialog(SDL_FILEDIALOG_OPENFILE, StrNullIfEmpty(params.dialogTitle), (void *)params.filters.data(),
                     params.filters.size(), true, StrNullIfEmpty(fmt::format("{}", params.defaultPath)),
                     params.userdata, params.callback);
}

void FileDialogService::InvokeSaveFileDialog(const FileDialogParams &params) const {
    InvokeFileDialog(SDL_FILEDIALOG_SAVEFILE, StrNullIfEmpty(params.dialogTitle), (void *)params.filters.data(),
                     params.filters.size(), false, StrNullIfEmpty(fmt::format("{}", params.defaultPath)),
                     params.userdata, params.callback);
}

void FileDialogService::InvokeSelectFolderDialog(const FolderDialogParams &params) const {
    // FIXME: Sadly, there's either a Windows or an SDL3 limitation that prevents us from using an UTF-8 path here
    InvokeFileDialog(SDL_FILEDIALOG_OPENFOLDER, StrNullIfEmpty(params.dialogTitle), nullptr, 0, false,
                     StrNullIfEmpty(params.defaultPath.string()), params.userdata, params.callback);
}

void FileDialogService::InvokeFileDialog(SDL_FileDialogType type, const char *title, void *filters, int numFilters,
                                         bool allowMany, const char *location, void *userdata,
                                         SDL_DialogFileCallback callback) const {
    SDL_PropertiesID props = m_fileDialogProps;

    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, filters);
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, numFilters);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, allowMany);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location);

    if (m_settings.video.fullScreen && !m_settings.video.borderlessFullScreen) {
        devlog::debug<grp::base>("Switching to borderless fullscreen mode before opening file dialog");

        // If in exclusive fullscreen mode, switch to borderless fullscreen mode temporarily
        SDL_SetWindowFullscreenMode(m_context.screen.window, nullptr);
        SDL_SetWindowFullscreen(m_context.screen.window, true);
        SDL_SyncWindow(m_context.screen.window);

        // Pass callback and user data pointer to file dialog properties
        SDL_SetPointerProperty(props, "ymir.filedialog.callback", (void *)callback);
        SDL_SetPointerProperty(props, "ymir.filedialog.userdata", userdata);

        SDL_ShowFileDialogWithProperties(
            type,
            [](void *userdata, const char *const *filelist, int filter) {
                const auto *service = static_cast<const FileDialogService *>(userdata);
                SDL_PropertiesID props = service->m_fileDialogProps;
                auto callback =
                    (SDL_DialogFileCallback)SDL_GetPointerProperty(props, "ymir.filedialog.callback", nullptr);
                auto *cbUserdata = SDL_GetPointerProperty(props, "ymir.filedialog.userdata", nullptr);

                callback(cbUserdata, filelist, filter);

                devlog::debug<grp::base>("Restoring exclusive fullscreen mode after closing file dialog");

                // Restore fullscreen mode after processing the callback
                if (auto *displayService = service->m_context.serviceLocator.Get<DisplayService>()) {
                    displayService->ApplyFullscreenMode();
                }
            },
            const_cast<FileDialogService *>(this), props);
    } else {
        SDL_ShowFileDialogWithProperties(type, callback, userdata, props);
    }
}

} // namespace app::services
