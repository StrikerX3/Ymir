#include "sh2_breakpoints_window.hpp"

using namespace ymir;

namespace app::ui {

SH2BreakpointsWindow::SH2BreakpointsWindow(SharedContext &context, bool master, SH2BreakpointsManager &bkptManager)
    : SH2WindowBase(context, master)
    , m_breakpointsView(context, bkptManager) {

    m_windowConfig.name = fmt::format("{}SH2 breakpoints", master ? 'M' : 'S');
    // m_windowConfig.flags = ImGuiWindowFlags_MenuBar;
}

void SH2BreakpointsWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(250 * m_context.displayScale, 250 * m_context.displayScale),
                                        ImVec2(250 * m_context.displayScale, FLT_MAX));
}

void SH2BreakpointsWindow::DrawContents() {
    m_breakpointsView.Display();
}

} // namespace app::ui
