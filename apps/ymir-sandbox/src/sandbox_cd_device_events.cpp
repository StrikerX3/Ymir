#include <SDL3/SDL.h>

#include <ymir/util/scope_guard.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

#include <cfgmgr32.h>
#include <winioctl.h> // for GUID_DEVINTERFACE_CDROM

#pragma comment(lib, "Cfgmgr32.lib")

void runCDDeviceEventsSandbox() {
    using clk = std::chrono::steady_clock;
    using namespace std::chrono_literals;

    // Screen parameters
    const uint32 screenWidth = 500;
    const uint32 screenHeight = 300;

    // ---------------------------------
    // Initialize SDL subsystems

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        return;
    }
    util::ScopeGuard sgQuit{[] { SDL_Quit(); }};

    // ---------------------------------
    // Create window

    SDL_PropertiesID windowProps = SDL_CreateProperties();
    if (windowProps == 0) {
        SDL_Log("Unable to create window properties: %s", SDL_GetError());
        return;
    }
    util::ScopeGuard sgDestroyWindowProps{[&] { SDL_DestroyProperties(windowProps); }};

    // Assume the following calls succeed
    SDL_SetStringProperty(windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Sandbox");
    SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, false);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, screenWidth);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, screenHeight);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);

    auto window = SDL_CreateWindowWithProperties(windowProps);
    if (window == nullptr) {
        SDL_Log("Unable to create window: %s", SDL_GetError());
        return;
    }
    util::ScopeGuard sgDestroyWindow{[&] { SDL_DestroyWindow(window); }};

#if defined(_WIN32)
    HCMNOTIFICATION hDeviceNotification = NULL;
    {
        CM_NOTIFY_FILTER filter{};
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_CDROM;
        DWORD result = CM_Register_Notification(
            &filter,
            (PVOID) nullptr, // context pointer
            [](HCMNOTIFICATION hNotify, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA eventData,
               DWORD eventDataSize) -> DWORD {
                if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) {
                    fmt::println("Device interface added");
                } else if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
                    fmt::println("Device interface removed");
                }
                return 0;
            },
            &hDeviceNotification);
    }
#endif

    // ---------------------------------
    // Create renderer

    SDL_PropertiesID rendererProps = SDL_CreateProperties();
    if (rendererProps == 0) {
        SDL_Log("Unable to create renderer properties: %s", SDL_GetError());
        return;
    }
    util::ScopeGuard sgDestroyRendererProps{[&] { SDL_DestroyProperties(rendererProps); }};

    // Assume the following calls succeed
    SDL_SetPointerProperty(rendererProps, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
    // SDL_SetNumberProperty(rendererProps, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, SDL_RENDERER_VSYNC_DISABLED);
    // SDL_SetNumberProperty(rendererProps, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, SDL_RENDERER_VSYNC_ADAPTIVE);
    SDL_SetNumberProperty(rendererProps, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, 1);

    auto renderer = SDL_CreateRendererWithProperties(rendererProps);
    if (renderer == nullptr) {
        SDL_Log("Unable to create renderer: %s", SDL_GetError());
        return;
    }
    util::ScopeGuard sgDestroyRenderer{[&] { SDL_DestroyRenderer(renderer); }};

    // ---------------------------------
    // Main loop

    auto t = clk::now();
    uint64 frames = 0;
    bool running = true;

    bool pressed = false;

    while (running) {
        SDL_Event evt{};

        while (SDL_PollEvent(&evt)) {
            switch (evt.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN: pressed = true; break;
            case SDL_EVENT_MOUSE_BUTTON_UP: pressed = false; break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: pressed = true; break;
            case SDL_EVENT_GAMEPAD_BUTTON_UP: pressed = false; break;
            case SDL_EVENT_QUIT: running = false; break;
            }
        }

        ++frames;
        auto t2 = clk::now();
        if (t2 - t >= 1s) {
            auto title = fmt::format("{} fps", frames);
            SDL_SetWindowTitle(window, title.c_str());
            frames = 0;
            t = t2;
        }

        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
}
