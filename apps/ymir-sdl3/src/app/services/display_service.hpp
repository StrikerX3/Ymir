#pragma once

#include <SDL3/SDL_video.h>
#include <app/settings.hpp>
#include <app/shared_context.hpp>
#include <imgui.h>

namespace app::services {

/// @brief Handles UI scale, styles, fonts, fullscreen mode, and window sizes.
class DisplayService {
public:
    DisplayService(SharedContext &context, Settings &settings);
    ~DisplayService() = default;

    DisplayService(const DisplayService &) = delete;
    DisplayService &operator=(const DisplayService &) = delete;

    /// @brief Rescales the UI based on system or custom DPI scale.
    /// @param[in] displayScale New scale factor.
    void RescaleUI(float displayScale);

    /// @brief Reloads the ImGui colors and style properties.
    /// @param[in] displayScale UI scale factor to adjust spacing/padding.
    void ReloadStyle(float displayScale);

    /// @brief Registers embedded fonts with ImGui.
    void LoadFonts();

    /// @brief Handles new monitor connections.
    /// @param[in] id SDL display identifier.
    void OnDisplayAdded(SDL_DisplayID id);

    /// @brief Handles monitor disconnections.
    /// @param[in] id SDL display identifier.
    void OnDisplayRemoved(SDL_DisplayID id);

    /// @brief Applies the fullscreen setting to the active window.
    void ApplyFullscreenMode() const;

    /// @brief Saves the current window size and position to the profile.
    void PersistWindowGeometry();

private:
    SharedContext &m_context;
    Settings &m_settings;
};

} // namespace app::services
