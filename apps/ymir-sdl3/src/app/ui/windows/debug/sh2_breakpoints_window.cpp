#include "sh2_breakpoints_window.hpp"

using namespace ymir;

namespace app::ui {

SH2BreakpointsWindow::SH2BreakpointsWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_breakpointsView(context, m_sh2) {

    m_windowConfig.name = fmt::format("{}SH2 breakpoints", master ? 'M' : 'S');
    // m_windowConfig.flags = ImGuiWindowFlags_MenuBar;
}

void SH2BreakpointsWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(260 * m_context.displayScale, 250 * m_context.displayScale),
                                        ImVec2(260 * m_context.displayScale, FLT_MAX));
}

void SH2BreakpointsWindow::DrawContents() {
    m_breakpointsView.Display();
}

} // namespace app::ui
