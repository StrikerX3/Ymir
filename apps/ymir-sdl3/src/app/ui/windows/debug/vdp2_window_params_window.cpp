#include "vdp2_window_params_window.hpp"

namespace app::ui {

VDP2WindowParamsWindow::VDP2WindowParamsWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_layerParamsView(context, m_vdp) {

    m_windowConfig.name = "VDP2 window parameters";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2WindowParamsWindow::DrawContents() {
    m_layerParamsView.Display();
}

} // namespace app::ui
