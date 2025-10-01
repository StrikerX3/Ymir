#include "vdp2_debug_overlay_window.hpp"

namespace app::ui {

VDP2DebugOverlayWindow::VDP2DebugOverlayWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_debugOverlayView(context, m_vdp) {

    m_windowConfig.name = "VDP2 debug overlay";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2DebugOverlayWindow::PrepareWindow() {
    /*ImGui::SetNextWindowSizeConstraints(ImVec2(860 * m_context.displayScale, 250 * m_context.displayScale),
                                        ImVec2(860 * m_context.displayScale, FLT_MAX));*/
}

void VDP2DebugOverlayWindow::DrawContents() {
    m_debugOverlayView.Display();
}

} // namespace app::ui
