#include "vdp2_color_calc_params_window.hpp"

namespace app::ui {

VDP2ColorCalcParamsWindow::VDP2ColorCalcParamsWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_layerParamsView(context, m_vdp) {

    m_windowConfig.name = "VDP2 color calculation parameters";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2ColorCalcParamsWindow::DrawContents() {
    m_layerParamsView.Display();
}

} // namespace app::ui
