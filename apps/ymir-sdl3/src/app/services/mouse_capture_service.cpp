#include "mouse_capture_service.hpp"

#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <ymir/util/dev_log.hpp>

#include <SDL3/SDL.h>
#include <cassert>
#include <fmt/format.h>
#include <imgui.h>

namespace app::services {

MouseCaptureService::MouseCaptureService(SharedContext &context, Settings &settings)
    : m_context(context)
    , m_settings(settings) {}

MouseCaptureService::~MouseCaptureService() {
    ReleaseAllMice();
}

bool MouseCaptureService::CaptureMouse(uint32 id, uint32 port) {
    // Prevent capturing global mouse
    if (id == 0) {
        return false;
    }

    // Bail out if mouse is already bound to a peripheral
    if (m_capturedMice.contains(id)) {
        return false;
    }

    // Bind mouse to peripheral
    m_capturedMice[id] = port;
    const char *name = SDL_GetMouseNameForID(id);
    if (name != nullptr) {
        m_context.DisplayMessage(fmt::format("{} bound to {}", name, GetPeripheralName(port)));
    } else {
        m_context.DisplayMessage(fmt::format("Mouse {} bound to {}", id, GetPeripheralName(port)));
    }
    ConfigureMouseCapture();

    m_mouseCaptureActive = false;

    return true;
}

bool MouseCaptureService::ReleaseMouse(uint32 id) {
    // Prevent releasing global mouse
    if (id == 0) {
        return false;
    }

    // Bail out if mouse not bound to a peripheral
    if (!m_capturedMice.contains(id)) {
        return false;
    }

    // Release mouse
    const uint32 port = m_capturedMice.at(id);
    m_capturedMice.erase(id);

    const char *name = SDL_GetMouseNameForID(id);
    if (name != nullptr) {
        m_context.DisplayMessage(fmt::format("{} released from {}", name, GetPeripheralName(port)));
    } else {
        m_context.DisplayMessage(fmt::format("Mouse {} released from {}", id, GetPeripheralName(port)));
    }

    if (m_capturedMice.empty()) {
        m_context.DisplayMessage("Leaving mouse capture mode");
    }

    ConfigureMouseCapture();
    (void)m_context.inputContext.UnmapMouseInputs(id);

    return true;
}

bool MouseCaptureService::CaptureSystemMouse(uint32 port) {
    const bool captured = !m_systemMouseCaptured;
    if (captured) {
        m_context.DisplayMessage(fmt::format("Mouse cursor bound to {}", GetPeripheralName(port)));
        m_context.DisplayMessage("Press ESC to release");
        m_systemMouseCaptured = true;
        ConfigureMouseCapture();
        m_systemMousePeripheral = port;
    }
    return captured;
}

bool MouseCaptureService::ReleaseSystemMouse() {
    const bool released = m_systemMouseCaptured;
    if (released) {
        m_context.DisplayMessage(
            fmt::format("Mouse cursor released from {}", GetPeripheralName(m_systemMousePeripheral)));
        ConfigureMouseCapture();
        m_systemMouseCaptured = false;
        m_context.virtuaGunInputs[m_systemMousePeripheral].mouseAbsolute = false;
        (void)m_context.inputContext.UnmapMouseInputs(0);
    }
    return released;
}

void MouseCaptureService::ReleaseAllMice() {
    bool released = m_mouseCaptureActive || m_systemMouseCaptured || !m_capturedMice.empty();

    if (m_systemMouseCaptured) {
        m_systemMouseCaptured = false;
        (void)m_context.inputContext.UnmapMouseInputs(0);
    }
    for (auto [id, _] : m_capturedMice) {
        (void)m_context.inputContext.UnmapMouseInputs(id);
    }
    m_capturedMice.clear();

    m_mouseCaptureActive = false;

    for (uint32 i = 0; i < 2; ++i) {
        m_context.virtuaGunInputs[i].mouseAbsolute = false;
    }

    if (released) {
        m_context.DisplayMessage("All mice released");
        ConfigureMouseCapture();
    }
}

void MouseCaptureService::ConfigureMouseCapture() {
    const auto &settings = m_context.serviceLocator.GetRequired<Settings>();
    const bool grabSystemCursor = settings.input.mouse.lockToDisplay;
    const bool relativeMode = m_mouseCaptureActive || !m_capturedMice.empty();
    const bool captured = (m_systemMouseCaptured && grabSystemCursor) || relativeMode;
    const bool grabbed = captured && !relativeMode;
    const bool wasRelativeMode = SDL_GetWindowRelativeMouseMode(m_context.screen.window);
    SDL_SetWindowMouseGrab(m_context.screen.window, grabbed);
    SDL_SetWindowMouseRect(m_context.screen.window, grabbed ? &m_mouseRect : nullptr);
    SDL_SetWindowRelativeMouseMode(m_context.screen.window, relativeMode);
    if (captured) {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else {
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    if (wasRelativeMode != relativeMode) {
        if (relativeMode) {
            m_context.DisplayMessage("Entering mouse capture mode");
            m_context.DisplayMessage("Press ESC to release mice");
        } else {
            m_context.DisplayMessage("Leaving mouse capture mode");
        }
    }

    devlog::debug<grp::base>("Mouse capture config: captured={} relative={} grab={}", captured, relativeMode, grabbed);
}

void MouseCaptureService::ConnectMouseToPeripheral(uint32 id) {
    using PeripheralType = ymir::peripheral::PeripheralType;
    using CaptureMode = Settings::Input::Mouse::CaptureMode;

    // Build list of candidate peripherals.
    // Bail out if none are available.
    const std::set<uint32> candidates = GetCandidatePeripheralsForMouseCapture();
    if (candidates.empty()) {
        return;
    }

    CaptureMode captureMode = m_settings.input.mouse.captureMode;

    // Force PhysicalMouse mode if a Shuttle Mouse is connected to any port
    for (uint32 port : candidates) {
        if (m_settings.input.ports[port].type == PeripheralType::ShuttleMouse) {
            captureMode = CaptureMode::PhysicalMouse;
            break;
        }
    }

    // Bail out if mouse is already bound to a peripheral
    if (captureMode == CaptureMode::SystemCursor && m_systemMouseCaptured) {
        return;
    }
    if (captureMode == CaptureMode::PhysicalMouse && m_capturedMice.contains(id)) {
        return;
    }

    if (captureMode == CaptureMode::SystemCursor) {
        // Force global mouse ID when in system cursor capture mode
        id = 0;
    } else if (id == 0) {
        // Engage mouse capture in physical mouse capture mode when using the global mouse ID
        m_mouseCaptureActive = true;
        ConfigureMouseCapture();
        return;
    }

    // TODO: allow user to pick port when there are multiple candidates
    // - show a popup asking which one the player wants to bind to (or Cancel)
    // - cancel popup:
    //   - on ESC press
    //   - if the app loses focus
    //   - if mouse capture mode changes
    //   - if the target mouse is removed
    //   - if the valid controller list is changed
    const uint32 port = *candidates.begin();
    const PeripheralType type = m_settings.input.ports[port].type;

    assert(type == PeripheralType::VirtuaGun || type == PeripheralType::ShuttleMouse);

    const bool captured = [&] {
        switch (captureMode) {
        case CaptureMode::SystemCursor: return CaptureSystemMouse(port);
        case CaptureMode::PhysicalMouse: return CaptureMouse(id, port);
        }
        return false;
    }();

    // Bind inputs
    auto &inputCtx = m_context.inputContext;
    if (type == PeripheralType::VirtuaGun) {
        auto *actionCtx = &m_context.virtuaGunInputs[port];
        if (captureMode == CaptureMode::PhysicalMouse) {
            actionCtx->mouseAbsolute = false;
            (void)inputCtx.MapAction({id, input::MouseAxis2D::MouseRelative}, actions::virtua_gun::MouseRelMove,
                                     actionCtx);
        } else {
            actionCtx->mouseAbsolute = true;
            (void)inputCtx.MapAction({0, input::MouseAxis2D::MouseAbsolute}, actions::virtua_gun::MouseAbsMove,
                                     actionCtx);
        }
        // TODO: map inputs according to settings
        (void)inputCtx.MapAction({id, input::MouseButton::Left}, actions::virtua_gun::Trigger, actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Right}, actions::virtua_gun::Reload, actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Middle}, actions::virtua_gun::Start, actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Extra1}, actions::virtua_gun::Start, actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Extra2}, actions::virtua_gun::Start, actionCtx);
    } else if (type == PeripheralType::ShuttleMouse) {
        assert(captureMode == CaptureMode::PhysicalMouse);

        auto *actionCtx = &m_context.shuttleMouseInputs[port];
        (void)inputCtx.MapAction({id, input::MouseAxis2D::MouseRelative}, actions::shuttle_mouse::MouseRelMove,
                                 actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Left}, actions::shuttle_mouse::Left, actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Right}, actions::shuttle_mouse::Right, actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Middle}, actions::shuttle_mouse::Middle, actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Extra1}, actions::shuttle_mouse::Start, actionCtx);
        (void)inputCtx.MapAction({id, input::MouseButton::Extra2}, actions::shuttle_mouse::Start, actionCtx);
    }
}

