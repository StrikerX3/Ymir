#include "vdp_frame_capture_window.hpp"

namespace app::ui {

VDPFrameCaptureWindow::VDPFrameCaptureWindow(SharedContext &context)
    : VDPWindowBase(context)
/*, m_listView(context, m_vdp)*/
/*, m_displayView(context, m_vdp)*/
/*, m_cmdListView(context, m_vdp)*/
/*, m_pixelDetailsView(context, m_vdp)*/
{

    m_windowConfig.name = "VDP frame capture";
}

void VDPFrameCaptureWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(200 * m_context.displayScale, 200 * m_context.displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void VDPFrameCaptureWindow::DrawContents() {
    // m_listView.Display();
    // m_displayView.Display();
    // m_cmdListView.Display();
    // m_pixelDetailsView.Display();
}

} // namespace app::ui
