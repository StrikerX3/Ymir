#include "vdp2_registers_window.hpp"

namespace app::ui {

VDP2RegistersWindow::VDP2RegistersWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_regsView(context, m_vdp) {

    m_windowConfig.name = "VDP2 registers";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2RegistersWindow::DrawContents() {
    m_regsView.Display();
}

} // namespace app::ui
