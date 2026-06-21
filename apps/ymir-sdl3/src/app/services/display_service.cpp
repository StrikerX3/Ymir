#include "display_service.hpp"

#include <app/profile.hpp>
#include <app/settings.hpp>
#include <app/shared_context.hpp>
#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <ymir/util/dev_log.hpp>
#include <ymir/util/scope_guard.hpp>

#include <SDL3/SDL.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <cmrc/cmrc.hpp>
#include <imgui.h>

#include <algorithm>
#include <fstream>
#include <numbers>
#include <span>
#include <string_view>
#include <vector>

CMRC_DECLARE(Ymir_sdl3_rc);

namespace app::services {

DisplayService::DisplayService(SharedContext &context, Settings &settings)
    : m_context(context)
    , m_settings(settings) {}

void DisplayService::RescaleUI(float displayScale) {
    const auto &settings = m_settings;
    if (settings.gui.overrideUIScale) {
        displayScale = settings.gui.uiScale;
    }
    devlog::info<grp::base>("Window DPI scaling: {:.1f}%", displayScale * 100.0f);

    m_context.displayScale = displayScale;
    devlog::info<grp::base>("UI scaling set to {:.1f}%", m_context.displayScale * 100.0f);
    ReloadStyle(m_context.displayScale);
}

ImGuiStyle &DisplayService::ReloadStyle(float displayScale) {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(4, 3);
    style.ItemSpacing = ImVec2(7, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 21.0f;
    style.ScrollbarSize = 15.0f;
    style.GrabMinSize = 12.0f;
    style.WindowBorderSize = 1.0f * displayScale;
    style.ChildBorderSize = 1.0f * displayScale;
    style.PopupBorderSize = 1.0f * displayScale;
    style.FrameBorderSize = 0.0f * displayScale;
    style.WindowRounding = 3.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 1.0f;
    style.PopupRounding = 1.0f;
    style.ScrollbarRounding = 1.0f;
    style.GrabRounding = 1.0f;
    style.TabBorderSize = 0.0f * displayScale;
    style.TabBarBorderSize = 1.0f * displayScale;
    style.TabBarOverlineSize = 2.0f;
    style.TabCloseButtonMinWidthSelected = -1.0f;
    style.TabCloseButtonMinWidthUnselected = 0.0f;
    style.TabRounding = 2.0f;
    style.CellPadding = ImVec2(3, 2);
    style.TableAngledHeadersAngle = 50.0f * (2.0f * std::numbers::pi / 360.0f);
    style.TableAngledHeadersTextAlign = ImVec2(0.50f, 0.00f);
    style.WindowTitleAlign = ImVec2(0.50f, 0.50f);
    style.WindowBorderHoverPadding = 5.0f;
    style.WindowMenuButtonPosition = ImGuiDir_Left;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.50f, 0.50f);
    style.SelectableTextAlign = ImVec2(0.00f, 0.00f);
    style.SeparatorTextBorderSize = 2.0f * displayScale;
    style.SeparatorTextPadding = ImVec2(21, 2);
    style.LogSliderDeadzone = 4.0f;
    style.ImageBorderSize = 0.0f;
    style.DockingSeparatorSize = 2.0f;
    style.DisplayWindowPadding = ImVec2(21, 21);
    style.DisplaySafeAreaPadding = ImVec2(3, 3);
    style.ScaleAllSizes(displayScale);
    style.FontScaleMain = displayScale;

    // Setup Dear ImGui colors
    ImVec4 *colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(0.91f, 0.92f, 0.94f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.38f, 0.39f, 0.41f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.06f, 0.08f, 0.95f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.18f, 0.26f, 0.18f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.06f, 0.09f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.60f, 0.65f, 0.77f, 0.31f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.22f, 0.51f, 0.66f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.36f, 0.62f, 0.80f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.63f, 0.71f, 0.92f, 0.84f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.23f, 0.36f, 0.72f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.11f, 0.13f, 0.59f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.05f, 0.06f, 0.09f, 0.95f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.05f, 0.05f, 0.69f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.29f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.39f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.46f, 0.52f, 0.64f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.42f, 0.94f, 1.00f);
    colors[ImGuiCol_CheckboxSelectedBg] = ImVec4(0.20f, 0.42f, 0.94f, 0.45f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.43f, 0.57f, 0.91f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.74f, 0.82f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.26f, 0.46f, 0.98f, 0.40f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.46f, 0.98f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.51f, 0.64f, 0.99f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.26f, 0.46f, 0.98f, 0.40f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.46f, 0.98f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.48f, 0.98f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.46f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.46f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.46f, 0.98f, 0.95f);
    colors[ImGuiCol_InputTextCursor] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.46f, 0.98f, 0.80f);
    colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.29f, 0.58f, 0.86f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.33f, 0.68f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.26f, 0.46f, 0.98f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.07f, 0.09f, 0.15f, 0.97f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.14f, 0.22f, 0.42f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.46f, 0.98f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.53f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.67f, 0.25f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextLink] = ImVec4(0.37f, 0.54f, 1.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.43f, 0.59f, 0.98f, 0.43f);
    colors[ImGuiCol_TreeLines] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.97f, 0.60f, 0.19f, 0.90f);
    colors[ImGuiCol_DragDropTargetBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_UnsavedMarker] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
    colors[ImGuiCol_NavCursor] = ImVec4(0.26f, 0.46f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.07f, 0.07f, 0.07f, 0.35f);

    return style;
}

