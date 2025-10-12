#include "window_base.hpp"

namespace app::ui {

WindowBase::WindowBase(SharedContext &context)
    : m_context(context) {}

void WindowBase::Display() {
    if (!Open) {
        return;
    }

    PrepareWindow();
    if (!Open) {
        // Second opportunity to close the window
        return;
    }

    if (m_focusRequested) {
        m_focusRequested = false;
        ImGui::SetNextWindowFocus();
    }

    if (ImGui::Begin(m_windowConfig.name.c_str(), &Open, m_windowConfig.flags)) {
        DrawContents();

        // Close the window if nothing is focused and B/Circle is pressed

        if (m_windowConfig.allowClosingWithGamepad && ImGui::IsWindowFocused() && !ImGui::IsAnyItemFocused() &&
            !ImGui::GetIO().NavVisible && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
            Open = false;
        }
    }
    ImGui::End();
}

void WindowBase::RequestFocus() {
    if (Open) {
        m_focusRequested = true;
    }
}

} // namespace app::ui
