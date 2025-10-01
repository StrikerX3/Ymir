#include "vdp2_cram_window.hpp"

namespace app::ui {

VDP2CRAMWindow::VDP2CRAMWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_cramView(context, m_vdp) {

    m_windowConfig.name = "VDP2 Color RAM palette";
    // m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2CRAMWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(855 * m_context.displayScale, 250 * m_context.displayScale),
                                        ImVec2(855 * m_context.displayScale, FLT_MAX));
}

void VDP2CRAMWindow::DrawContents() {
    m_cramView.Display();
}

} // namespace app::ui
