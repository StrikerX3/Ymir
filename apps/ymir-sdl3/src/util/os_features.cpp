#include "os_features.hpp"

#if defined(_WIN32)
    #include <dwmapi.h>
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

} // namespace util::os
