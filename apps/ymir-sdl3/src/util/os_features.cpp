#include "os_features.hpp"

#if defined(_WIN32)
    #include <dwmapi.h>
    #include <fileapi.h>
#endif

namespace util::os {

void ConfigureWindowDecorations(SDL_Window *window) {
#if defined(_WIN32)
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    auto hwnd =
        static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, INVALID_HANDLE_VALUE));
    DWM_WINDOW_CORNER_PREFERENCE cornerPref = DWMWCP_DONOTROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));
#endif
}

void SetFileHidden(std::filesystem::path path, bool hidden) {
#if defined(_WIN32)
    DWORD attrs = GetFileAttributesW(path.wstring().c_str());
    const bool isHidden = attrs & FILE_ATTRIBUTE_HIDDEN;
    if (isHidden != hidden) {
        attrs ^= FILE_ATTRIBUTE_HIDDEN;
        SetFileAttributesW(path.wstring().c_str(), attrs);
    }
#endif
}

} // namespace util::os
