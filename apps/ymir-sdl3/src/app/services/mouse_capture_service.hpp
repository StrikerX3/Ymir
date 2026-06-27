#pragma once

#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <SDL3/SDL_video.h>

#include <set>
#include <string>
#include <unordered_map>

namespace app::services {

/// @brief Handles capturing the mouse cursor and routing physical mouse inputs.
class MouseCaptureService {
public:
    MouseCaptureService(SharedContext &context, Settings &settings);
    ~MouseCaptureService();

    MouseCaptureService(const MouseCaptureService &) = delete;
    MouseCaptureService &operator=(const MouseCaptureService &) = delete;

    /// @brief Captures a physical mouse input device to a controller port.
    /// @param[in] id Mouse device identifier.
    /// @param[in] port Destination controller port.
    /// @return True if successful.
    bool CaptureMouse(uint32 id, uint32 port);

    /// @brief Releases capture of a physical mouse input device.
    /// @param[in] id Mouse device identifier.
    /// @return True if successful.
    bool ReleaseMouse(uint32 id);

    /// @brief Grabs the host system mouse to use as a lightgun or Saturn mouse.
    /// @param[in] port Controller port.
    /// @return True if successful.
    bool CaptureSystemMouse(uint32 port);

    /// @brief Releases the system mouse cursor.
    /// @return True if successful.
    bool ReleaseSystemMouse();

    /// @brief Releases both system mouse capture and all physical mouse devices.
    void ReleaseAllMice();

    /// @brief Configures window grab and relative cursor modes based on capture state.
    void ConfigureMouseCapture();

    /// @brief Links a mouse device to its target peripheral on click.
    /// @param[in] id Mouse device identifier.
    void ConnectMouseToPeripheral(uint32 id);

    /// @brief Checks if any mouse (physical or system) is currently captured.
    [[nodiscard]] bool IsMouseCaptured() const;

    /// @brief Checks if any mouse-compatible peripherals are configured.
    [[nodiscard]] bool HasValidPeripheralsForMouseCapture() const;

    /// @brief Gets ports with mouse peripherals that aren't captured yet.
    [[nodiscard]] std::set<uint32> GetCandidatePeripheralsForMouseCapture() const;

    /// @brief Gets the name of the peripheral on a given port.
    [[nodiscard]] std::string GetPeripheralName(uint32 port) const;

    /// @brief Checks if the cursor should be hidden.
    /// @param[in] wantCaptureMouse Whether the app wants to capture mouse inputs.
    [[nodiscard]] bool ShouldHideMouse(bool wantCaptureMouse) const;

    /// @brief Checks if physical mouse capture is active.
    [[nodiscard]] bool IsPhysicalMouseCapturedOrActive() const;

    /// @brief Checks if ImGui should ignore mouse inputs due to capture.
    [[nodiscard]] bool ShouldDisableImGuiMouseInputs() const;

    /// @brief Checks if the system cursor is captured.
    [[nodiscard]] bool IsSystemMouseCaptured() const {
        return m_systemMouseCaptured;
    }

    /// @brief Gets all active physical mouse captures (mouse ID -> port).
    [[nodiscard]] const std::unordered_map<uint32, uint32> &GetCapturedMice() const {
        return m_capturedMice;
    }

    /// @brief Registers or unregisters a port when the peripheral type changes.
    bool SetPeripheralType(uint32 port, ymir::peripheral::PeripheralType type);

    /// @brief Configures the mouse cursor confinement area when captured.
    /// @param[in] x horizontal position on the screen
    /// @param[in] y vertical position on the screen
    /// @param[in] w area width
    /// @param[in] h area height
    void SetMouseRect(int x, int y, int w, int h);

private:
    SharedContext &m_context;
    Settings &m_settings;

    std::unordered_map<uint32, uint32> m_capturedMice{}; // mouse ID -> peripheral index
    bool m_mouseCaptureActive = false;
    bool m_systemMouseCaptured = false;
    uint32 m_systemMousePeripheral = 0;
    std::set<uint32> m_validPeripheralsForMouseCapture{};

    SDL_Rect m_mouseRect;
};

} // namespace app::services
