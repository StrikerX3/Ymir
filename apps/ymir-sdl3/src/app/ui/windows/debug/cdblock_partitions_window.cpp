#include "cdblock_partitions_window.hpp"

using namespace ymir;

namespace app::ui {

CDBlockPartitionsWindow::CDBlockPartitionsWindow(SharedContext &context)
    : CDBlockWindowBase(context)
    , m_partitionsView(context) {

    m_windowConfig.name = "CD Block partitions";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void CDBlockPartitionsWindow::PrepareWindow() {}

void CDBlockPartitionsWindow::DrawContents() {
    m_partitionsView.Display();
}

} // namespace app::ui
