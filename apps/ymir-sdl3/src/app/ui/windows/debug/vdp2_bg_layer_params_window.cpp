#include "vdp2_bg_layer_params_window.hpp"

namespace app::ui {

VDP2BGLayerParamsWindow::VDP2BGLayerParamsWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_layerParamsView(context, m_vdp) {

    m_windowConfig.name = "VDP2 background layer parameters";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2BGLayerParamsWindow::DrawContents() {
    m_layerParamsView.Display();
}

} // namespace app::ui
