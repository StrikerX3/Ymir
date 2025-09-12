#include "cdblock_filters_window.hpp"

using namespace ymir;

#include <ymir/hw/cdblock/cdblock.hpp>

namespace app::ui {

CDBlockFiltersWindow::CDBlockFiltersWindow(SharedContext &context)
    : CDBlockWindowBase(context)
    , m_filtersView(context) {

    m_windowConfig.name = "CD Block filters";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void CDBlockFiltersWindow::PrepareWindow() {}

void CDBlockFiltersWindow::DrawContents() {
    m_filtersView.Display();
}

} // namespace app::ui
