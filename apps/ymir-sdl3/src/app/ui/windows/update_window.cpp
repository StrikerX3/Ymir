#include "update_window.hpp"

#include <ymir/version.hpp>

namespace app::ui {

UpdateWindow::UpdateWindow(SharedContext &context)
    : WindowBase(context) {

    m_windowConfig.name = "Update available";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
}

void UpdateWindow::PrepareWindow() {
    // Close window if no updates are actually available
    if (!m_context.targetUpdate) {
        Open = false;
        return;
    }

    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
}

void UpdateWindow::DrawContents() {
    assert(m_context.targetUpdate.has_value());

    auto &info = m_context.targetUpdate->info;

    ImGui::TextUnformatted("A new version of Ymir is available.");
    ImGui::TextUnformatted("Current version: " Ymir_FULL_VERSION);
    ImGui::TextUnformatted("New version: ");
    ImGui::SameLine(0, 0);
    ImGui::TextLinkOpenURL(info.version.to_string().c_str(), info.downloadURL.c_str());
    ImGui::TextLinkOpenURL("Release notes", info.releaseNotesURL.c_str());

    ImGui::Separator();
    if (ImGui::Button("Close")) {
        Open = false;
    }
}

} // namespace app::ui
