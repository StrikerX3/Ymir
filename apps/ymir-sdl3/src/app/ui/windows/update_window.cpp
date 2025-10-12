#include "update_window.hpp"

namespace app::ui {

UpdateWindow::UpdateWindow(SharedContext &context)
    : WindowBase(context) {

    m_windowConfig.name = "About";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void UpdateWindow::PrepareWindow() {
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
}

void UpdateWindow::DrawContents() {}

} // namespace app::ui
