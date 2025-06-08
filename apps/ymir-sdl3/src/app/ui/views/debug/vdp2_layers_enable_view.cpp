#include "vdp2_layers_enable_view.hpp"

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2LayersEnableView::VDP2LayersEnableView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2LayersEnableView::Display() {
    auto checkbox = [&](const char *name, vdp::Layer layer) {
        bool enabled = m_vdp.IsLayerEnabled(layer);
        if (ImGui::Checkbox(name, &enabled)) {
            m_vdp.SetLayerEnabled(layer, enabled);
        }
    };
    checkbox("Sprite", vdp::Layer::Sprite);
    checkbox("RBG0", vdp::Layer::RBG0);
    checkbox("NBG0/RBG1", vdp::Layer::NBG0_RBG1);
    checkbox("NBG1/EXBG", vdp::Layer::NBG1_EXBG);
    checkbox("NBG2", vdp::Layer::NBG2);
    checkbox("NBG3", vdp::Layer::NBG3);
}

} // namespace app::ui