bool MouseCaptureService::IsMouseCaptured() const {
    return m_systemMouseCaptured || !m_capturedMice.empty();
}

bool MouseCaptureService::HasValidPeripheralsForMouseCapture() const {
    return !GetCandidatePeripheralsForMouseCapture().empty();
}

std::set<uint32> MouseCaptureService::GetCandidatePeripheralsForMouseCapture() const {
    std::set<uint32> candidates = m_validPeripheralsForMouseCapture;
    for (auto [_, port] : m_capturedMice) {
        candidates.erase(port);
    }
    if (m_systemMouseCaptured) {
        candidates.erase(m_systemMousePeripheral);
    }
    return candidates;
}

std::string MouseCaptureService::GetPeripheralName(uint32 port) const {
    const ymir::peripheral::PeripheralType type = m_settings.input.ports[port].type;
    return fmt::format("{} on port {}", ymir::peripheral::GetPeripheralName(type), port + 1);
}

bool MouseCaptureService::ShouldHideMouse(bool wantCaptureMouse) const {
    return (m_systemMouseCaptured && !wantCaptureMouse) || !m_capturedMice.empty();
}

bool MouseCaptureService::IsPhysicalMouseCapturedOrActive() const {
    return !m_capturedMice.empty() || m_mouseCaptureActive;
}

