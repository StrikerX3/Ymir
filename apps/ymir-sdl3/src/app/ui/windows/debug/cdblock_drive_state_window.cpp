#include "cdblock_drive_state_window.hpp"

namespace app::ui {

CDDriveStateWindow::CDDriveStateWindow(SharedContext &context)
    : CDBlockWindowBase(context)
    //, m_statusView(context)
    , m_stateTraceView(context) {

    m_windowConfig.name = "CD drive state";
}

void CDDriveStateWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(720 * m_context.displayScale, 300 * m_context.displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void CDDriveStateWindow::DrawContents() {
    // m_statusView.Display();
    ImGui::SeparatorText("State trace");
    m_stateTraceView.Display();
}

} // namespace app::ui
