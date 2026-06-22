#include "vdp2_sprite_layer_params_window.hpp"

namespace app::ui {

VDP2SpriteLayerParamsWindow::VDP2SpriteLayerParamsWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_layerParamsView(context, m_vdp) {

    m_windowConfig.name = "VDP2 sprite layer parameters";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2SpriteLayerParamsWindow::DrawContents() {
    m_layerParamsView.Display();
}

} // namespace app::ui
