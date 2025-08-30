#include "sh2_watchpoints_window.hpp"

using namespace ymir;

namespace app::ui {

SH2WatchpointsWindow::SH2WatchpointsWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_watchpointsView(context, m_sh2) {

    m_windowConfig.name = fmt::format("{}SH2 watchpoints", master ? 'M' : 'S');
    // m_windowConfig.flags = ImGuiWindowFlags_MenuBar;
}

void SH2WatchpointsWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(285 * m_context.displayScale, 300 * m_context.displayScale),
                                        ImVec2(285 * m_context.displayScale, FLT_MAX));
}

void SH2WatchpointsWindow::DrawContents() {
    m_watchpointsView.Display();
}

} // namespace app::ui
