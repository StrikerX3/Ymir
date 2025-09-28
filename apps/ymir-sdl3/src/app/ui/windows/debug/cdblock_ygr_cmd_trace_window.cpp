#include "cdblock_ygr_cmd_trace_window.hpp"

namespace app::ui {

YGRCommandTraceWindow::YGRCommandTraceWindow(SharedContext &context)
    : CDBlockWindowBase(context)
    , m_cmdTraceView(context) {

    m_windowConfig.name = "YGR command trace";
}

void YGRCommandTraceWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(450 * m_context.displayScale, 180 * m_context.displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void YGRCommandTraceWindow::DrawContents() {
    m_cmdTraceView.Display();
}

} // namespace app::ui
