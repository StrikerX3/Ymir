#include "vdp2_vram_access_patterns_window.hpp"

namespace app::ui {

VDP2VRAMAccessPatternsWindow::VDP2VRAMAccessPatternsWindow(SharedContext &context)
    : VDPWindowBase(context)
    , m_accessPatternsView(context, m_vdp) {

    m_windowConfig.name = "VDP2 VRAM access patterns";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void VDP2VRAMAccessPatternsWindow::DrawContents() {
    m_accessPatternsView.Display();
}

} // namespace app::ui
