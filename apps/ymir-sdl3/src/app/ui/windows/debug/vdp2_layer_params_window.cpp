#include "vdp2_layer_params_window.hpp"

namespace app::ui {

VDP2LayerParamsWindow::VDP2LayerParamsWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_layerParamsView(context, m_vdp) {

    m_windowConfig.name = "VDP2 layer parameters";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2LayerParamsWindow::DrawContents() {
    m_layerParamsView.Display();
}

} // namespace app::ui