bool MouseCaptureService::ShouldDisableImGuiMouseInputs() const {
    return m_systemMouseCaptured || HasValidPeripheralsForMouseCapture();
}

bool MouseCaptureService::SetPeripheralType(uint32 port, ymir::peripheral::PeripheralType type) {
    using PeripheralType = ymir::peripheral::PeripheralType;
    bool changed;
    if (type == PeripheralType::VirtuaGun || type == PeripheralType::ShuttleMouse) {
        changed = m_validPeripheralsForMouseCapture.insert(port).second;
    } else {
        changed = m_validPeripheralsForMouseCapture.erase(port) > 0;
    }
    return changed;
}

void MouseCaptureService::SetMouseRect(int x, int y, int w, int h) {
    m_mouseRect.x = x;
    m_mouseRect.y = y;
    m_mouseRect.w = w;
    m_mouseRect.h = h;

    const auto &settings = m_context.serviceLocator.GetRequired<Settings>();
    const bool grabSystemCursor = settings.input.mouse.lockToDisplay;
    const bool relativeMode = m_mouseCaptureActive || !m_capturedMice.empty();
    const bool captured = (m_systemMouseCaptured && grabSystemCursor) || relativeMode;
    const bool grabbed = captured && !relativeMode;
    SDL_SetWindowMouseRect(m_context.screen.window, grabbed ? &m_mouseRect : nullptr);
}

} // namespace app::services