void DisplayService::LoadFonts() {
    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();

    style.FontSizeBase = 16.0f;

    ImFontConfig config;

    auto embedfs = cmrc::Ymir_sdl3_rc::get_filesystem();

    io.Fonts->Clear();

    auto loadFont = [&](std::string_view name, const char *path, bool includeIcons) {
        std::span configName{config.Name};
        std::fill(configName.begin(), configName.end(), '\0');
        name = name.substr(0, configName.size());
        std::copy(name.begin(), name.end(), configName.begin());

        cmrc::file file = embedfs.open(path);

        ImFont *font = io.Fonts->AddFontFromMemoryTTF((void *)file.begin(), file.size(), style.FontSizeBase, &config);
        IM_ASSERT(font != nullptr);
        if (includeIcons) {
            cmrc::file iconFile = embedfs.open("fonts/MaterialSymbolsOutlined_Filled-Regular.ttf");

            static const ImWchar iconsRanges[] = {ICON_MIN_MS, ICON_MAX_16_MS, 0};
            ImFontConfig iconsConfig;
            iconsConfig.MergeMode = true;
            iconsConfig.PixelSnapH = true;
            iconsConfig.PixelSnapV = true;
            iconsConfig.GlyphMinAdvanceX = 20.0f;
            iconsConfig.GlyphOffset.y = 4.0f;
            font = io.Fonts->AddFontFromMemoryTTF((void *)iconFile.begin(), iconFile.size(), 20.0f, &iconsConfig,
                                                  iconsRanges);
        }
        return font;
    };

    auto mergeFont = [&](const char *path) {
        ImFontConfig mergeConfig = config;
        mergeConfig.MergeMode = true;

        cmrc::file file = embedfs.open(path);

        ImFont *font =
            io.Fonts->AddFontFromMemoryTTF((void *)file.begin(), file.size(), style.FontSizeBase, &mergeConfig);
        IM_ASSERT(font != nullptr);
        return font;
    };

    m_context.fonts.sansSerif.regular = loadFont("SplineSans Medium", "fonts/SplineSans-Medium.ttf", true);
    m_context.fonts.sansSerif.regular = mergeFont("fonts/MPLUSU-Bold.ttf");
    m_context.fonts.sansSerif.bold = loadFont("SplineSans Bold", "fonts/SplineSans-Bold.ttf", true);
    m_context.fonts.sansSerif.bold = mergeFont("fonts/MPLUSU-ExtraBold.ttf");

    m_context.fonts.monospace.regular = loadFont("SplineSansMono Medium", "fonts/SplineSansMono-Medium.ttf", false);
    m_context.fonts.monospace.regular = mergeFont("fonts/MPLUSU-Bold.ttf");
    m_context.fonts.monospace.bold = loadFont("SplineSansMono Bold", "fonts/SplineSansMono-Bold.ttf", false);
    m_context.fonts.monospace.bold = mergeFont("fonts/MPLUSU-ExtraBold.ttf");

    m_context.fonts.display = loadFont("ZenDots Regular", "fonts/ZenDots-Regular.ttf", false);

    io.FontDefault = m_context.fonts.sansSerif.regular;
}

