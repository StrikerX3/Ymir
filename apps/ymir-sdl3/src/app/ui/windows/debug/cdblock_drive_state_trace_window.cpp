#include "cdblock_drive_state_trace_window.hpp"

namespace app::ui {

CDDriveStateTraceWindow::CDDriveStateTraceWindow(SharedContext &context)
    : CDBlockWindowBase(context)
    , m_stateTraceView(context) {

    m_windowConfig.name = "CD drive state trace";
}

void CDDriveStateTraceWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(720 * m_context.displayScale, 250 * m_context.displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void CDDriveStateTraceWindow::DrawContents() {
    m_stateTraceView.Display();
}

} // namespace app::ui