void DisplayService::OnDisplayAdded(SDL_DisplayID id) {
    auto &info = m_context.display.list[id];
    info.name = SDL_GetDisplayName(id);
    SDL_GetDisplayBounds(id, &info.bounds);
    info.modes.clear();

    devlog::info<grp::base>("Display {} ({}) added", id, info.name);

    int modeCount = 0;
    SDL_DisplayMode **modes = SDL_GetFullscreenDisplayModes(id, &modeCount);
    if (modes != nullptr) {
        util::ScopeGuard sgFreeModes{[&] { SDL_free(modes); }};

        info.modes.reserve(modeCount);
        for (SDL_DisplayMode **currMode = modes; *currMode != nullptr; ++currMode) {
            auto &mode = info.modes.emplace_back();
            mode.width = (*currMode)->w;
            mode.height = (*currMode)->h;
            mode.pixelFormat = (*currMode)->format;
            mode.refreshRate = (*currMode)->refresh_rate;
            mode.pixelDensity = (*currMode)->pixel_density;
        }
    }
}

void DisplayService::OnDisplayRemoved(SDL_DisplayID id) {
    if (auto it = m_context.display.list.find(id); it != m_context.display.list.end()) {
        devlog::info<grp::base>("Display {} ({}) removed", id, it->second.name);
        m_context.display.list.erase(it);
    } else {
        devlog::info<grp::base>("Display {} removed", id);
    }
}

void DisplayService::ApplyFullscreenMode() const {
    SDL_Window *window = m_context.screen.window;
    SDL_DisplayID displayID = m_context.GetSelectedDisplay();
    const auto &settings = m_settings;
    const auto &mode = settings.video.fullScreenMode;
    const bool borderless = settings.video.borderlessFullScreen;
    SDL_DisplayMode closest;
    if (!borderless && !mode.IsValid()) {
        // Use desktop resolution
        const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode(displayID);
        SDL_SetWindowFullscreenMode(window, desktopMode);
    } else if (!borderless && SDL_GetClosestFullscreenDisplayMode(displayID, mode.width, mode.height, mode.refreshRate,
                                                                  true, &closest)) {
        // Use exclusive display mode if found
        SDL_SetWindowFullscreenMode(window, &closest);
    } else {
        // Use borderless full screen mode, or fallback to borderless if no valid exclusive mode found
        if (settings.video.fullScreen && SDL_GetDisplayForWindow(window) != displayID) {
            // Move window to new display if in fullscreen mode
            SDL_Rect bounds;
            SDL_GetDisplayBounds(displayID, &bounds);
            SDL_SetWindowPosition(window, bounds.x, bounds.y);
            SDL_SetWindowSize(window, bounds.w, bounds.h);
        }
        SDL_SetWindowFullscreenMode(window, nullptr);
    }
    SDL_SyncWindow(window);
}

void DisplayService::PersistWindowGeometry() {
    const auto &settings = m_settings;
    if (settings.gui.rememberWindowGeometry) {
        int wx, wy, ww, wh;
        const bool posOK = SDL_GetWindowPosition(m_context.screen.window, &wx, &wy);
        const bool sizeOK = SDL_GetWindowSize(m_context.screen.window, &ww, &wh);
        if (posOK && sizeOK) {
            std::ofstream out{m_context.profile.GetPath(ProfilePath::PersistentState) / "window.txt"};
            out << fmt::format("{} {} {} {}", wx, wy, ww, wh);
        }
    }
}

} // namespace app::services
